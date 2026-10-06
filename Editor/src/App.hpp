#pragma once

#include "Analysis.hpp"
#include "Preview.hpp"
#include "Project.hpp"
#include "Settings.hpp"
#include "XactAudio.hpp"

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct SDL_Window;

class App
{
public:
    struct Options
    {
        std::string dataDir, gameDir, select;
        bool noSaveSettings = false;
    };
    bool Init(SDL_Window* window, const char* glslVersion, const Options& options);
    // Saves the preview to path and the window back buffer to <path>_ui.png.
    void SaveScreenshots(const std::string& path, int windowW, int windowH);
    void Shutdown();
    // Draws one frame of UI. Returns false when the app should exit.
    bool Frame();
    void RequestQuit() { m_quitRequested = true; }

    // Font for the editor UI (called before the first frame).
    void SetupUiFonts(float dpiScale);

private:
    // Data
    void LoadProject();
    bool Save();
    // Writes subtitles.json to out (empty = <data>/subtitles.json); install also copies it to the game.
    void Export(bool install, const std::string& out = {});
    void ReloadGameAssets();
    void ApplySettingsChange();

    // UI
    void DrawMenuBar();
    void DrawHeader();
    void DrawList();
    void DrawDetail();
    void DrawPreview();
    void RenderPreview(float rasterScale);
    void DrawBoxSettings();
    void DrawSettings();
    void DrawQuitModal();
    void HandleShortcuts();

    // Helpers
    void RebuildFilter();
    void Select(size_t line, bool focusText);
    void SelectRelative(int delta);
    void SelectNext(Status wanted);
    void PlayAudio(size_t line, const AudioLang& lang);
    void SetStatusMessage(std::string msg, bool error = false);
    std::string CharacterLabel(int id) const;
    void PollDialogs();

    SDL_Window* m_window = nullptr;
    Options  m_options;
    Settings m_settings;
    Project  m_project;
    Preview  m_preview;
    Analysis m_analysis;
    XactBank m_bank;
    AudioPlayer m_player;
    std::string m_bankError;

    uint64_t m_configRevision = 1;
    uint64_t m_analysisConfigRevision = 1;

    // Selection / filtering
    size_t m_selected = SIZE_MAX;
    std::vector<size_t> m_filtered;
    bool m_filterDirty = true;
    uint64_t m_filterProjectRevision = ~0ull;
    bool m_scrollToSelected = false;
    bool m_focusText = false;
    std::string m_search;
    int m_filterCategory = -1;   // -1 all
    int m_filterStatus = -1;     // -1 all
    int m_filterCharacter = INT32_MIN;
    bool m_filterWarnings = false;

    std::string m_textBuffer;
    std::string m_characterSearch;

    // Status bar
    std::string m_status;
    bool   m_statusError = false;
    double m_statusTime = 0;

    // Autosave
    double m_lastAutosave = 0;

    // Quit
    bool m_quitRequested = false;
    bool m_quit = false;

    // Async SDL file dialogs
    enum class DialogTarget { None, DataDir, GameDir, PortraitsDir, SavePng, ExportSubtitles };
    std::mutex m_dialogMutex;
    DialogTarget m_dialogTarget = DialogTarget::None;
    std::string  m_dialogResult;
    std::string  m_dialogDefault;  // default location, alive while the dialog is open
    bool         m_dialogDone = false;
    void OpenDialog(DialogTarget target);
    static void DialogCallback(void* userdata, const char* const* filelist, int filter);

    // Settings edit buffers
    std::string m_editDataDir, m_editGameDir, m_editPortraitsDir;
};
