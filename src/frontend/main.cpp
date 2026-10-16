#include <SDL.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../core/gamegear.h"
#include "../render/sdl_renderers.h"
#include "input.h"

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

}  // namespace

int main(int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER) != 0) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("plumgg - arrastra una ROM de Game Gear aquí", SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED, VideoFrame::kVisibleWidth * 4,
                                          VideoFrame::kVisibleHeight * 4, SDL_WINDOW_RESIZABLE);
    if (!window) {
        std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Renderer* sdl = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!sdl) sdl = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (!sdl) {
        std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }

    std::vector<std::unique_ptr<Renderer>> renderers;
    renderers.push_back(std::make_unique<FlatRenderer>(sdl));
    renderers.push_back(std::make_unique<LayerRenderer>(sdl));
    size_t current = 0;

    auto input = std::make_unique<Input>(InputConfig::loadOrCreate(configPath()));
    input->openAll();

    auto app = std::make_unique<App>();
    if (argc > 1) app->load(argv[1], window);

    const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
    Uint64 next = SDL_GetPerformanceCounter();
    bool running = true;
    bool paused = false;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            input->handleEvent(e);
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_DROPFILE:
                    app->load(e.drop.file, window);
                    SDL_free(e.drop.file);
                    break;
                case SDL_KEYDOWN:
                    switch (e.key.keysym.scancode) {
                        case SDL_SCANCODE_ESCAPE: running = false; break;
                        case SDL_SCANCODE_F1: app->console.reset(); break;
                        case SDL_SCANCODE_F2: current = (current + 1) % renderers.size(); break;
                        case SDL_SCANCODE_P: paused = !paused; break;
                        default: break;
                    }
                    break;
                default: break;
            }
        }

        if (app->romLoaded && !paused) {
            app->console.setButtons(input->poll());
            app->console.runFrame();
        }

        int w, h;
        SDL_GetRendererOutputSize(sdl, &w, &h);
        renderers[current]->draw(app->console.frame(), w, h);
        SDL_RenderPresent(sdl);

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
    renderers.clear();
    input.reset();
    SDL_DestroyRenderer(sdl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
