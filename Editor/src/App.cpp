#include "App.hpp"
#include "Exporter.hpp"
#include "Paths.hpp"
#include "../../shared/SubtitleText.hpp"

#include "../../imgui/imgui.h"
#include "../../imgui/misc/cpp/imgui_stdlib.h"
#include "../../imgui/imgui_internal.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace fs = std::filesystem;

// external/fonts/NotoSansJP-Regular.otf, embedded by CMake (cmake/embed_file.cmake)
extern const unsigned char g_notoSansJp[];
extern const unsigned g_notoSansJp_size;

namespace
{
    const ImVec4 kStatusColors[3] = {
        ImVec4(0.86f, 0.36f, 0.36f, 1.0f),  // pending
        ImVec4(0.95f, 0.76f, 0.30f, 1.0f),  // translated
        ImVec4(0.42f, 0.84f, 0.46f, 1.0f),  // reviewed
    };
    const char* kStatusNames[3] = { "Pendiente", "Traducida", "Revisada" };
    const ImVec4 kWarnColor(1.0f, 0.62f, 0.25f, 1.0f);

    struct Resolution { int w, h; const char* label; };
    const Resolution kResolutions[] = {
        { 1280, 720, "1280 x 720" },   { 1366, 768, "1366 x 768" },   { 1600, 900, "1600 x 900" },
        { 1920, 1080, "1920 x 1080" }, { 2560, 1440, "2560 x 1440" }, { 3840, 2160, "3840 x 2160" },
    };

    std::string Lower(std::string s)
    {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        return s;
    }

    bool Contains(const std::string& haystack, const std::string& lowerNeedle)
    {
        return Lower(haystack).find(lowerNeedle) != std::string::npos;
    }

    std::string FirstLine(const std::string& s)
    {
        std::string out = s.substr(0, s.find('\n'));
        if (out.size() != s.size())
            out += " ...";
        return out;
    }

    std::string GameDataDir(const std::string& gameDir)
    {
        if (gameDir.empty())
            return {};
        std::error_code ec;
        if (fs::exists(U8Path(gameDir) / "data" / "SOUND.xsb", ec))
            return U8String(U8Path(gameDir) / "data");
        return gameDir;
    }

    void StatusDot(Status s)
    {
        const float sz = ImGui::GetTextLineHeight() * 0.55f;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float y = p.y + (ImGui::GetTextLineHeight() - sz) * 0.5f;
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, y), ImVec2(p.x + sz, y + sz),
                                                  ImGui::GetColorU32(kStatusColors[int(s)]), sz * 0.25f);
        ImGui::Dummy(ImVec2(sz, ImGui::GetTextLineHeight()));
    }

    void HelpMarker(const char* text)
    {
        ImGui::TextDisabled("(?)");
        if (ImGui::BeginItemTooltip())
        {
            ImGui::PushTextWrapPos(ImGui::GetFontSize() * 30.0f);
            ImGui::TextUnformatted(text);
            ImGui::PopTextWrapPos();
            ImGui::EndTooltip();
        }
    }
}

// ---------------------------------------------------------------------------
// Init / shutdown
// ---------------------------------------------------------------------------

void App::SetupUiFonts(float dpiScale)
{
    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();
    style.FontSizeBase = m_settings.uiFontSize;
    style.FontScaleDpi = dpiScale;

    // UI font: the one chosen in the settings, else a proportional system font.
    static const char* const kSystemFonts[] = {
        "C:/Windows/Fonts/segoeui.ttf",
        "/System/Library/Fonts/SFNS.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
        "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/noto/NotoSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
    };
    std::error_code ec;
    auto exists = [&](const std::string& path) { return !path.empty() && fs::exists(U8Path(path), ec); };

    ImFont* font = nullptr;
    if (exists(m_settings.uiFontPath))
        font = io.Fonts->AddFontFromFileTTF(m_settings.uiFontPath.c_str());
    for (const char* f : kSystemFonts)
        if (!font && exists(f))
            font = io.Fonts->AddFontFromFileTTF(f);
    if (!font)
        font = io.Fonts->AddFontDefaultVector();

    // Japanese (the ja transcriptions): Noto Sans JP embedded in the exe, so it works
    // whatever fonts the PC or the game install have. Only used for glyphs the UI font lacks.
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(g_notoSansJp), int(g_notoSansJp_size), 0.0f, &cfg);
}

bool App::Init(SDL_Window* window, const char* glslVersion, const Options& options)
{
    m_window = window;
    m_options = options;
    m_settings.Load();
    if (!options.dataDir.empty())
        m_settings.dataDir = options.dataDir;
    if (!options.gameDir.empty())
        m_settings.gameDir = options.gameDir;
    if (m_settings.dataDir.empty())
        m_settings.dataDir = FindDefaultDataDir();

    if (!m_preview.Init(glslVersion))
        return false;
    LoadProject();
    ReloadGameAssets();

    m_editDataDir = m_settings.dataDir;
    m_editGameDir = m_settings.gameDir;
    m_editPortraitsDir = m_settings.portraitsDir;
    if (m_settings.gameDir.empty() && options.select.empty())
        m_settings.showSettings = true;
    if (!options.select.empty())
        for (size_t i = 0; i < m_project.lines.size(); ++i)
            if (m_project.lines[i].id == options.select)
                Select(i, false);
    m_lastAutosave = NowSeconds();
    return true;
}

void App::SaveScreenshots(const std::string& path, int windowW, int windowH)
{
    std::string error;
    RenderPreview(1.0f);
    if (!m_preview.SavePng(path, error))
        SDL_Log("%s", error.c_str());

    std::vector<unsigned char> pixels(size_t(windowW) * windowH * 4);
    gl::PixelStorei(gl::PACK_ALIGNMENT, 1);
    gl::ReadPixels(0, 0, windowW, windowH, gl::RGBA, gl::UNSIGNED_BYTE, pixels.data());
    std::string ui = path;
    const size_t dot = ui.rfind('.');
    ui.insert(dot == std::string::npos ? ui.size() : dot, "_ui");
    if (!SavePngFile(ui, pixels.data(), windowW, windowH, true, error))
        SDL_Log("%s", error.c_str());
}

void App::Shutdown()
{
    m_player.Stop();
    m_bank.Close();
    m_preview.Shutdown();
    if (!m_options.noSaveSettings)
        m_settings.Save();
}

void App::LoadProject()
{
    m_selected = SIZE_MAX;
    m_filterDirty = true;
    if (m_settings.dataDir.empty())
    {
        SetStatusMessage("Elige la carpeta de datos (data/) en Ajustes.", true);
        return;
    }
    std::string error;
    if (!m_project.Load(m_settings.dataDir, error))
    {
        SetStatusMessage("No se pudo cargar el proyecto: " + error, true);
        return;
    }
    ++m_configRevision;
    m_filterCategory = -1;
    m_preview.LoadPortraits(m_settings.PortraitsDir(), m_project);
    std::string message = "Cargadas " + std::to_string(m_project.lines.size()) + " líneas de " + m_settings.dataDir;
    if (!m_project.skippedFiles.empty())
    {
        message += " (ignorados, no son de líneas:";
        for (const std::string& f : m_project.skippedFiles)
            message += " " + f;
        message += ")";
    }
    SetStatusMessage(message);
}

void App::ReloadGameAssets()
{
    m_player.Stop();
    m_preview.SetFont(m_settings.gameDir.empty() ? std::string()
                                                 : U8String(U8Path(m_settings.gameDir) / DialogueBoxCore::kFontFile));
    m_bankError.clear();
    m_bank.Close();
    if (!m_settings.gameDir.empty())
        m_bank.Open(GameDataDir(m_settings.gameDir), m_bankError);
    else
        m_bankError = "carpeta del juego sin configurar";

    std::string error;
    if (m_settings.useBackgroundImage && !m_settings.backgroundImage.empty() &&
        !m_preview.SetBackgroundImage(m_settings.backgroundImage, error))
        SetStatusMessage(error, true);
    ++m_configRevision;
}

void App::ApplySettingsChange()
{
    m_settings.Save();
    ++m_configRevision;
}

bool App::Save()
{
    std::string error;
    if (!m_project.Save(error))
    {
        SetStatusMessage("Error al guardar: " + error, true);
        return false;
    }
    m_lastAutosave = NowSeconds();
    SetStatusMessage("Guardado.");
    return true;
}

void App::Export(bool install)
{
    if (!m_project.IsLoaded())
        return;
    size_t count = 0;
    const std::string json = BuildSubtitlesJson(m_project, &count);
    const std::string out = U8String(U8Path(m_project.DataDir()) / "subtitles.json");
    if (!WriteFileAtomic(out, json))
    {
        SetStatusMessage("No se pudo escribir " + out, true);
        return;
    }
    std::string msg = "Exportados " + std::to_string(count) + " subtítulos a " + out;
    if (install)
    {
        if (m_settings.gameDir.empty())
        {
            SetStatusMessage(msg + ". Configura la carpeta del juego para instalarlos.", true);
            return;
        }
        const fs::path game = U8Path(m_settings.gameDir);
        if (!WriteFileAtomic(U8String(game / "subtitles.json"), json) ||
            !WriteFileAtomic(U8String(game / "dialoguebox.json"), DialogueBoxCore::SerializeConfig(m_project.boxConfig)))
        {
            SetStatusMessage(msg + ". No se pudo copiar a la carpeta del juego.", true);
            return;
        }
        msg += " e instalados en la carpeta del juego (subtitles.json + dialoguebox.json)";
    }
    SetStatusMessage(msg + ".");
}

void App::SetStatusMessage(std::string msg, bool error)
{
    m_status = std::move(msg);
    m_statusError = error;
    m_statusTime = NowSeconds();
    SDL_Log("%s", m_status.c_str());
}

std::string App::CharacterLabel(int id) const
{
    if (const Character* ch = m_project.FindCharacter(id))
        return ch->name.empty() ? "#" + std::to_string(id) : ch->name;
    return id < 0 ? "(sin personaje)" : "#" + std::to_string(id);
}

// ---------------------------------------------------------------------------
// File dialogs
// ---------------------------------------------------------------------------

void App::DialogCallback(void* userdata, const char* const* filelist, int)
{
    App* app = static_cast<App*>(userdata);
    std::lock_guard<std::mutex> lock(app->m_dialogMutex);
    app->m_dialogResult = filelist && filelist[0] ? filelist[0] : "";
    app->m_dialogDone = true;
}

void App::OpenDialog(DialogTarget target)
{
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        if (m_dialogTarget != DialogTarget::None)
            return;
        m_dialogTarget = target;
        m_dialogDone = false;
    }
    static const SDL_DialogFileFilter images[] = { { "Imágenes", "png;jpg;jpeg;bmp" } };
    static const SDL_DialogFileFilter png[] = { { "PNG", "png" } };
    switch (target)
    {
    case DialogTarget::DataDir:
        SDL_ShowOpenFolderDialog(DialogCallback, this, m_window, m_settings.dataDir.empty() ? nullptr : m_settings.dataDir.c_str(), false);
        break;
    case DialogTarget::GameDir:
        SDL_ShowOpenFolderDialog(DialogCallback, this, m_window, m_settings.gameDir.empty() ? nullptr : m_settings.gameDir.c_str(), false);
        break;
    case DialogTarget::PortraitsDir:
        SDL_ShowOpenFolderDialog(DialogCallback, this, m_window, nullptr, false);
        break;
    case DialogTarget::Background:
        SDL_ShowOpenFileDialog(DialogCallback, this, m_window, images, 1, nullptr, false);
        break;
    case DialogTarget::SavePng:
        SDL_ShowSaveFileDialog(DialogCallback, this, m_window, png, 1, "preview.png");
        break;
    default:
        break;
    }
}

void App::PollDialogs()
{
    DialogTarget target;
    std::string result;
    {
        std::lock_guard<std::mutex> lock(m_dialogMutex);
        if (!m_dialogDone)
            return;
        target = m_dialogTarget;
        result = m_dialogResult;
        m_dialogTarget = DialogTarget::None;
        m_dialogDone = false;
    }
    if (result.empty())
        return;
    switch (target)
    {
    case DialogTarget::DataDir:      m_editDataDir = result; break;
    case DialogTarget::GameDir:      m_editGameDir = result; break;
    case DialogTarget::PortraitsDir: m_editPortraitsDir = result; break;
    case DialogTarget::Background:
    {
        std::string error;
        if (m_preview.SetBackgroundImage(result, error))
        {
            m_settings.backgroundImage = result;
            m_settings.useBackgroundImage = true;
            m_settings.Save();
        }
        else
            SetStatusMessage(error, true);
        break;
    }
    case DialogTarget::SavePng:
    {
        if (fs::path(U8Path(result)).extension().empty())
            result += ".png";
        std::string error;
        RenderPreview(1.0f);  // the exact in-game pixels
        if (m_preview.SavePng(result, error))
            SetStatusMessage("Preview guardada en " + result);
        else
            SetStatusMessage(error, true);
        break;
    }
    default:
        break;
    }
}

// ---------------------------------------------------------------------------
// Selection & filter
// ---------------------------------------------------------------------------

void App::RebuildFilter()
{
    m_filtered.clear();
    const std::string needle = Lower(m_search);
    for (size_t i = 0; i < m_project.lines.size(); ++i)
    {
        const Line& line = m_project.lines[i];
        const ojson& e = *line.entry;
        if (m_filterCategory >= 0 && line.category != m_filterCategory)
            continue;
        if (m_filterStatus >= 0 && int(Project::GetStatus(e)) != m_filterStatus)
            continue;
        if (m_filterCharacter != INT32_MIN && Project::GetCharacter(e) != m_filterCharacter)
            continue;
        if (m_filterWarnings && !m_analysis.Warnings(i).Any())
            continue;
        if (!needle.empty() && line.id.find(needle) == std::string::npos &&
            !Contains(Project::SourceText(e, "en"), needle) && !Contains(Project::SourceText(e, "ja"), needle) &&
            !Contains(Project::Text(e), needle) && !Contains(CharacterLabel(Project::GetCharacter(e)), needle))
            continue;
        m_filtered.push_back(i);
    }
    m_filterDirty = false;
    m_filterProjectRevision = m_project.Revision();
}

void App::Select(size_t line, bool focusText)
{
    if (line >= m_project.lines.size())
        return;
    if (line != m_selected)
    {
        m_player.Stop();
        ImGui::ClearActiveID();
    }
    m_selected = line;
    m_scrollToSelected = true;
    m_focusText = focusText;
}

void App::SelectRelative(int delta)
{
    if (m_filtered.empty())
        return;
    auto it = std::find(m_filtered.begin(), m_filtered.end(), m_selected);
    long pos = it == m_filtered.end() ? (delta > 0 ? -1 : long(m_filtered.size())) : long(it - m_filtered.begin());
    pos = std::clamp(pos + delta, 0L, long(m_filtered.size()) - 1);
    Select(m_filtered[size_t(pos)], m_focusText);
}

void App::SelectNext(Status wanted)
{
    if (m_filtered.empty())
        return;
    auto it = std::find(m_filtered.begin(), m_filtered.end(), m_selected);
    const size_t start = it == m_filtered.end() ? 0 : size_t(it - m_filtered.begin()) + 1;
    for (size_t k = 0; k < m_filtered.size(); ++k)
    {
        const size_t idx = m_filtered[(start + k) % m_filtered.size()];
        if (idx != m_selected && Project::GetStatus(*m_project.lines[idx].entry) == wanted)
        {
            Select(idx, true);
            return;
        }
    }
    SetStatusMessage(std::string("No quedan líneas '") + kStatusNames[int(wanted)] + "' en la lista filtrada.");
}

void App::PlayAudio(size_t line, const AudioLang& lang)
{
    if (line >= m_project.lines.size())
        return;
    const std::string cue = m_project.lines[line].id + lang.cueSuffix;
    const std::string tag = cue;
    if (m_player.IsPlaying() && m_player.Tag() == tag)
    {
        m_player.Stop();
        return;
    }
    Wave wave;
    std::string error;
    if (!m_bank.IsOpen())
    {
        SetStatusMessage("Audio no disponible: " + m_bankError, true);
        return;
    }
    if (!m_bank.Decode(cue, wave, error) || !m_player.Play(wave, m_settings.volume, error))
    {
        SetStatusMessage("No se pudo reproducir " + cue + ": " + error, true);
        return;
    }
    m_player.SetTag(tag);
}

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------

bool App::Frame()
{
    PollDialogs();
    if (m_project.IsLoaded())
    {
        // While a slider is being dragged (box settings) keep the old warnings: recomputing
        // every line each frame would make dragging sluggish.
        if (!ImGui::IsAnyItemActive())
            m_analysisConfigRevision = m_configRevision;
        m_analysis.Update(m_project, m_preview, m_settings, m_analysisConfigRevision);
        if (m_filterDirty || (m_filterProjectRevision != m_project.Revision() && (m_filterStatus >= 0 || m_filterWarnings || !m_search.empty())))
        {
            // Filters on editable fields are refreshed on change, but the selected line
            // stays visible so editing it does not make it vanish.
            const bool editRefresh = !m_filterDirty;
            const size_t keep = m_selected;
            RebuildFilter();
            if (editRefresh && keep != SIZE_MAX && std::find(m_filtered.begin(), m_filtered.end(), keep) == m_filtered.end())
            {
                m_filtered.push_back(keep);
                std::sort(m_filtered.begin(), m_filtered.end());
            }
        }
        if (m_selected == SIZE_MAX && !m_filtered.empty())
            Select(m_filtered.front(), false);

        // Autosave
        if (m_settings.autosaveSeconds > 0 && (m_project.IsDirty() || m_project.boxConfigDirty) &&
            NowSeconds() - m_lastAutosave >= m_settings.autosaveSeconds &&
            NowSeconds() - m_project.LastEditTime() > 2.0)
        {
            std::string error;
            if (m_project.Save(error))
                SetStatusMessage("Autoguardado.");
            else
                SetStatusMessage("Error en el autoguardado: " + error, true);
            m_lastAutosave = NowSeconds();
        }
    }

    HandleShortcuts();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("HyperSubtitles Editor", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleVar();

    DrawMenuBar();
    if (m_project.IsLoaded())
    {
        DrawHeader();
        const float statusH = ImGui::GetFrameHeightWithSpacing();
        if (ImGui::BeginTable("##layout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                              ImVec2(0, ImGui::GetContentRegionAvail().y - statusH)))
        {
            ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthStretch, 0.52f);
            ImGui::TableSetupColumn("edit", ImGuiTableColumnFlags_WidthStretch, 0.48f);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            DrawList();
            ImGui::TableNextColumn();
            DrawDetail();
            DrawPreview();
            ImGui::EndTable();
        }
    }
    else
    {
        ImGui::Spacing();
        ImGui::TextWrapped("No hay proyecto cargado. Abre Archivo > Ajustes y elige la carpeta data/ del repositorio "
                           "(la que contiene lines/ con los .json de líneas, p. ej. lines/battle.json).");
        if (ImGui::Button("Abrir ajustes"))
            m_settings.showSettings = true;
    }

    // Status bar
    ImGui::Separator();
    if (!m_status.empty())
    {
        if (m_statusError)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", m_status.c_str());
        else
            ImGui::TextDisabled("%s", m_status.c_str());
    }
    ImGui::End();

    DrawBoxSettings();
    DrawSettings();
    DrawQuitModal();
    return !m_quit;
}

void App::HandleShortcuts()
{
    const ImGuiInputFlags global = ImGuiInputFlags_RouteGlobal | ImGuiInputFlags_RouteOverFocused;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, global | ImGuiInputFlags_RouteOverActive))
        Save();
    if (!m_project.IsLoaded())
        return;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, global))
    {
        ImGui::ClearActiveID();
        m_project.Undo();
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, global) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, global))
    {
        ImGui::ClearActiveID();
        m_project.Redo();
    }
    const ImGuiInputFlags overActive = global | ImGuiInputFlags_RouteOverActive;
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Enter, overActive) || ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_KeypadEnter, overActive))
    {
        if (m_selected != SIZE_MAX)
        {
            const ojson& e = *m_project.lines[m_selected].entry;
            if (!SubtitleText::CollapseSpaces(Project::Text(e)).empty() && Project::GetStatus(e) == Status::Pending)
                m_project.SetStatus(m_selected, Status::Translated);
        }
        SelectNext(Status::Pending);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Enter, overActive))
    {
        if (m_selected != SIZE_MAX && !SubtitleText::CollapseSpaces(Project::Text(*m_project.lines[m_selected].entry)).empty())
            m_project.SetStatus(m_selected, Status::Reviewed);
        SelectNext(Status::Translated);
    }
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_DownArrow, overActive | ImGuiInputFlags_Repeat))
        SelectRelative(+1);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_UpArrow, overActive | ImGuiInputFlags_Repeat))
        SelectRelative(-1);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_1, overActive))
        PlayAudio(m_selected, kAudioLangs[0]);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_2, overActive))
        PlayAudio(m_selected, kAudioLangs[1]);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Space, overActive) && m_selected != SIZE_MAX)
    {
        const ojson& e = *m_project.lines[m_selected].entry;
        PlayAudio(m_selected, Project::HasAudio(e, "en") ? kAudioLangs[1] : kAudioLangs[0]);
    }
}

// ---------------------------------------------------------------------------
// Menu / header
// ---------------------------------------------------------------------------

void App::DrawMenuBar()
{
    if (!ImGui::BeginMenuBar())
        return;
    if (ImGui::BeginMenu("Archivo"))
    {
        if (ImGui::MenuItem("Guardar", "Ctrl+S", false, m_project.IsLoaded()))
            Save();
        if (ImGui::MenuItem("Recargar desde disco", nullptr, false, m_project.IsLoaded()))
            LoadProject();
        ImGui::Separator();
        if (ImGui::MenuItem("Exportar subtitles.json", nullptr, false, m_project.IsLoaded()))
            Export(false);
        if (ImGui::MenuItem("Exportar e instalar en el juego", nullptr, false, m_project.IsLoaded() && !m_settings.gameDir.empty()))
            Export(true);
        ImGui::SetItemTooltip("Escribe data/subtitles.json y copia subtitles.json y dialoguebox.json\njunto al ejecutable del juego.");
        ImGui::Separator();
        if (ImGui::MenuItem("Ajustes...", nullptr, m_settings.showSettings))
            m_settings.showSettings = !m_settings.showSettings;
        ImGui::Separator();
        if (ImGui::MenuItem("Salir"))
            m_quitRequested = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Editar"))
    {
        if (ImGui::MenuItem("Deshacer", "Ctrl+Z", false, m_project.CanUndo()))
            m_project.Undo();
        if (ImGui::MenuItem("Rehacer", "Ctrl+Y", false, m_project.CanRedo()))
            m_project.Redo();
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ver"))
    {
        if (ImGui::MenuItem("Ajustes de la caja de diálogo", nullptr, m_settings.showBoxSettings))
            m_settings.showBoxSettings = !m_settings.showBoxSettings;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda"))
    {
        ImGui::TextUnformatted(
            "Atajos\n"
            "  Ctrl+Enter          Marcar traducida y saltar a la siguiente pendiente\n"
            "  Ctrl+Shift+Enter    Marcar revisada y saltar a la siguiente traducida\n"
            "  Ctrl+Arriba/Abajo   Línea anterior / siguiente\n"
            "  Ctrl+Espacio        Reproducir / detener la voz (EN, o JA si no hay)\n"
            "  Ctrl+1 / Ctrl+2     Reproducir voz JA / EN\n"
            "  Ctrl+S              Guardar\n"
            "  Ctrl+Z / Ctrl+Y     Deshacer / rehacer (fuera del campo de texto)\n"
            "  Enter               Salto de línea dentro de la traducción");
        ImGui::EndMenu();
    }
    if (m_project.IsDirty() || m_project.boxConfigDirty)
    {
        ImGui::Spacing();
        ImGui::TextColored(kWarnColor, "* sin guardar");
    }
    ImGui::EndMenuBar();
}

void App::DrawHeader()
{
    const int columns = int(m_analysis.counts.size());
    if (columns == int(m_project.categories.size()) + 1 && ImGui::BeginTable("##progress", columns, ImGuiTableFlags_SizingStretchSame))
    {
        ImGui::TableNextRow();
        for (int i = 0; i < columns; ++i)
        {
            ImGui::TableNextColumn();
            const Analysis::Counts& c = m_analysis.counts[i];
            const float total = float(std::max(c.total, 1));
            const char* name = i + 1 < columns ? m_project.categories[i].c_str() : "Total";
            ImGui::Text("%s  %d líneas", name, c.total);
            // Stacked bar: reviewed, translated, pending
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float w = ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x;
            const float h = ImGui::GetTextLineHeight() * 0.7f;
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float x = p.x;
            const int values[3] = { c.reviewed, c.translated, c.pending };
            const int colors[3] = { 2, 1, 0 };
            for (int k = 0; k < 3; ++k)
            {
                const float segW = w * values[k] / total;
                if (segW > 0)
                    dl->AddRectFilled(ImVec2(x, p.y), ImVec2(x + segW, p.y + h), ImGui::GetColorU32(kStatusColors[colors[k]]));
                x += segW;
            }
            ImGui::Dummy(ImVec2(w, h));
            ImGui::TextColored(kStatusColors[2], "%d rev.", c.reviewed);
            ImGui::SameLine();
            ImGui::TextColored(kStatusColors[1], "%d trad.", c.translated);
            ImGui::SameLine();
            ImGui::TextColored(kStatusColors[0], "%d pend.", c.pending);
            ImGui::SameLine();
            ImGui::TextDisabled("(%.1f%%)", 100.0f * (c.reviewed + c.translated) / total);
            if (c.warnings)
            {
                ImGui::SameLine();
                ImGui::TextColored(kWarnColor, "%d avisos", c.warnings);
            }
        }
        ImGui::EndTable();
    }
    ImGui::Separator();
}

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

void App::DrawList()
{
    // Filters
    const float w = ImGui::GetContentRegionAvail().x;
    ImGui::SetNextItemWidth(w * 0.40f);
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_F, ImGuiInputFlags_RouteGlobal))
        ImGui::SetKeyboardFocusHere();
    if (ImGui::InputTextWithHint("##search", "Buscar (id, texto, personaje)  Ctrl+F", &m_search))
        m_filterDirty = true;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.17f);
    const char* catPreview = m_filterCategory >= 0 && m_filterCategory < int(m_project.categories.size())
                                 ? m_project.categories[m_filterCategory].c_str() : "Todas";
    if (ImGui::BeginCombo("##cat", catPreview))
    {
        for (int c = -1; c < int(m_project.categories.size()); ++c)
            if (ImGui::Selectable(c < 0 ? "Todas" : m_project.categories[c].c_str(), m_filterCategory == c))
            {
                m_filterCategory = c;
                m_filterDirty = true;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(w * 0.17f);
    const char* statuses[] = { "Todos", "Pendiente", "Traducida", "Revisada" };
    int st = m_filterStatus + 1;
    if (ImGui::Combo("##status", &st, statuses, 4))
    {
        m_filterStatus = st - 1;
        m_filterDirty = true;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-FLT_MIN);
    const std::string charPreview = m_filterCharacter == INT32_MIN ? std::string("Personaje: todos") : CharacterLabel(m_filterCharacter);
    if (ImGui::BeginCombo("##charfilter", charPreview.c_str(), ImGuiComboFlags_HeightLarge))
    {
        if (ImGui::Selectable("Todos", m_filterCharacter == INT32_MIN))
        {
            m_filterCharacter = INT32_MIN;
            m_filterDirty = true;
        }
        if (ImGui::Selectable("(sin personaje)", m_filterCharacter == -1))
        {
            m_filterCharacter = -1;
            m_filterDirty = true;
        }
        for (const auto& [id, ch] : m_project.characters)
        {
            const std::string label = ch.name + "##" + std::to_string(id);
            if (ImGui::Selectable(label.c_str(), m_filterCharacter == id))
            {
                m_filterCharacter = id;
                m_filterDirty = true;
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Checkbox("Solo con avisos", &m_filterWarnings))
        m_filterDirty = true;
    ImGui::SameLine();
    ImGui::TextDisabled("%zu de %zu líneas", m_filtered.size(), m_project.lines.size());

    const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter |
                                  ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Hideable;
    if (!ImGui::BeginTable("##lines", 7, flags))
        return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("##st", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize, ImGui::GetTextLineHeight() * 0.8f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("00000000").x);
    ImGui::TableSetupColumn("Personaje", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Purple Heart").x);
    ImGui::TableSetupColumn("Inglés", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Traducción", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Audio", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("JA EN").x);
    ImGui::TableSetupColumn("!", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("!").x * 2);
    ImGui::TableHeadersRow();

    // Arrow keys move the selection while the list has focus.
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows) && !ImGui::IsAnyItemActive())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) SelectRelative(+1);
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))   SelectRelative(-1);
    }

    long selectedRow = -1;
    if (m_scrollToSelected)
    {
        auto it = std::find(m_filtered.begin(), m_filtered.end(), m_selected);
        if (it != m_filtered.end())
            selectedRow = long(it - m_filtered.begin());
    }

    ImGuiListClipper clipper;
    clipper.Begin(int(m_filtered.size()));
    if (selectedRow >= 0)
        clipper.IncludeItemByIndex(int(selectedRow));
    while (clipper.Step())
    {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
        {
            const size_t idx = m_filtered[size_t(row)];
            const Line& line = m_project.lines[idx];
            const ojson& e = *line.entry;
            const Status status = Project::GetStatus(e);
            ImGui::TableNextRow();
            ImGui::PushID(int(idx));

            ImGui::TableNextColumn();
            StatusDot(status);

            ImGui::TableNextColumn();
            if (ImGui::Selectable(line.id.c_str(), idx == m_selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                Select(idx, false);
            if (row == selectedRow)
            {
                ImGui::SetScrollHereY(0.5f);
                m_scrollToSelected = false;
            }
            if (ImGui::BeginItemTooltip())
            {
                ImGui::Text("%s/%s  -  %s", m_project.categories[line.category].c_str(), line.id.c_str(), kStatusNames[int(status)]);
                ImGui::EndTooltip();
            }

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(CharacterLabel(Project::GetCharacter(e)).c_str());

            ImGui::TableNextColumn();
            std::string en = Project::SourceText(e, "en");
            if (en.empty())
                ImGui::TextDisabled("%s", FirstLine(Project::SourceText(e, "ja")).c_str());
            else
                ImGui::TextUnformatted(FirstLine(SubtitleText::CollapseSpaces(en)).c_str());

            ImGui::TableNextColumn();
            ImGui::TextUnformatted(FirstLine(Project::Text(e)).c_str());

            ImGui::TableNextColumn();
            const bool ja = Project::HasAudio(e, "ja"), enA = Project::HasAudio(e, "en");
            ImGui::TextColored(ja ? ImVec4(0.7f, 0.8f, 1.0f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 0.6f), "JA");
            ImGui::SameLine(0, ImGui::CalcTextSize(" ").x);
            ImGui::TextColored(enA ? ImVec4(0.7f, 0.8f, 1.0f, 1.0f) : ImVec4(0.4f, 0.4f, 0.4f, 0.6f), "EN");

            ImGui::TableNextColumn();
            if (m_analysis.Warnings(idx).Any())
                ImGui::TextColored(kWarnColor, "!");

            ImGui::PopID();
        }
    }
    if (selectedRow < 0)
        m_scrollToSelected = false;
    ImGui::EndTable();
}

// ---------------------------------------------------------------------------
// Detail
// ---------------------------------------------------------------------------

void App::DrawDetail()
{
    if (!ImGui::BeginChild("##detail", ImVec2(0, ImGui::GetContentRegionAvail().y * 0.52f),
                           ImGuiChildFlags_ResizeY | ImGuiChildFlags_Borders))
    {
        ImGui::EndChild();
        return;
    }
    if (m_selected == SIZE_MAX)
    {
        ImGui::TextDisabled("Selecciona una línea.");
        ImGui::EndChild();
        return;
    }

    const size_t idx = m_selected;
    const Line& line = m_project.lines[idx];
    const ojson& e = *line.entry;
    ImGui::PushID(int(idx));

    // Title + status
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%s / %s", m_project.categories[line.category].c_str(), line.id.c_str());
    ImGui::SameLine(0, ImGui::GetFontSize() * 2);
    const Status status = Project::GetStatus(e);
    for (int s = 0; s < 3; ++s)
    {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_CheckMark, kStatusColors[s]);
        if (ImGui::RadioButton(kStatusNames[s], int(status) == s))
            m_project.SetStatus(idx, Status(s));
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    // Source texts + audio
    for (const AudioLang& lang : { kAudioLangs[1], kAudioLangs[0] })
    {
        const std::string text = Project::SourceText(e, lang.key);
        const bool hasAudio = Project::HasAudio(e, lang.key);
        if (text.empty() && !hasAudio)
            continue;
        ImGui::PushID(lang.key);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(0.7f, 0.8f, 1.0f, 1.0f), "%s", std::string(lang.key) == "en" ? "Inglés" : "Japonés");
        if (hasAudio)
        {
            ImGui::SameLine();
            const bool playing = m_player.IsPlaying() && m_player.Tag() == line.id + lang.cueSuffix;
            ImGui::BeginDisabled(!m_bank.IsOpen() || !m_bank.Has(line.id + lang.cueSuffix));
            if (ImGui::SmallButton(playing ? "Detener" : "Reproducir"))
                PlayAudio(idx, lang);
            ImGui::EndDisabled();
            if (!m_bank.IsOpen())
                ImGui::SetItemTooltip("Audio no disponible: %s", m_bankError.c_str());
            ImGui::SameLine();
            if (playing)
                ImGui::ProgressBar(float(m_player.Position() / std::max(m_player.Length(), 0.001)),
                                   ImVec2(ImGui::GetFontSize() * 8, 0), "");
            else
                ImGui::TextDisabled("%.2f s", Project::AudioDuration(e, lang.key));
        }
        if (!text.empty())
        {
            const auto src = e[lang.key].value("source", std::string());
            if (!src.empty())
            {
                ImGui::SameLine();
                ImGui::TextDisabled("[%s]", src.c_str());
            }
            ImGui::Indent();
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextUnformatted(text.c_str());
            ImGui::PopTextWrapPos();
            ImGui::Unindent();
        }
        ImGui::PopID();
    }
    ImGui::Separator();

    // Translation
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Traducción");
    ImGui::SameLine();
    HelpMarker("Enter inserta un salto de línea, que se respeta en el juego.\n"
               "Ctrl+Enter marca la línea como traducida y salta a la siguiente pendiente.\n"
               "Si se deja vacía, el juego muestra el texto en inglés.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Copiar inglés"))
    {
        ImGui::ClearActiveID();
        m_project.SetText(idx, SubtitleText::CollapseSpaces(Project::SourceText(e, "en")));
        m_focusText = true;
    }
    m_textBuffer = Project::Text(e);
    if (m_focusText)
    {
        ImGui::SetKeyboardFocusHere();
        m_focusText = false;
    }
    if (ImGui::InputTextMultiline("##text", &m_textBuffer, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.5f),
                                  ImGuiInputTextFlags_WordWrap))
        m_project.SetText(idx, m_textBuffer);

    // Character
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Personaje");
    ImGui::SameLine();
    const int character = Project::GetCharacter(e);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16);
    const std::string preview = CharacterLabel(character) + (character >= 0 ? "  (" + std::to_string(character) + ")" : "");
    if (ImGui::BeginCombo("##character", preview.c_str(), ImGuiComboFlags_HeightLargest))
    {
        if (ImGui::IsWindowAppearing())
        {
            ImGui::SetKeyboardFocusHere();
            m_characterSearch.clear();
        }
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##chsearch", "Buscar", &m_characterSearch);
        const std::string needle = Lower(m_characterSearch);
        const float thumbH = ImGui::GetTextLineHeight() * 1.6f;
        auto item = [&](int id, const std::string& name) {
            if (!needle.empty() && !Contains(name, needle) && std::to_string(id).find(needle) == std::string::npos)
                return;
            ImGui::PushID(id);
            const gl::GLuint tex = m_preview.PortraitTexture(id);
            const ImVec2 pos = ImGui::GetCursorPos();
            if (ImGui::Selectable("##sel", id == character, 0, ImVec2(0, thumbH)))
                m_project.SetCharacter(idx, id);
            if (id == character && ImGui::IsWindowAppearing())
                ImGui::SetScrollHereY();
            ImGui::SetCursorPos(pos);
            if (tex)
                ImGui::Image(ImTextureID(tex), ImVec2(thumbH * 2, thumbH));
            else
                ImGui::Dummy(ImVec2(thumbH * 2, thumbH));
            ImGui::SameLine();
            ImGui::AlignTextToFramePadding();
            if (id >= 0)
                ImGui::Text("%s  (%d)", name.c_str(), id);
            else
                ImGui::TextUnformatted(name.c_str());
            ImGui::PopID();
        };
        item(-1, "(sin personaje)");
        for (const auto& [id, ch] : m_project.characters)
            item(id, ch.name);
        ImGui::EndCombo();
    }
    if (!m_preview.HasPortrait(character))
    {
        ImGui::SameLine();
        ImGui::TextDisabled("sin retrato: el juego solo muestra el texto");
    }

    // Durations
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Duración en pantalla");
    ImGui::SameLine();
    HelpMarker("Cuánto tiempo se ve el subtítulo con cada audio. Por defecto es la duración de la voz.");
    if (ImGui::BeginTable("##durations", 4, ImGuiTableFlags_SizingFixedFit))
    {
        for (const AudioLang& lang : kAudioLangs)
        {
            if (!Project::HasAudio(e, lang.key))
                continue;
            ImGui::PushID(lang.key);
            const double audio = Project::AudioDuration(e, lang.key);
            const auto over = Project::DisplayDurationOverride(e, lang.key);
            float value = float(over ? *over : audio);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            ImGui::Text("   Voz %s (%.3f s)", lang.label, audio);
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
            if (ImGui::DragFloat("##dur", &value, 0.01f, 0.3f, 60.0f, "%.3f s", ImGuiSliderFlags_AlwaysClamp))
                m_project.SetDisplayDuration(idx, lang.key, double(value));
            ImGui::TableNextColumn();
            ImGui::BeginDisabled(!over);
            if (ImGui::SmallButton("Restablecer"))
                m_project.SetDisplayDuration(idx, lang.key, std::nullopt);
            ImGui::EndDisabled();
            ImGui::TableNextColumn();
            if (over)
                ImGui::TextDisabled("%+.2f s respecto a la voz", *over - audio);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    // Translation memory
    const std::vector<size_t>& same = m_analysis.SameSource(m_project, idx);
    if (same.size() > 1)
    {
        std::vector<std::pair<std::string, int>> suggestions;
        std::vector<size_t> untranslated;
        for (size_t other : same)
        {
            if (other == idx)
                continue;
            const std::string t = Project::Text(*m_project.lines[other].entry);
            if (SubtitleText::CollapseSpaces(t).empty())
            {
                untranslated.push_back(other);
                continue;
            }
            auto it = std::find_if(suggestions.begin(), suggestions.end(), [&](const auto& s) { return s.first == t; });
            if (it == suggestions.end())
                suggestions.emplace_back(t, 1);
            else
                ++it->second;
        }
        std::sort(suggestions.begin(), suggestions.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        ImGui::Separator();
        ImGui::Text("Memoria de traducción: %zu líneas con el mismo texto en inglés", same.size() - 1);
        const std::string current = Project::Text(e);
        int shown = 0;
        for (const auto& [text, n] : suggestions)
        {
            if (text == current || shown++ >= 5)
                continue;
            ImGui::PushID(shown);
            if (ImGui::SmallButton("Usar"))
            {
                ImGui::ClearActiveID();
                m_project.SetText(idx, text);
            }
            ImGui::SameLine();
            ImGui::TextWrapped("%s  (x%d)", text.c_str(), n);
            ImGui::PopID();
        }
        if (!SubtitleText::CollapseSpaces(current).empty() && !untranslated.empty())
        {
            const std::string label = "Aplicar esta traducción a las " + std::to_string(untranslated.size()) +
                                      " líneas idénticas sin traducir";
            if (ImGui::Button(label.c_str()))
            {
                m_project.SetTextMany(untranslated, current);
                SetStatusMessage("Traducción aplicada a " + std::to_string(untranslated.size()) + " líneas (Ctrl+Z para deshacer).");
            }
        }
    }

    // Warnings
    const LineWarnings& w = m_analysis.Warnings(idx);
    if (w.Any())
    {
        ImGui::Separator();
        ImGui::TextColored(kWarnColor, "Avisos");
        if (!w.unsupported.empty())
            ImGui::BulletText("La fuente del juego no tiene estos caracteres: %s", w.unsupported.c_str());
        if (w.overflow)
            ImGui::BulletText("Demasiado texto: no cabe en %d líneas ni con la fuente al mínimo.", m_project.boxConfig.maxLines);
        else if (w.shrunk)
            ImGui::BulletText("La fuente se reduce para que quepa.");
        else if (w.widened)
            ImGui::BulletText("La caja se ensancha a toda la pantalla para que quepa.");
        if (w.tooFast)
            ImGui::BulletText("%.1f caracteres por segundo (máximo %.0f): alarga la duración o acorta el texto.",
                              w.charsPerSecond, m_settings.maxCharsPerSecond);
    }

    ImGui::PopID();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

void App::RenderPreview(float rasterScale)
{
    std::string text;
    int character = -1;
    if (m_project.IsLoaded() && m_selected != SIZE_MAX)
    {
        const ojson& e = *m_project.lines[m_selected].entry;
        text = Project::DisplayText(e);
        character = Project::GetCharacter(e);
    }
    m_preview.Render(m_project.boxConfig, text, character, m_settings.previewWidth, m_settings.previewHeight,
                     m_settings.backgroundColor, m_settings.useBackgroundImage, rasterScale);
}

void App::DrawPreview()
{
    // Toolbar
    int resIndex = -1;
    for (int i = 0; i < int(std::size(kResolutions)); ++i)
        if (kResolutions[i].w == m_settings.previewWidth && kResolutions[i].h == m_settings.previewHeight)
            resIndex = i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.5f);
    const std::string resLabel = resIndex >= 0 ? kResolutions[resIndex].label
                                               : std::to_string(m_settings.previewWidth) + " x " + std::to_string(m_settings.previewHeight);
    static bool customRes = false;
    if (ImGui::BeginCombo("##res", resLabel.c_str()))
    {
        for (int i = 0; i < int(std::size(kResolutions)); ++i)
            if (ImGui::Selectable(kResolutions[i].label, i == resIndex))
            {
                m_settings.previewWidth = kResolutions[i].w;
                m_settings.previewHeight = kResolutions[i].h;
                customRes = false;
                ApplySettingsChange();
            }
        if (ImGui::Selectable("Personalizada...", customRes))
            customRes = true;
        ImGui::EndCombo();
    }
    ImGui::SetItemTooltip("Resolución del juego. La posición de la caja está en píxeles absolutos.");
    if (customRes)
    {
        ImGui::SameLine();
        int res[2] = { m_settings.previewWidth, m_settings.previewHeight };
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7);
        if (ImGui::InputInt2("##customres", res, ImGuiInputTextFlags_EnterReturnsTrue))
        {
            m_settings.previewWidth = std::clamp(res[0], 320, 7680);
            m_settings.previewHeight = std::clamp(res[1], 240, 4320);
            ApplySettingsChange();
        }
    }

    ImGui::SameLine();
    const float zooms[] = { -1.0f, 0.0f, 0.25f, 0.5f, 1.0f, 2.0f };
    const char* zoomNames[] = { "Caja", "Pantalla completa", "25%", "50%", "100% (exacto)", "200%" };
    int zoomIndex = 0;
    for (int i = 0; i < 6; ++i)
        if (std::abs(zooms[i] - m_settings.previewScale) < 0.001f)
            zoomIndex = i;
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0f);
    if (ImGui::Combo("##zoom", &zoomIndex, zoomNames, 6))
    {
        m_settings.previewScale = zooms[zoomIndex];
        m_settings.Save();
    }
    ImGui::SetItemTooltip("Caja: amplía la caja para que se lea bien.\n100%%: un píxel del juego = un píxel de pantalla (exacto).");

    ImGui::SameLine();
    bool useImage = m_settings.useBackgroundImage && m_preview.HasBackgroundImage();
    if (ImGui::Checkbox("Captura", &useImage))
    {
        if (useImage && !m_preview.HasBackgroundImage())
            OpenDialog(DialogTarget::Background);
        else
        {
            m_settings.useBackgroundImage = useImage;
            m_settings.Save();
        }
    }
    ImGui::SetItemTooltip("Usar una captura del juego como fondo.");
    ImGui::SameLine();
    if (ImGui::SmallButton("Elegir..."))
        OpenDialog(DialogTarget::Background);
    ImGui::SameLine();
    if (ImGui::ColorEdit3("##bg", m_settings.backgroundColor, ImGuiColorEditFlags_NoInputs))
        m_settings.Save();
    ImGui::SetItemTooltip("Color de fondo");
    ImGui::SameLine();
    if (ImGui::SmallButton("Guardar PNG"))
        OpenDialog(DialogTarget::SavePng);
    ImGui::SameLine();
    if (ImGui::SmallButton("Caja..."))
        m_settings.showBoxSettings = !m_settings.showBoxSettings;

    if (!m_preview.FontLoaded())
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f),
                           "No se encuentra %s en la carpeta del juego: la preview NO es exacta.", DialogueBoxCore::kFontFile);

    // Selected line
    std::string text;
    DialogueBoxCore::Layout layout;
    if (m_selected != SIZE_MAX)
    {
        const ojson& e = *m_project.lines[m_selected].entry;
        text = Project::DisplayText(e);
        const DialogueBoxCore::Layout& l = layout = m_preview.Measure(m_project.boxConfig, text, m_settings.previewWidth, m_settings.previewHeight);
        ImGui::TextDisabled("%d línea(s), fuente %.0f px%s%s", l.lines, l.fontSize, l.widened ? ", caja ensanchada" : "",
                            SubtitleText::CollapseSpaces(Project::Text(e)).empty() ? "  -  sin traducir: se muestra el inglés" : "");
    }
    // Show it. 100% = one game pixel per physical screen pixel.
    if (ImGui::BeginChild("##previewimg", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
    {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const float fbScale = ImGui::GetIO().DisplayFramebufferScale.x > 0 ? ImGui::GetIO().DisplayFramebufferScale.x : 1.0f;
        const float pw = float(m_settings.previewWidth), ph = float(m_settings.previewHeight);

        // Part of the frame to show (game pixels): the box plus a margin in "Caja" mode,
        // else the whole frame.
        ImVec2 c0(0, 0), c1(pw, ph);
        const bool boxMode = m_settings.previewScale < 0 && !text.empty();
        if (boxMode)
        {
            const float margin = 24.0f;
            c0 = ImVec2(std::max(0.0f, layout.boxMin.x - margin), std::max(0.0f, layout.boxMin.y - margin));
            c1 = ImVec2(std::min(pw, layout.boxMax.x + margin), std::min(ph, layout.boxMax.y + margin));
        }

        // Screen pixels per game pixel. 100% is exact: the real in-game pixels. Any other
        // zoom renders the same layout directly at the displayed size, so it stays sharp
        // instead of resampling the 1:1 image. Fitted scales are quantized so resizing
        // the panel does not bake a new font size every frame.
        float raster;
        if (m_settings.previewScale > 0)
            raster = m_settings.previewScale;
        else
        {
            const float fit = std::min(avail.x / std::max(c1.x - c0.x, 1.0f), avail.y / std::max(c1.y - c0.y, 1.0f)) * fbScale;
            raster = std::max(0.05f, std::floor(fit * 20.0f) / 20.0f);
        }
        RenderPreview(raster);
        raster = m_preview.RasterScale();  // may be clamped

        // Snap the crop to whole framebuffer pixels and show it 1:1 on screen.
        const float fw = float(m_preview.FramebufferWidth()), fh = float(m_preview.FramebufferHeight());
        const ImVec2 f0(std::floor(c0.x * raster), std::floor(c0.y * raster));
        const ImVec2 f1(std::min(fw, std::ceil(c1.x * raster)), std::min(fh, std::ceil(c1.y * raster)));
        const ImVec2 size((f1.x - f0.x) / fbScale, (f1.y - f0.y) / fbScale);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        if (boxMode)  // center the box horizontally
            pos.x += std::max(0.0f, (avail.x - size.x) * 0.5f);
        pos = ImVec2(std::floor(pos.x * fbScale) / fbScale, std::floor(pos.y * fbScale) / fbScale);
        ImGui::SetCursorScreenPos(pos);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerNearest, nullptr);
        // The framebuffer texture is bottom-up: flip V.
        ImGui::Image(ImTextureID(m_preview.Texture()), size,
                     ImVec2(f0.x / fw, 1.0f - f0.y / fh), ImVec2(f1.x / fw, 1.0f - f1.y / fh));
        dl->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerLinear, nullptr);
    }
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// Windows
// ---------------------------------------------------------------------------

void App::DrawBoxSettings()
{
    if (!m_settings.showBoxSettings || !m_project.IsLoaded())
        return;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 26, ImGui::GetFontSize() * 40), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Caja de diálogo (dialoguebox.json)", &m_settings.showBoxSettings))
    {
        ImGui::End();
        return;
    }
    DialogueBoxCore::Config& c = m_project.boxConfig;
    const DialogueBoxCore::Config before = c;
    const float dw = float(m_settings.previewWidth), dh = float(m_settings.previewHeight);

    ImGui::TextWrapped("Afecta a todos los idiomas. Se guarda en %s y se instala junto al juego al exportar.",
                       m_project.BoxConfigPath().c_str());
    ImGui::SeparatorText("Posición (px, -1 = automática)");
    ImGui::DragFloat("X", &c.x, 1.0f, -1.0f, dw);
    ImGui::DragFloat("Y", &c.y, 1.0f, -1.0f, dh);
    ImGui::DragFloat("Ancho", &c.width, 1.0f, -1.0f, dw);
    ImGui::SeparatorText("Retrato");
    ImGui::DragFloat("Alto del retrato", &c.portraitHeight, 1.0f, 32.0f, 512.0f);
    ImGui::DragFloat("Proporción", &c.portraitAspect, 0.01f, 0.5f, 4.0f);
    ImGui::SeparatorText("Márgenes");
    ImGui::DragFloat("Izquierda", &c.paddingLeft, 0.5f, 0.0f, 128.0f);
    ImGui::DragFloat("Derecha", &c.paddingRight, 0.5f, 0.0f, 128.0f);
    ImGui::DragFloat("Arriba", &c.paddingTop, 0.5f, 0.0f, 128.0f);
    ImGui::DragFloat("Abajo", &c.paddingBottom, 0.5f, 0.0f, 128.0f);
    ImGui::DragFloat("Retrato <-> texto", &c.paddingInner, 0.5f, 0.0f, 128.0f);
    ImGui::SeparatorText("Texto");
    ImGui::DragFloat("Tamaño de fuente", &c.fontSize, 0.5f, 8.0f, 96.0f);
    ImGui::SliderInt("Líneas máximas", &c.maxLines, 1, 6);
    ImGui::SliderFloat("Escala mínima", &c.minFontScale, 0.5f, 1.0f);
    ImGui::DragFloat("Desplazamiento X", &c.textXOffset, 0.5f, -128.0f, 128.0f);
    ImGui::DragFloat("Desplazamiento Y", &c.textYOffset, 0.5f, -128.0f, 128.0f);
    ImGui::SeparatorText("Aspecto");
    ImGui::SliderFloat("Opacidad", &c.opacity, 0.0f, 1.0f);
    ImGui::DragFloat("Redondeo", &c.rounding, 0.5f, 0.0f, 32.0f);
    ImGui::DragFloat("Grosor del borde", &c.borderThickness, 0.1f, 0.0f, 10.0f);
    ImGui::ColorEdit4("Fondo", c.bgColor);
    ImGui::ColorEdit4("Borde", c.borderColor);
    ImGui::ColorEdit4("Texto", c.textColor);

    if (std::memcmp(&before, &c, sizeof c) != 0)
    {
        m_project.boxConfigDirty = true;
        ++m_configRevision;
    }

    ImGui::Separator();
    if (ImGui::Button("Guardar"))
    {
        std::string error;
        if (m_project.SaveBoxConfig(error))
            SetStatusMessage("dialoguebox.json guardado.");
        else
            SetStatusMessage(error, true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Valores por defecto"))
    {
        c = DialogueBoxCore::DefaultGameConfig();
        m_project.boxConfigDirty = true;
        ++m_configRevision;
    }
    ImGui::End();
}

void App::DrawSettings()
{
    if (!m_settings.showSettings)
        return;
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetFontSize() * 40, 0), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Ajustes", &m_settings.showSettings))
    {
        ImGui::End();
        return;
    }
    auto pathField = [&](const char* label, const char* help, std::string& value, DialogTarget target) {
        ImGui::PushID(label);
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        HelpMarker(help);
        ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 6);
        ImGui::InputText("##path", &value);
        ImGui::SameLine();
        if (ImGui::Button("Examinar..."))
            OpenDialog(target);
        ImGui::PopID();
    };

    pathField("Carpeta de datos", "La carpeta data/ con lines/*.json (cada archivo válido es una categoría), characters.json y dialoguebox.json.\n"
              "Cada equipo de traducción tiene su propia copia.", m_editDataDir, DialogTarget::DataDir);
    pathField("Carpeta del juego", "Donde está instalado el juego. Se usa para la fuente FOT-NewRodinPro-EB.otf, "
              "las voces (data/SOUND.xsb y data/SOUND.xwb) y para instalar los subtítulos.", m_editGameDir, DialogTarget::GameDir);
    pathField("Carpeta de retratos", "PNG de los retratos (<id>.png). Vacío = Dll1/faces del repositorio.",
              m_editPortraitsDir, DialogTarget::PortraitsDir);

    const bool changed = m_editDataDir != m_settings.dataDir || m_editGameDir != m_settings.gameDir ||
                         m_editPortraitsDir != m_settings.portraitsDir;
    ImGui::BeginDisabled(!changed);
    if (ImGui::Button("Aplicar rutas"))
    {
        if (m_project.IsDirty() && m_editDataDir != m_settings.dataDir)
            Save();
        const bool dataChanged = m_editDataDir != m_settings.dataDir;
        const bool gameChanged = m_editGameDir != m_settings.gameDir;
        m_settings.dataDir = m_editDataDir;
        m_settings.gameDir = m_editGameDir;
        m_settings.portraitsDir = m_editPortraitsDir;
        m_settings.Save();
        if (dataChanged)
            LoadProject();
        else
            m_preview.LoadPortraits(m_settings.PortraitsDir(), m_project);
        if (gameChanged)
            ReloadGameAssets();
    }
    ImGui::EndDisabled();

    ImGui::SeparatorText("Estado");
    ImGui::BulletText("Líneas: %zu", m_project.lines.size());
    ImGui::BulletText("Fuente del juego: %s", m_preview.FontLoaded() ? "cargada (preview exacta)" : "NO encontrada");
    if (m_bank.IsOpen())
        ImGui::BulletText("Voces: %zu cues en SOUND.xsb", m_bank.CueCount());
    else
        ImGui::BulletText("Voces: no disponibles (%s)", m_bankError.c_str());
    ImGui::BulletText("Retratos: %zu", m_preview.PortraitCount());

    ImGui::SeparatorText("Edición");
    if (ImGui::SliderInt("Autoguardado (s)", &m_settings.autosaveSeconds, 0, 600, m_settings.autosaveSeconds ? "%d s" : "desactivado"))
        m_settings.Save();
    if (ImGui::SliderFloat("Máx. caracteres/segundo", &m_settings.maxCharsPerSecond, 0.0f, 40.0f, "%.0f"))
        ApplySettingsChange();
    ImGui::SameLine();
    HelpMarker("Avisa cuando el texto se lee demasiado rápido para su duración (0 = sin aviso).");
    if (ImGui::SliderFloat("Volumen", &m_settings.volume, 0.0f, 1.0f, "%.2f"))
    {
        m_player.SetVolume(m_settings.volume);
        m_settings.Save();
    }

    ImGui::SeparatorText("Interfaz (requiere reiniciar)");
    ImGui::SetNextItemWidth(-ImGui::GetFontSize() * 10);
    if (ImGui::InputTextWithHint("Fuente de la interfaz", "por defecto", &m_settings.uiFontPath))
        m_settings.Save();
    if (ImGui::SliderFloat("Tamaño", &m_settings.uiFontSize, 10.0f, 32.0f, "%.0f"))
        m_settings.Save();
    ImGui::TextDisabled("Ajustes guardados en %s", Settings::FilePath().c_str());
    ImGui::End();
}

void App::DrawQuitModal()
{
    if (m_quitRequested)
    {
        m_quitRequested = false;
        if (m_project.IsDirty() || m_project.boxConfigDirty)
            ImGui::OpenPopup("Cambios sin guardar");
        else
            m_quit = true;
    }
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Cambios sin guardar", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::TextUnformatted("Hay cambios sin guardar. ¿Guardar antes de salir?");
        if (ImGui::Button("Guardar y salir"))
        {
            if (Save())
                m_quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Salir sin guardar"))
        {
            m_quit = true;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancelar"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}
