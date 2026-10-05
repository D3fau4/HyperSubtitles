#pragma once

#include "../../shared/json.hpp"
#include "../../shared/DialogueBoxCore.hpp"

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using ojson = nlohmann::ordered_json;

enum class Status { Pending, Translated, Reviewed };
const char* StatusKey(Status s);     // "pending", ...
Status      ParseStatus(const std::string& s);

// Audio languages, in the order lines.py LANGUAGES uses them.
struct AudioLang { const char* key; const char* folder; const char* cueSuffix; const char* label; };
inline constexpr AudioLang kAudioLangs[2] = {
    { "ja", "VOICE",    "",  "JA" },
    { "en", "VOICE_EN", "e", "EN" },
};

struct Character
{
    int         id = -1;
    std::string name;
    std::string portrait;      // "face/001"
    int         portraitOwner = -1;  // lowest character id with the same portrait = PNG file name
};

struct Line
{
    int         category = 0;  // index in Project::categories
    std::string id;
    ojson*      entry = nullptr;
};

// The line database, characters and box config. Every data/lines/*.json whose
// values are all line entries is a category named after the file (battle.json -> BATTLE).
// Saving writes exactly what tools/lines/lines.py save_category() writes.
class Project
{
public:
    bool Load(const std::string& dataDir, std::string& error);
    bool Save(std::string& error);
    bool IsLoaded() const { return m_loaded; }
    const std::string& DataDir() const { return m_dataDir; }

    std::vector<Line>               lines;
    std::vector<std::string>        categories;    // sorted
    std::vector<std::string>        skippedFiles;  // lines/*.json without the line structure
    std::map<int, Character>        characters;
    DialogueBoxCore::Config         boxConfig = DialogueBoxCore::DefaultGameConfig();
    bool                            boxConfigDirty = false;
    bool                            SaveBoxConfig(std::string& error);
    std::string                     BoxConfigPath() const;

    // Read helpers
    static std::string   Text(const ojson& e);                       // translation
    static std::string   SourceText(const ojson& e, const char* lang); // ja / en text
    static bool          HasAudio(const ojson& e, const char* lang);
    static double        AudioDuration(const ojson& e, const char* lang);   // 0 if none
    static std::optional<double> DisplayDurationOverride(const ojson& e, const char* lang);
    static double        EffectiveDuration(const ojson& e, const char* lang);
    static Status        GetStatus(const ojson& e);
    static int           GetCharacter(const ojson& e);
    static std::string   DisplayText(const ojson& e);                // what the game shows

    const Character* FindCharacter(int id) const;

    // Edits (each one is an undo step; consecutive edits with the same mergeKey on
    // the same line within a short time are merged, e.g. typing)
    void SetText(size_t line, const std::string& text);
    void SetStatus(size_t line, Status s);
    void SetCharacter(size_t line, int character);
    void SetDisplayDuration(size_t line, const char* lang, std::optional<double> seconds);
    // Sets the same translation on several lines as one undo step.
    void SetTextMany(const std::vector<size_t>& lines, const std::string& text);

    bool CanUndo() const { return m_undoPos > 0; }
    bool CanRedo() const { return m_undoPos < m_undo.size(); }
    bool Undo();
    bool Redo();

    bool     IsDirty() const { return m_dirty; }
    double   LastEditTime() const { return m_lastEditTime; }
    // Increments on every change (edits, undo, redo, load).
    uint64_t Revision() const { return m_revision; }
    // Increments on every Load().
    uint64_t LoadGeneration() const { return m_loadGeneration; }
    // Lines changed since the last call (edits, undo, redo).
    std::vector<size_t> TakeChangedLines() { return std::exchange(m_changedLines, {}); }

private:
    struct Change { size_t line; ojson before; ojson after; };
    struct Edit   { std::vector<Change> changes; std::string mergeKey; double time = 0; };

    void Commit(size_t line, ojson after, const std::string& mergeKey);
    void Commit(std::vector<Change> changes, const std::string& mergeKey);
    void Touch();

    bool        m_loaded = false;
    std::string m_dataDir;
    std::vector<std::string> m_fileNames;
    std::vector<ojson> m_files;
    std::vector<Edit> m_undo;
    size_t      m_undoPos = 0;
    bool        m_dirty = false;
    double      m_lastEditTime = 0;
    uint64_t    m_revision = 0;
    uint64_t    m_loadGeneration = 0;
    std::vector<size_t> m_changedLines;
};

double NowSeconds();
bool   ReadFile(const std::string& path, std::string& out);
bool   WriteFileAtomic(const std::string& path, const std::string& data);
