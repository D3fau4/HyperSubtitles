#pragma once

#include <string>

// Per-user editor settings, stored as JSON in the SDL pref path.
struct Settings
{
    std::string dataDir;        // folder with lines/, characters.json and dialoguebox.json
    std::string gameDir;        // game install: font, data/SOUND.xsb, data/SOUND.xwb
    std::string portraitsDir;   // PNG portraits (<id>.png); empty = <dataDir>/../Dll1/faces
    std::string uiFontPath;     // optional font for the editor UI; empty = default + game font
    float       uiFontSize = 20.0f;

    int   previewWidth  = 1920;
    int   previewHeight = 1080;
    float previewScale  = -1.0f; // -1 = fit the dialogue box, 0 = fit the whole screen, >0 = fixed zoom
    bool  useBackgroundImage = false;
    std::string backgroundImage;
    float backgroundColor[3] = { 0.10f, 0.10f, 0.12f };

    int   autosaveSeconds = 60;  // 0 = off
    float maxCharsPerSecond = 20.0f;
    float volume = 1.0f;

    bool showBoxSettings = false;
    bool showSettings = false;

    bool Load();
    bool Save() const;

    std::string PortraitsDir() const;
    static std::string FilePath();
};

// Looks for a folder containing lines/event.json near the executable and the
// working directory (the repo's data/ folder). Empty if not found.
std::string FindDefaultDataDir();
