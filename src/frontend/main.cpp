#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../core/gamegear.h"
#include "../render/gl_renderers.h"
#include "audio.h"
#include "input.h"
#include "settings.h"
#include "ui.h"

using namespace plumgg;

namespace {

constexpr double kFrameSeconds = 1.0 / 59.922743;  // NTSC Game Gear refresh

std::string savePathFor(const std::string& romPath) {
    size_t dot = romPath.find_last_of('.');
    size_t slash = romPath.find_last_of("/\\");
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) return romPath + ".sav";
    return romPath.substr(0, dot) + ".sav";
}

struct App {
    GameGear console;
    bool romLoaded = false;

    void saveBattery() {
        if (romLoaded && console.cartridge().hasSaveData()) {
            console.cartridge().writeSave(savePathFor(console.cartridge().path()));
        }
    }

    bool load(const std::string& path, SDL_Window* window) {
        saveBattery();
        std::string error;
        if (!console.loadRom(path, &error)) {
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "plumgg", error.c_str(), window);
            return false;
        }
        console.cartridge().loadSave(savePathFor(path));
        romLoaded = true;
        size_t slash = path.find_last_of("/\\");
        std::string title = "plumgg - " + (slash == std::string::npos ? path : path.substr(slash + 1));
        SDL_SetWindowTitle(window, title.c_str());
        return true;
    }
};

// A plumgg.ini next to the executable wins (portable use); otherwise the per-user
// settings folder, since the install folder may not be writable.
std::string configPath() {
    std::string portable;
    if (char* base = SDL_GetBasePath()) {
        portable = std::string(base) + "plumgg.ini";
        SDL_free(base);
        if (SDL_RWops* f = SDL_RWFromFile(portable.c_str(), "rb")) {
            SDL_RWclose(f);
            return portable;
        }
    }
    if (char* pref = SDL_GetPrefPath("plumgg", "plumgg")) {
        std::string path = std::string(pref) + "plumgg.ini";
        SDL_free(pref);
        return path;
    }
    return portable.empty() ? "plumgg.ini" : portable;
}

void applyWindowScale(SDL_Window* window, int scale, int menuHeight) {
    SDL_SetWindowSize(window, VideoFrame::kVisibleWidth * scale, VideoFrame::kVisibleHeight * scale + menuHeight);
}

}  // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    const std::string iniPath = configPath();
    auto input = std::make_unique<Input>(InputConfig::loadOrCreate(iniPath));
    Settings settings = Settings::load(iniPath);

    // OpenGL 3.3 core everywhere; macOS only hands out core contexts marked forward compatible.
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_Window* window = SDL_CreateWindow("plumgg", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                          VideoFrame::kVisibleWidth * settings.windowScale,
                                          VideoFrame::kVisibleHeight * settings.windowScale,
                                          SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, VideoFrame::kVisibleWidth, VideoFrame::kVisibleHeight);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    const char* missing = context ? gl::load() : nullptr;
    if (!context || missing) {
        const std::string message =
            "plumgg necesita una tarjeta gráfica con OpenGL 3.3 y sus drivers actualizados.\n\n" +
            std::string(context ? "Falta la función " + std::string(missing) : SDL_GetError());
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "plumgg", message.c_str(), window);
        return 1;
    }
    SDL_GL_SetSwapInterval(0);  // the loop below paces itself to the console's refresh rate

    std::vector<std::unique_ptr<Renderer>> renderers;
    renderers.push_back(std::make_unique<FlatRenderer>());
    renderers.push_back(std::make_unique<LayerRenderer>());
    auto pseudo3d = std::make_unique<Pseudo3DRenderer>();
    UiState uiState;
    for (const auto& r : renderers) uiState.views.push_back(r->name());
    auto viewIndex = [&] {
        for (size_t i = 0; i < renderers.size(); ++i)
            if (settings.view == renderers[i]->name()) return i;
        return size_t{0};
    };
    // An unknown view in the settings file (an old name, a typo) falls back to 2D.
    settings.view = renderers[viewIndex()]->name();

    input->openAll();
    auto ui = std::make_unique<Ui>(window, context, settings, *input);
    auto app = std::make_unique<App>();
    auto audio = std::make_unique<Audio>();

    auto save = [&] { settings.save(iniPath, input->config()); };
    auto load = [&](const std::string& path) {
        if (app->load(path, window)) {
            settings.addRecent(path);
        } else {
            settings.removeRecent(path);
        }
        save();
    };
    auto setFullscreen = [&] { SDL_SetWindowFullscreen(window, settings.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0); };
    if (settings.fullscreen) setFullscreen();
    if (argc > 1) load(argv[1]);
    bool sizedForMenu = false;

    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 next = SDL_GetPerformanceCounter();
    bool running = true;
    bool paused = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (ui->handleEvent(e)) continue;
            input->handleEvent(e);
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    load(e.drop.file);
                    SDL_free(e.drop.file);
                    break;
                default: break;
            }
            if (ui->keyboardBusy() && e.type == SDL_KEYDOWN) continue;

            if (input->hotkeyFor(e) == Hotkey::Toggle3D) {
                settings.render.pseudo3d = !settings.render.pseudo3d;
                ui->notify(settings.render.pseudo3d ? "Efecto 3D activado" : "Efecto 3D desactivado");
                save();
            }
            if (e.type != SDL_KEYDOWN || e.key.repeat) continue;
            const bool ctrl = (e.key.keysym.mod & KMOD_CTRL) != 0;
            const bool alt = (e.key.keysym.mod & KMOD_ALT) != 0;
            switch (e.key.keysym.scancode) {
                case SDL_SCANCODE_ESCAPE:
                    settings.showMenu = !settings.showMenu;
                    save();
                    break;
                case SDL_SCANCODE_O:
                    if (ctrl) ui->openRom();
                    break;
                case SDL_SCANCODE_F1: app->console.reset(); break;
                case SDL_SCANCODE_F2:
                    settings.view = renderers[(viewIndex() + 1) % renderers.size()]->name();
                    ui->notify(std::string("Vista: ") + settings.view);
                    save();
                    break;
                case SDL_SCANCODE_P: paused = !paused; break;
                case SDL_SCANCODE_M:
                    settings.mute = !settings.mute;
                    ui->notify(settings.mute ? "Sonido silenciado" : "Sonido activado");
                    save();
                    break;
                case SDL_SCANCODE_RETURN:
                    if (!alt) break;
                    [[fallthrough]];
                case SDL_SCANCODE_F11:
                    settings.fullscreen = !settings.fullscreen;
                    setFullscreen();
                    save();
                    break;
                default: break;
            }
        }

        uiState.romLoaded = app->romLoaded;
        uiState.paused = paused;
        const UiActions act = ui->frame(uiState);
        if (!act.load.empty()) load(act.load);
        if (act.reset) app->console.reset();
        if (act.togglePause) paused = !paused;
        if (act.quit) running = false;
        if (act.fullscreenChanged) setFullscreen();
        if (act.windowScale) applyWindowScale(window, act.windowScale, ui->menuHeight());
        if (act.settingsChanged) save();
        // The first frame tells us how tall the menu bar is; grow the window to fit it.
        if (!sizedForMenu && !settings.fullscreen) {
            applyWindowScale(window, settings.windowScale, ui->menuHeight());
            sizedForMenu = true;
        }

        if (app->romLoaded && !paused) {
            app->console.setButtons(input->poll(!ui->keyboardBusy()));
            app->console.psg().setSampleRate(audio->rate());
            app->console.runFrame();
            audio->push(app->console.psg().samples(), settings.volume, settings.mute);
            app->console.psg().clearSamples();
        }

        int w, h;
        SDL_GL_GetDrawableSize(window, &w, &h);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0, 0, 0, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        // The game fills the window below the menu bar; OpenGL counts rows from the bottom.
        const Viewport game{0, 0, w, std::max(1, h - ui->menuHeight())};
        Renderer& view = settings.render.pseudo3d ? *pseudo3d : *renderers[viewIndex()];
        view.setOptions(settings.render);
        view.draw(app->console.frame(), game);
        ui->render();
        SDL_GL_SwapWindow(window);

        // Pace to the console's refresh rate; resync if we fell far behind.
        next += static_cast<Uint64>(kFrameSeconds * freq);
        Uint64 now = SDL_GetPerformanceCounter();
        if (now < next) {
            SDL_Delay(static_cast<Uint32>((next - now) * 1000 / freq));
        } else if (now - next > static_cast<Uint64>(freq / 4)) {
            next = now;
        }
    }

    app->saveBattery();
    save();
    audio.reset();
    ui.reset();
    renderers.clear();
    pseudo3d.reset();
    input.reset();
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
