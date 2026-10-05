#include "Project.hpp"
#include "../../shared/SubtitleText.hpp"
#include "Paths.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

static constexpr const char* kEntryKeys[] = { "character", "ja", "en", "text", "status" };
static constexpr const char* kAudioKeys[] = { "text", "source", "duration", "displayDuration" };
static constexpr double kMergeSeconds = 1.5;

double NowSeconds()
{
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return duration<double>(steady_clock::now() - start).count();
}

bool ReadFile(const std::string& path, std::string& out)
{
    std::ifstream file(U8Path(path), std::ios::binary);
    if (!file)
        return false;
    std::stringstream ss;
    ss << file.rdbuf();
    out = ss.str();
    return true;
}

bool WriteFileAtomic(const std::string& path, const std::string& data)
{
    const fs::path target = U8Path(path);
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream file(tmp, std::ios::binary | std::ios::trunc);
        if (!file)
            return false;
        file.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!file)
            return false;
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec)
    {
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

const char* StatusKey(Status s)
{
    switch (s)
    {
    case Status::Translated: return "translated";
    case Status::Reviewed:   return "reviewed";
    default:                 return "pending";
    }
}

Status ParseStatus(const std::string& s)
{
    if (s == "translated") return Status::Translated;
    if (s == "reviewed")   return Status::Reviewed;
    return Status::Pending;
}

// Same key order as lines.py ENTRY_KEYS / AUDIO_KEYS; unknown keys stay at the end.
static ojson Ordered(const ojson& obj, const char* const* keys, size_t count)
{
    ojson out = ojson::object();
    for (size_t i = 0; i < count; ++i)
        if (obj.contains(keys[i]))
            out[keys[i]] = obj[keys[i]];
    for (auto it = obj.begin(); it != obj.end(); ++it)
        if (!out.contains(it.key()))
            out[it.key()] = it.value();
    return out;
}

static ojson NormalizeEntry(ojson e)
{
    for (const AudioLang& lang : kAudioLangs)
        if (e.contains(lang.key) && e[lang.key].is_object())
            e[lang.key] = Ordered(e[lang.key], kAudioKeys, std::size(kAudioKeys));
    return Ordered(e, kEntryKeys, std::size(kEntryKeys));
}

// Same rule as lines.py is_lines_file(): a non-empty object whose values are all
// objects with a "ja" or "en" object.
static bool IsLinesFile(const ojson& j)
{
    if (!j.is_object() || j.empty())
        return false;
    for (auto it = j.begin(); it != j.end(); ++it)
    {
        const ojson& entry = it.value();
        if (!entry.is_object())
            return false;
        const bool hasLang = std::any_of(std::begin(kAudioLangs), std::end(kAudioLangs), [&](const AudioLang& lang) {
            auto it = entry.find(lang.key);
            return it != entry.end() && it->is_object();
        });
        if (!hasLang)
            return false;
    }
    return true;
}

static std::string Dump(const ojson& j, int indent)
{
    return j.dump(indent, ' ', false, ojson::error_handler_t::replace) + "\n";
}

// ---------------------------------------------------------------------------
// Load / save
// ---------------------------------------------------------------------------

bool Project::Load(const std::string& dataDir, std::string& error)
{
    m_loaded = false;
    lines.clear();
    characters.clear();
    m_undo.clear();
    m_undoPos = 0;
    m_dirty = false;
    m_changedLines.clear();
    m_dataDir = dataDir;

    categories.clear();
    skippedFiles.clear();
    m_fileNames.clear();
    m_files.clear();

    const fs::path linesDir = U8Path(dataDir) / "lines";
    std::vector<std::pair<std::string, fs::path>> found;
    std::error_code ec;
    for (fs::directory_iterator it(linesDir, ec), end; !ec && it != end; it.increment(ec))
    {
        const fs::path& path = it->path();
        if (!it->is_regular_file(ec) || path.extension() != ".json")
            continue;
        std::string name = U8String(path.stem());
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
        found.emplace_back(std::move(name), path);
    }
    std::sort(found.begin(), found.end());

    for (auto& [name, path] : found)
    {
        std::string text;
        if (!ReadFile(U8String(path), text))
        {
            error = U8String(path) + ": cannot read";
            return false;
        }
        ojson j = ojson::parse(text, nullptr, false);
        if (!IsLinesFile(j))
        {
            skippedFiles.push_back(U8String(path.filename()));
            continue;
        }
        // lines.py writes the ids sorted; keep that order.
        std::vector<std::string> keys;
        for (auto it = j.begin(); it != j.end(); ++it)
            keys.push_back(it.key());
        if (!std::is_sorted(keys.begin(), keys.end()))
        {
            std::sort(keys.begin(), keys.end());
            ojson sorted = ojson::object();
            for (const auto& k : keys)
                sorted[k] = std::move(j[k]);
            j = std::move(sorted);
        }
        categories.push_back(name);
        m_fileNames.push_back(U8String(path.filename()));
        m_files.push_back(std::move(j));
    }

    for (size_t c = 0; c < m_files.size(); ++c)
        for (auto it = m_files[c].begin(); it != m_files[c].end(); ++it)
            lines.push_back(Line{ int(c), it.key(), &it.value() });

    if (lines.empty())
    {
        error = "no lines found in " + U8String(linesDir);
        return false;
    }

    // characters.json
    std::string text;
    if (ReadFile(U8String(U8Path(dataDir) / "characters.json"), text))
    {
        ojson j = ojson::parse(text, nullptr, false);
        if (j.is_object())
        {
            for (auto it = j.begin(); it != j.end(); ++it)
            {
                Character ch;
                try { ch.id = std::stoi(it.key()); } catch (...) { continue; }
                ch.name = it.value().value("name", std::string());
                ch.portrait = it.value().value("portrait", std::string());
                characters[ch.id] = ch;
            }
            std::map<std::string, int> owner;
            for (auto& [id, ch] : characters)  // ascending ids
                if (!ch.portrait.empty())
                    ch.portraitOwner = owner.emplace(ch.portrait, id).first->second;
        }
    }

    // dialoguebox.json (optional)
    boxConfig = DialogueBoxCore::DefaultGameConfig();
    boxConfigDirty = false;
    if (fs::exists(U8Path(BoxConfigPath())))
    {
        std::string err, json;
        if (!ReadFile(BoxConfigPath(), json) || !DialogueBoxCore::ParseConfig(json, boxConfig, &err))
        {
            error = BoxConfigPath() + ": " + err;
            return false;
        }
    }

    m_loaded = true;
    ++m_revision;
    ++m_loadGeneration;
    return true;
}

std::string Project::BoxConfigPath() const
{
    return U8String(U8Path(m_dataDir) / "dialoguebox.json");
}

bool Project::SaveBoxConfig(std::string& error)
{
    if (!WriteFileAtomic(BoxConfigPath(), DialogueBoxCore::SerializeConfig(boxConfig)))
    {
        error = "cannot write " + BoxConfigPath();
        return false;
    }
    boxConfigDirty = false;
    return true;
}

bool Project::Save(std::string& error)
{
    if (!m_loaded)
        return false;
    const fs::path dir = U8Path(m_dataDir) / "lines";
    std::error_code ec;
    fs::create_directories(dir, ec);
    for (size_t c = 0; c < m_files.size(); ++c)
    {
        const std::string path = U8String(dir / U8Path(m_fileNames[c]));
        if (!WriteFileAtomic(path, Dump(m_files[c], 2)))
        {
            error = "cannot write " + path;
            return false;
        }
    }
    if (boxConfigDirty && !SaveBoxConfig(error))
        return false;
    m_dirty = false;
    return true;
}

// ---------------------------------------------------------------------------
// Read helpers
// ---------------------------------------------------------------------------

std::string Project::Text(const ojson& e)
{
    auto it = e.find("text");
    return it != e.end() && it->is_string() ? it->get<std::string>() : std::string();
}

std::string Project::SourceText(const ojson& e, const char* lang)
{
    auto it = e.find(lang);
    if (it == e.end() || !it->is_object())
        return {};
    auto t = it->find("text");
    return t != it->end() && t->is_string() ? t->get<std::string>() : std::string();
}

bool Project::HasAudio(const ojson& e, const char* lang)
{
    auto it = e.find(lang);
    return it != e.end() && it->is_object() && it->contains("duration");
}

double Project::AudioDuration(const ojson& e, const char* lang)
{
    if (!HasAudio(e, lang))
        return 0.0;
    const ojson& d = e[lang]["duration"];
    return d.is_number() ? d.get<double>() : 0.0;
}

std::optional<double> Project::DisplayDurationOverride(const ojson& e, const char* lang)
{
    auto it = e.find(lang);
    if (it == e.end() || !it->is_object())
        return std::nullopt;
    auto d = it->find("displayDuration");
    if (d == it->end() || !d->is_number())
        return std::nullopt;
    return d->get<double>();
}

double Project::EffectiveDuration(const ojson& e, const char* lang)
{
    if (auto o = DisplayDurationOverride(e, lang))
        return *o;
    return AudioDuration(e, lang);
}

Status Project::GetStatus(const ojson& e)
{
    auto it = e.find("status");
    return it != e.end() && it->is_string() ? ParseStatus(it->get<std::string>()) : Status::Pending;
}

int Project::GetCharacter(const ojson& e)
{
    auto it = e.find("character");
    return it != e.end() && it->is_number_integer() ? it->get<int>() : -1;
}

std::string Project::DisplayText(const ojson& e)
{
    return SubtitleText::DisplayText(Text(e), SourceText(e, "en"));
}

const Character* Project::FindCharacter(int id) const
{
    auto it = characters.find(id);
    return it != characters.end() ? &it->second : nullptr;
}

// ---------------------------------------------------------------------------
// Edits
// ---------------------------------------------------------------------------

void Project::Touch()
{
    m_dirty = true;
    m_lastEditTime = NowSeconds();
    ++m_revision;
}

void Project::Commit(size_t line, ojson after, const std::string& mergeKey)
{
    std::vector<Change> changes;
    changes.push_back(Change{ line, *lines[line].entry, NormalizeEntry(std::move(after)) });
    Commit(std::move(changes), mergeKey);
}

void Project::Commit(std::vector<Change> changes, const std::string& mergeKey)
{
    changes.erase(std::remove_if(changes.begin(), changes.end(),
        [](const Change& c) { return c.before == c.after; }), changes.end());
    if (changes.empty())
        return;

    for (const Change& c : changes)
    {
        *lines[c.line].entry = c.after;
        m_changedLines.push_back(c.line);
    }

    const double now = NowSeconds();
    m_undo.resize(m_undoPos);
    if (!mergeKey.empty() && !m_undo.empty() && changes.size() == 1)
    {
        Edit& last = m_undo.back();
        if (last.mergeKey == mergeKey && last.changes.size() == 1 && last.changes[0].line == changes[0].line &&
            now - last.time < kMergeSeconds)
        {
            last.changes[0].after = changes[0].after;
            last.time = now;
            if (last.changes[0].before == last.changes[0].after)  // typed and reverted: nothing to undo
            {
                m_undo.pop_back();
                m_undoPos = m_undo.size();
            }
            Touch();
            return;
        }
    }
    m_undo.push_back(Edit{ std::move(changes), mergeKey, now });
    m_undoPos = m_undo.size();
    Touch();
}

static ojson WithText(ojson e, const std::string& text)
{
    const std::string old = e.contains("text") && e["text"].is_string() ? e["text"].get<std::string>() : std::string();
    if (old == text)
        return e;
    e["text"] = text;
    const Status status = Project::GetStatus(e);
    const bool empty = SubtitleText::CollapseSpaces(text).empty();
    if (empty)
        e["status"] = StatusKey(Status::Pending);
    else if (status == Status::Pending || status == Status::Reviewed)
        e["status"] = StatusKey(Status::Translated);  // edited after review: needs another review
    return e;
}

void Project::SetText(size_t line, const std::string& text)
{
    Commit(line, WithText(*lines[line].entry, text), "text:" + lines[line].id);
}

void Project::SetTextMany(const std::vector<size_t>& targets, const std::string& text)
{
    std::vector<Change> changes;
    for (size_t line : targets)
        changes.push_back(Change{ line, *lines[line].entry, NormalizeEntry(WithText(*lines[line].entry, text)) });
    Commit(std::move(changes), {});
}

void Project::SetStatus(size_t line, Status s)
{
    ojson e = *lines[line].entry;
    e["status"] = StatusKey(s);
    Commit(line, std::move(e), {});
}

void Project::SetCharacter(size_t line, int character)
{
    ojson e = *lines[line].entry;
    e["character"] = character;
    Commit(line, std::move(e), {});
}

void Project::SetDisplayDuration(size_t line, const char* lang, std::optional<double> seconds)
{
    ojson e = *lines[line].entry;
    if (!e.contains(lang) || !e[lang].is_object())
        return;
    if (seconds)
        e[lang]["displayDuration"] = std::round(*seconds * 1000.0) / 1000.0;
    else
        e[lang].erase("displayDuration");
    Commit(line, std::move(e), std::string("duration:") + lang + ":" + lines[line].id);
}

bool Project::Undo()
{
    if (!CanUndo())
        return false;
    const Edit& edit = m_undo[--m_undoPos];
    for (auto it = edit.changes.rbegin(); it != edit.changes.rend(); ++it)
    {
        *lines[it->line].entry = it->before;
        m_changedLines.push_back(it->line);
    }
    m_undo[m_undoPos].mergeKey.clear();  // never merge into an undone step
    Touch();
    return true;
}

bool Project::Redo()
{
    if (!CanRedo())
        return false;
    const Edit& edit = m_undo[m_undoPos++];
    for (const Change& c : edit.changes)
    {
        *lines[c.line].entry = c.after;
        m_changedLines.push_back(c.line);
    }
    Touch();
    return true;
}
