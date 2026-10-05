// editor_tests <repo data dir> <display_text_cases.json> <output dir>
//
// - Every lines/*.json is loaded (other JSON files are skipped); saving without edits is byte-identical.
// - SubtitleText::DisplayText matches the shared cases (also run by tools/lines/test_lines.py).
// - Edits, automatic status, undo/redo and key order.
// - Writes an edited copy of the data to <out>/rt and the editor export to
//   <out>/editor_subtitles.json for compare_export.cmake.

#include "Exporter.hpp"
#include "Project.hpp"
#include "Paths.hpp"
#include "../../shared/SubtitleText.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

static int g_failures = 0;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++g_failures; } } while (0)

static std::string Slurp(const fs::path& p)
{
    std::string s;
    ReadFile(U8String(p), s);
    return s;
}

static size_t FindLine(const Project& p, const std::string& id)
{
    for (size_t i = 0; i < p.lines.size(); ++i)
        if (p.lines[i].id == id)
            return i;
    return SIZE_MAX;
}

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::printf("usage: editor_tests <data dir> <cases.json> <out dir>\n");
        return 2;
    }
    const fs::path data = U8Path(argv[1]);
    const fs::path out = U8Path(argv[3]);
    const fs::path rt = out / "rt";
    std::error_code ec;
    fs::remove_all(out, ec);
    fs::create_directories(rt / "lines");
    std::vector<std::string> lineFiles;
    for (const auto& file : fs::directory_iterator(data / "lines"))
        if (file.path().extension() == ".json")
        {
            lineFiles.push_back("lines/" + U8String(file.path().filename()));
            fs::copy_file(file.path(), rt / lineFiles.back());
        }
    for (const char* f : { "characters.json", "dialoguebox.json" })
        if (fs::exists(data / f))
            fs::copy_file(data / f, rt / f);
    const std::string notLines = "{\n  \"version\": 1\n}\n";
    CHECK(WriteFileAtomic(U8String(rt / "lines" / "notes.json"), notLines));

    // --- Display text cases
    {
        std::string text;
        CHECK(ReadFile(argv[2], text));
        ojson cases = ojson::parse(text);
        for (const auto& c : cases)
        {
            const std::string got = SubtitleText::DisplayText(c["translation"].get<std::string>(), c["english"].get<std::string>());
            if (got != c["expected"].get<std::string>())
            {
                std::printf("FAIL display text: got \"%s\", expected \"%s\"\n", got.c_str(), c["expected"].get<std::string>().c_str());
                ++g_failures;
            }
        }
    }

    // --- Round trip
    Project project;
    std::string error;
    CHECK(project.Load(U8String(rt), error));
    CHECK(!project.lines.empty());
    CHECK(project.categories.size() == lineFiles.size());
    CHECK(std::is_sorted(project.categories.begin(), project.categories.end()));
    CHECK(project.skippedFiles == std::vector<std::string>{ "notes.json" });
    CHECK(project.Save(error));
    CHECK(Slurp(rt / "lines" / "notes.json") == notLines);
    for (const std::string& f : lineFiles)
    {
        const bool same = Slurp(data / f) == Slurp(rt / f);
        if (!same)
            std::printf("FAIL round trip changed %s\n", f.c_str());
        CHECK(same);
    }
    // dialoguebox.json round trip
    if (fs::exists(data / "dialoguebox.json"))
    {
        DialogueBoxCore::Config cfg;
        CHECK(DialogueBoxCore::ParseConfig(Slurp(data / "dialoguebox.json"), cfg));
        CHECK(DialogueBoxCore::SerializeConfig(cfg) == Slurp(data / "dialoguebox.json"));
    }

    // --- Edits
    const size_t a = FindLine(project, "80101001");
    const size_t b = FindLine(project, "00010101");
    CHECK(a != SIZE_MAX && b != SIZE_MAX);
    if (a != SIZE_MAX && b != SIZE_MAX)
    {
        const ojson original = *project.lines[a].entry;
        project.SetText(a, "  Primera   línea \n\n segunda línea ");
        CHECK(Project::GetStatus(*project.lines[a].entry) == Status::Translated);
        CHECK(Project::DisplayText(*project.lines[a].entry) == "Primera línea\nsegunda línea");
        CHECK(project.Undo());
        CHECK(*project.lines[a].entry == original);
        CHECK(project.Redo());
        CHECK(Project::Text(*project.lines[a].entry) == "  Primera   línea \n\n segunda línea ");

        project.SetStatus(a, Status::Reviewed);
        project.SetText(a, "Primera línea\nsegunda línea");  // edit after review -> translated
        CHECK(Project::GetStatus(*project.lines[a].entry) == Status::Translated);

        project.SetDisplayDuration(a, "ja", 3.25);
        const ojson& ja = (*project.lines[a].entry)["ja"];
        CHECK(Project::EffectiveDuration(*project.lines[a].entry, "ja") == 3.25);
        CHECK(std::prev(ja.end()).key() == "displayDuration");
        project.SetDisplayDuration(a, "ja", 4.0);
        project.SetDisplayDuration(a, "ja", std::nullopt);
        CHECK(!Project::DisplayDurationOverride(*project.lines[a].entry, "ja"));
        project.SetDisplayDuration(a, "en", 5.5);

        project.SetCharacter(b, 3);
        project.SetText(b, "¡Ahí!");
        project.SetText(b, "");
        CHECK(Project::GetStatus(*project.lines[b].entry) == Status::Pending);
        project.SetTextMany({ b }, "¡Ahí está!");
        CHECK(Project::GetStatus(*project.lines[b].entry) == Status::Translated);

        CHECK(project.Save(error));
        Project reloaded;
        CHECK(reloaded.Load(U8String(rt), error));
        const size_t ra = FindLine(reloaded, "80101001");
        CHECK(ra != SIZE_MAX && Project::Text(*reloaded.lines[ra].entry) == "Primera línea\nsegunda línea");
        CHECK(ra != SIZE_MAX && *Project::DisplayDurationOverride(*reloaded.lines[ra].entry, "en") == 5.5);
    }

    // --- Export for compare_export.cmake
    size_t count = 0;
    CHECK(WriteFileAtomic(U8String(out / "editor_subtitles.json"), BuildSubtitlesJson(project, &count)));
    CHECK(count > 0);

    std::printf("%s (%d failures)\n", g_failures ? "FAILED" : "OK", g_failures);
    return g_failures ? 1 : 0;
}
