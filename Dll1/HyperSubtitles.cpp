#include "HyperSubtitles.hpp"


#include <filesystem>

#include "json.hpp"

#include "../minhook/include/MinHook.h"
#include "../imgui/imgui.h"
#include "ImGuiRenderer.hpp"
#include "DialogueBox.hpp"
#include "Logger.hpp"
#include "Rebirth_offsets.hpp"

using json = nlohmann::json;

std::vector<Subtitle>* g_subtitles;
Voice_Play_t original_Voice_Play;

void LoadSubtitles(const std::string& filePath, std::vector<Subtitle>& subtitles)
{
    std::ifstream file(filePath);
    json data = json::parse(file);

    for (const auto& sub : data["subtitles"]) {
        Subtitle s;
        s.character = sub["character"];
        s.text      = sub["text"];
        s.duration  = sub["duration"];
        s.audioFile = sub["audioFile"];
        subtitles.push_back(s);
    }
    Logger::log("Loaded %d subtitles", subtitles.size());
}

void ShowSubtitle(const char* text, float seconds, int character)
{
    DialogueBox::Show(text, static_cast<float>(ImGui::GetTime()) + seconds, character);
}

int __cdecl HookedVoicePlay(int a1, char* Source, __int64 a3, int a4, int a5, int a6, int a7, int a8)
{
    Logger::log("Voice_Play called with audio file: %s", Source);
    if (g_subtitles) {
        std::string audioFile(Source);
        for (const auto& sub : *g_subtitles) {
            if (sub.audioFile == audioFile) {
                ShowSubtitle(sub.text.c_str(), sub.duration, sub.character);
                break;
            }
        }
    }

    return original_Voice_Play(a1, Source, a3, a4, a5, a6, a7, a8);
}

void RenderImGuiContent()
{    
    DialogueBox::Render();
#ifdef _DEBUG
    DialogueBox::DrawDebugWindow();
#endif
}

bool SetupHooks(DWORD entryPoint, uintptr_t base)
{
    if (!ImGuiRenderer::Setup()) {
        Logger::log("ImGuiRenderer::Setup failed");
        return FALSE;
    }

    if (std::filesystem::exists("./subtitles.json")) {
        Logger::log("Loading subtitles from subtitles.json");
        g_subtitles = new std::vector<Subtitle>();
        LoadSubtitles("./subtitles.json", *g_subtitles);
    }

    DialogueBox::Config cfg;
    cfg.x = 600.0f;
    cfg.y = 1000.0f;
    cfg.width = 1200.0f;
    cfg.portraitHeight = 74;
    cfg.fontSize = 32.0f;
    DialogueBox::SetConfig(cfg);

	switch (entryPoint) {
        case RB1_STEAM_ENTRYPOINT:
			Logger::log("Detected Neptunia Rebirth 1 (Steam)");
			TRY_HOOK((void*)(base + RB1_STEAM_FUNC_ADDR), &HookedVoicePlay, &original_Voice_Play, "Failed to hook Voice_Play (alternative address)");
            break;
        default:
            Logger::log("Unknown game version, using default config");
            break;
    }

    Logger::log("ImGui renderer hooked (OpenGL2)");
    return TRUE;
}
