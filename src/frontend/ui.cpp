#include "ui.h"

#include <algorithm>

#include "imgui.h"
#include "imgui_impl_sdl2.h"
#include "imgui_impl_opengl3.h"

namespace plumgg {

namespace {

constexpr int kRows = InputConfig::kButtons + InputConfig::kHotkeys;
constexpr const char* kRowNames[kRows] = {"Arriba", "Abajo", "Izquierda", "Derecha",
                                          "Botón 1", "Botón 2", "Start", "Efecto 3D"};
static_assert(InputConfig::kButtons == 7 && InputConfig::kHotkeys == 1, "update kRowNames");

std::vector<SDL_Scancode>& keysOf(InputConfig& c, int row) {
    return row < InputConfig::kButtons ? c.keys[row] : c.hotkeyKeys[row - InputConfig::kButtons];
}

std::vector<SDL_GameControllerButton>& padOf(InputConfig& c, int row) {
    return row < InputConfig::kButtons ? c.pad[row] : c.hotkeyPad[row - InputConfig::kButtons];
}

std::string fileName(const std::string& path) {
    size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

Ui::Ui(SDL_Window* window, SDL_GLContext context, Settings& settings, Input& input)
    : window_(window), settings_(settings), input_(input) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;  // window layout is not worth a second settings file
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 4.0f;
#ifdef _WIN32
    // Segoe UI reads better than the built-in bitmap font; it covers Spanish accents too.
    const char* font = "C:\\Windows\\Fonts\\segoeui.ttf";
    if (SDL_RWops* f = SDL_RWFromFile(font, "rb")) {
        SDL_RWclose(f);
        io.Fonts->AddFontFromFileTTF(font, 18.0f);
    }
#endif
    ImGui_ImplSDL2_InitForOpenGL(window_, context);
    ImGui_ImplOpenGL3_Init("#version 330 core");
}

Ui::~Ui() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
}

bool Ui::handleEvent(const SDL_Event& e) {
    if (capture_.active) {
        if (e.type == SDL_KEYDOWN) {
            if (e.key.keysym.scancode == SDL_SCANCODE_ESCAPE) capture_.active = false;
            else if (!capture_.pad) finishCapture(e.key.keysym.scancode);
            return true;
        }
        if (e.type == SDL_KEYUP || e.type == SDL_TEXTINPUT) return true;
        if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            if (capture_.pad) finishCapture(e.cbutton.button);
            return true;
        }
        if (e.type == SDL_CONTROLLERBUTTONUP) return true;
    }
    ImGui_ImplSDL2_ProcessEvent(&e);
    return false;
}

bool Ui::keyboardBusy() const { return capture_.active || ImGui::GetIO().WantCaptureKeyboard; }

void Ui::finishCapture(int code) {
    InputConfig c = input_.config();
    if (capture_.pad) {
        auto& list = padOf(c, capture_.row);
        const auto b = static_cast<SDL_GameControllerButton>(code);
        if (!capture_.append) list.clear();
        if (std::find(list.begin(), list.end(), b) == list.end()) list.push_back(b);
    } else {
        auto& list = keysOf(c, capture_.row);
        const auto k = static_cast<SDL_Scancode>(code);
        if (!capture_.append) list.clear();
        if (std::find(list.begin(), list.end(), k) == list.end()) list.push_back(k);
    }
    input_.setConfig(std::move(c));
    capture_.active = false;
    pendingSave_ = true;
}

void Ui::notify(std::string message) {
    toast_ = std::move(message);
    toastUntil_ = SDL_GetTicks() + 2500;
}

UiActions Ui::frame(const UiState& state) {
    UiActions out;
    out.settingsChanged = pendingSave_;
    pendingSave_ = false;

    if (openRequested_) {
        openRequested_ = false;
        std::string path;
        switch (openRomDialog(window_, settings_.lastFolder, &path)) {
            case DialogResult::Chosen: out.load = path; break;
            case DialogResult::Cancelled: break;
            case DialogResult::Unavailable: browser_.open(settings_.lastFolder); break;
        }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    drawMenuBar(state, out);
    if (showControls_) drawControls(out);
    if (showVideo_) drawVideo(state, out);
    if (showAbout_) drawAbout();
    std::string picked;
    if (browser_.draw(&picked)) out.load = picked;
    drawOverlays(state);
    return out;
}

void Ui::render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Ui::drawMenuBar(const UiState& state, UiActions& out) {
    const ImGuiIO& io = ImGui::GetIO();
    const float barHeight = ImGui::GetFrameHeight();
    if (!settings_.showMenu) {
        // Hidden bar: reveal it while the mouse sits at the top edge or a menu is open.
        const bool atTop = io.MousePos.y >= 0 && io.MousePos.y < (peekMenu_ ? barHeight : 4.0f);
        peekMenu_ = atTop || (peekMenu_ && ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId));
    }
    menuHeight_ = settings_.showMenu ? static_cast<int>(barHeight) : 0;
    if (!settings_.showMenu && !peekMenu_) return;
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("Archivo")) {
        if (ImGui::MenuItem("Abrir ROM...", "Ctrl+O")) openRequested_ = true;
        if (ImGui::BeginMenu("Recientes", !settings_.recent.empty())) {
            for (const auto& path : settings_.recent) {
                if (ImGui::MenuItem(fileName(path).c_str())) out.load = path;
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", path.c_str());
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Borrar lista")) {
                settings_.recent.clear();
                out.settingsChanged = true;
            }
            ImGui::EndMenu();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Salir")) out.quit = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Juego")) {
        if (ImGui::MenuItem("Pausa", "P", state.paused, state.romLoaded)) out.togglePause = true;
        if (ImGui::MenuItem("Reiniciar", "F1", false, state.romLoaded)) out.reset = true;
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Opciones")) {
        if (ImGui::MenuItem("Controles...")) showControls_ = true;
        if (ImGui::MenuItem("Vídeo...")) showVideo_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Efecto 3D", nullptr, settings_.render.pseudo3d)) {
            settings_.render.pseudo3d = !settings_.render.pseudo3d;
            out.settingsChanged = true;
        }
        if (ImGui::MenuItem("Pantalla completa", "F11", settings_.fullscreen)) {
            settings_.fullscreen = !settings_.fullscreen;
            out.fullscreenChanged = out.settingsChanged = true;
        }
        if (ImGui::MenuItem("Barra de menús", "Esc", settings_.showMenu)) {
            settings_.showMenu = !settings_.showMenu;
            out.settingsChanged = true;
        }
        ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Ayuda")) {
        if (ImGui::MenuItem("Acerca de plumgg")) showAbout_ = true;
        ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
}

void Ui::drawControls(UiActions& out) {
    ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Controles", &showControls_)) {
        ImGui::End();
        return;
    }
    if (!showControls_) capture_.active = false;

    ImGui::TextWrapped("Pulsa una casilla y luego la tecla o el botón del mando. \"+\" añade otro, "
                       "\"x\" lo borra, Esc cancela.");
    InputConfig c = input_.config();
    bool changed = false;

    if (ImGui::BeginTable("bindings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("Acción", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableSetupColumn("Teclado");
        ImGui::TableSetupColumn("Mando");
        ImGui::TableHeadersRow();
        for (int row = 0; row < kRows; ++row) {
            ImGui::PushID(row);
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(kRowNames[row]);
            for (int col = 0; col < 2; ++col) {
                const bool pad = col == 1;
                ImGui::TableNextColumn();
                ImGui::PushID(col);
                std::string label;
                if (capture_.active && capture_.row == row && capture_.pad == pad) {
                    label = pad ? "Pulsa un botón..." : "Pulsa una tecla...";
                } else if (pad) {
                    for (auto b : padOf(c, row)) label += (label.empty() ? "" : ", ") + std::string(SDL_GameControllerGetStringForButton(b));
                } else {
                    for (auto k : keysOf(c, row)) label += (label.empty() ? "" : ", ") + std::string(SDL_GetScancodeName(k));
                }
                if (label.empty()) label = "-";
                const float small = ImGui::GetFrameHeight() * 2 + ImGui::GetStyle().ItemSpacing.x * 2;
                if (ImGui::Button(label.c_str(), ImVec2(ImGui::GetContentRegionAvail().x - small, 0)))
                    capture_ = {true, row, pad, false};
                ImGui::SameLine();
                if (ImGui::Button("+", ImVec2(ImGui::GetFrameHeight(), 0))) capture_ = {true, row, pad, true};
                ImGui::SameLine();
                if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0))) {
                    if (pad) padOf(c, row).clear();
                    else keysOf(c, row).clear();
                    capture_.active = false;
                    changed = true;
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    changed |= ImGui::Checkbox("Usar el stick izquierdo como cruceta", &c.stickAsDpad);
    ImGui::BeginDisabled(!c.stickAsDpad);
    int dead = c.stickDeadzone * 100 / 32767;
    if (ImGui::SliderInt("Zona muerta del stick", &dead, 0, 95, "%d%%")) {
        c.stickDeadzone = dead * 32767 / 100;
        changed = true;
    }
    ImGui::EndDisabled();

    const auto pads = input_.padNames();
    if (pads.empty()) {
        ImGui::TextDisabled("No hay mandos conectados.");
    } else {
        ImGui::Text("Mandos conectados:");
        for (const auto& p : pads) ImGui::BulletText("%s", p.c_str());
    }

    ImGui::Spacing();
    if (ImGui::Button("Restaurar predeterminados")) {
        const InputConfig d = InputConfig::defaults();
        c.keys = d.keys;
        c.pad = d.pad;
        c.hotkeyKeys = d.hotkeyKeys;
        c.hotkeyPad = d.hotkeyPad;
        c.stickAsDpad = d.stickAsDpad;
        c.stickDeadzone = d.stickDeadzone;
        capture_.active = false;
        changed = true;
    }
    if (changed) {
        input_.setConfig(std::move(c));
        out.settingsChanged = true;
    }
    ImGui::End();
}

void Ui::drawVideo(const UiState& state, UiActions& out) {
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_Appearing);
    if (!ImGui::Begin("Vídeo", &showVideo_)) {
        ImGui::End();
        return;
    }
    bool changed = false;

    if (ImGui::BeginCombo("Vista", settings_.view.c_str())) {
        for (const char* v : state.views) {
            if (ImGui::Selectable(v, settings_.view == v)) {
                settings_.view = v;
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Imagen");
    int scale = static_cast<int>(settings_.render.scale);
    changed |= ImGui::RadioButton("Escala entera", &scale, static_cast<int>(ScaleMode::Integer));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Ajustar", &scale, static_cast<int>(ScaleMode::Fit));
    ImGui::SameLine();
    changed |= ImGui::RadioButton("Estirar", &scale, static_cast<int>(ScaleMode::Stretch));
    settings_.render.scale = static_cast<ScaleMode>(scale);
    changed |= ImGui::Checkbox("Suavizar píxeles", &settings_.render.smooth);

    ImGui::SeparatorText("Ventana");
    ImGui::BeginDisabled(settings_.fullscreen);
    ImGui::TextUnformatted("Tamaño:");
    for (int s = 1; s <= 6; ++s) {
        ImGui::SameLine();
        const std::string label = std::to_string(s) + "x";
        if (ImGui::Button(label.c_str())) {
            settings_.windowScale = s;
            out.windowScale = s;
            changed = true;
        }
    }
    ImGui::EndDisabled();
    if (ImGui::Checkbox("Pantalla completa (F11)", &settings_.fullscreen)) {
        out.fullscreenChanged = true;
        changed = true;
    }
    changed |= ImGui::Checkbox("Mostrar la barra de menús (Esc)", &settings_.showMenu);

    ImGui::SeparatorText("Pseudo-3D");
    changed |= ImGui::Checkbox("Efecto 3D", &settings_.render.pseudo3d);
    ImGui::SameLine();
    ImGui::TextDisabled("(atajo en Opciones > Controles)");
    RenderOptions& r = settings_.render;
    ImGui::BeginDisabled(!r.pseudo3d);
    // Sliders save once released rather than on every frame of the drag.
    float depth = r.depth * 100.0f, thickness = r.thickness * 100.0f;
    if (ImGui::SliderFloat("Profundidad", &depth, 0.0f, 100.0f, "%.0f %%")) r.depth = depth / 100.0f;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    ImGui::SliderFloat("Inclinación", &r.tilt, 0.0f, 45.0f, "%.0f°");
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    if (ImGui::SliderFloat("Grosor", &thickness, 0.0f, 100.0f, "%.0f %%")) r.thickness = thickness / 100.0f;
    changed |= ImGui::IsItemDeactivatedAfterEdit();
    changed |= ImGui::Checkbox("Sombra de los sprites", &r.shadows);
    if (ImGui::Button("Valores por defecto")) {
        const RenderOptions defaults;
        r.depth = defaults.depth;
        r.tilt = defaults.tilt;
        r.thickness = defaults.thickness;
        r.shadows = defaults.shadows;
        changed = true;
    }
    ImGui::EndDisabled();

    out.settingsChanged |= changed;
    ImGui::End();
}

void Ui::drawAbout() {
    if (!ImGui::Begin("Acerca de plumgg", &showAbout_, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }
    ImGui::TextUnformatted("plumgg, emulador de Sega Game Gear.");
    ImGui::Spacing();
    ImGui::TextUnformatted("Arrastra una ROM a la ventana o usa Archivo > Abrir ROM.");
    ImGui::SeparatorText("Atajos");
    ImGui::BulletText("Ctrl+O  abrir ROM");
    ImGui::BulletText("F1  reiniciar");
    ImGui::BulletText("F2  cambiar de vista");
    ImGui::BulletText("P  pausa");
    ImGui::BulletText("F11  pantalla completa");
    ImGui::BulletText("Esc  mostrar u ocultar la barra de menús");
    ImGui::BulletText("F3  efecto 3D (se puede cambiar en Opciones > Controles)");
    ImGui::End();
}

void Ui::drawOverlays(const UiState& state) {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
    const float top = vp->Pos.y + static_cast<float>(menuHeight_);

    if (!state.romLoaded) {
        // Behind every window, so it never shows through the option windows.
        const char* hint = "Arrastra una ROM aquí o usa Archivo > Abrir ROM";
        const ImVec2 size = ImGui::CalcTextSize(hint);
        const ImVec2 pos(vp->Pos.x + (vp->Size.x - size.x) * 0.5f, top + (vp->Size.y - menuHeight_ - size.y) * 0.5f);
        ImGui::GetBackgroundDrawList()->AddText(pos, ImGui::GetColorU32(ImGuiCol_Text), hint);
    }
    if (state.paused && state.romLoaded) {
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x - 10, top + 10), ImGuiCond_Always, ImVec2(1, 0));
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##pausa", nullptr, flags);
        ImGui::TextUnformatted("PAUSA");
        ImGui::End();
    }
    if (!toast_.empty() && SDL_TICKS_PASSED(SDL_GetTicks(), toastUntil_)) toast_.clear();
    if (!toast_.empty()) {
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + 10, vp->Pos.y + vp->Size.y - 10), ImGuiCond_Always, ImVec2(0, 1));
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##aviso", nullptr, flags);
        ImGui::TextUnformatted(toast_.c_str());
        ImGui::End();
    }
}

}  // namespace plumgg
