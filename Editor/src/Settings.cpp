#include "Settings.hpp"
#include "../../shared/json.hpp"
#include "Paths.hpp"

#include <SDL3/SDL.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;

std::string Settings::FilePath()
{
    char* pref = SDL_GetPrefPath("HyperSubtitles", "Editor");
    if (!pref)
        return "editor_settings.json";
    std::string path = std::string(pref) + "settings.json";
    SDL_free(pref);
    return path;
}

std::string Settings::PortraitsDir() const
{
    if (!portraitsDir.empty())
        return portraitsDir;
    std::error_code ec;
    if (!dataDir.empty())
    {
        const fs::path repoFaces = (U8Path(dataDir) / ".." / "Dll1" / "faces").lexically_normal();
        if (fs::is_directory(repoFaces, ec))
            return U8String(repoFaces);
    }
    // A translation team's data folder can live anywhere: fall back to the
    // repo next to the editor executable.
    if (const char* base = SDL_GetBasePath())
        for (fs::path dir = U8Path(base); !dir.empty(); dir = dir.parent_path())
        {
            if (fs::is_directory(dir / "Dll1" / "faces", ec))
                return U8String(dir / "Dll1" / "faces");
            if (fs::is_directory(dir / "faces", ec))
                return U8String(dir / "faces");
            if (dir == dir.parent_path())
                break;
        }
    return {};
}

template <typename T>
static void Read(const json& j, const char* key, T& out)
{
    if (j.contains(key))
    {
        try { out = j[key].get<T>(); } catch (...) {}
    }
}

bool Settings::Load()
{
    std::ifstream file(U8Path(FilePath()), std::ios::binary);
    if (!file)
        return false;
    std::stringstream ss;
    ss << file.rdbuf();
    json j = json::parse(ss.str(), nullptr, false);
    if (!j.is_object())
        return false;

    Read(j, "dataDir", dataDir);
    Read(j, "gameDir", gameDir);
    Read(j, "portraitsDir", portraitsDir);
    Read(j, "uiFontPath", uiFontPath);
    Read(j, "uiFontSize", uiFontSize);
    Read(j, "previewWidth", previewWidth);
    Read(j, "previewHeight", previewHeight);
    Read(j, "previewZoom", previewZoom);
    Read(j, "useBackgroundImage", useBackgroundImage);
    Read(j, "backgroundImage", backgroundImage);
    if (j.contains("backgroundColor") && j["backgroundColor"].is_array() && j["backgroundColor"].size() == 3)
        for (int i = 0; i < 3; ++i)
            if (j["backgroundColor"][i].is_number())
                backgroundColor[i] = j["backgroundColor"][i].get<float>();
    Read(j, "autosaveSeconds", autosaveSeconds);
    Read(j, "maxCharsPerSecond", maxCharsPerSecond);
    Read(j, "volume", volume);
    Read(j, "showBoxSettings", showBoxSettings);
    return true;
}

bool Settings::Save() const
{
    json j;
    j["dataDir"] = dataDir;
    j["gameDir"] = gameDir;
    j["portraitsDir"] = portraitsDir;
    j["uiFontPath"] = uiFontPath;
    j["uiFontSize"] = uiFontSize;
    j["previewWidth"] = previewWidth;
    j["previewHeight"] = previewHeight;
    j["previewZoom"] = previewZoom;
    j["useBackgroundImage"] = useBackgroundImage;
    j["backgroundImage"] = backgroundImage;
    j["backgroundColor"] = { backgroundColor[0], backgroundColor[1], backgroundColor[2] };
    j["autosaveSeconds"] = autosaveSeconds;
    j["maxCharsPerSecond"] = maxCharsPerSecond;
    j["volume"] = volume;
    j["showBoxSettings"] = showBoxSettings;

    std::ofstream file(U8Path(FilePath()), std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file << j.dump(2) << "\n";
    return static_cast<bool>(file);
}

std::string FindDefaultDataDir()
{
    std::vector<fs::path> starts;
    std::error_code ec;
    starts.push_back(fs::current_path(ec));
    if (const char* base = SDL_GetBasePath())
        starts.push_back(U8Path(base));

    for (fs::path dir : starts)
    {
        for (int up = 0; up < 6 && !dir.empty(); ++up)
        {
            if (fs::exists(dir / "data" / "lines" / "event.json", ec))
                return U8String((dir / "data").lexically_normal());
            if (fs::exists(dir / "lines" / "event.json", ec))
                return U8String(dir.lexically_normal());
            fs::path parent = dir.parent_path();
            if (parent == dir)
                break;
            dir = parent;
        }
    }
    return {};
}
