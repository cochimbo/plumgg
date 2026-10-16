#pragma once

#include <SDL.h>

#include <array>
#include <string>
#include <vector>

#include "../core/gamegear.h"

namespace plumgg {

enum class GgButton { Up, Down, Left, Right, Button1, Button2, Start, Count };

// Keyboard and gamepad bindings, read from and written to a plain-text .ini file.
struct InputConfig {
    static constexpr int kButtons = static_cast<int>(GgButton::Count);
    std::array<std::vector<SDL_Scancode>, kButtons> keys;
    std::array<std::vector<SDL_GameControllerButton>, kButtons> pad;
    bool stickAsDpad = true;
    int stickDeadzone = 12000;

    static InputConfig defaults();
    // Loads `path`; if it does not exist, writes the defaults there so the user can edit them.
    static InputConfig loadOrCreate(const std::string& path);
    bool save(const std::string& path) const;
};

// Tracks connected controllers and merges keyboard + every pad into one set of buttons.
class Input {
public:
    explicit Input(InputConfig config) : config_(std::move(config)) {}
    ~Input();

    void openAll();
    void handleEvent(const SDL_Event& e);
    Buttons poll() const;

private:
    InputConfig config_;
    std::vector<SDL_GameController*> pads_;
};

}  // namespace plumgg
