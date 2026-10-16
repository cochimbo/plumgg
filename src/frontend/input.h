#pragma once

#include <SDL.h>

#include <array>
#include <ostream>
#include <string>
#include <vector>

#include "../core/gamegear.h"

namespace plumgg {

enum class GgButton { Up, Down, Left, Right, Button1, Button2, Start, Count };

// Emulator actions that can be bound like console buttons but fire once per press.
enum class Hotkey { Toggle3D, Count };

// Keyboard and gamepad bindings, read from and written to a plain-text .ini file.
struct InputConfig {
    static constexpr int kButtons = static_cast<int>(GgButton::Count);
    static constexpr int kHotkeys = static_cast<int>(Hotkey::Count);
    std::array<std::vector<SDL_Scancode>, kButtons> keys;
    std::array<std::vector<SDL_GameControllerButton>, kButtons> pad;
    std::array<std::vector<SDL_Scancode>, kHotkeys> hotkeyKeys;
    std::array<std::vector<SDL_GameControllerButton>, kHotkeys> hotkeyPad;
    bool stickAsDpad = true;
    int stickDeadzone = 12000;

    static InputConfig defaults();
    // Loads `path`; if it does not exist, writes the defaults there so the user can edit them.
    static InputConfig loadOrCreate(const std::string& path);
    bool save(const std::string& path) const;
    // Writes the [teclado] and [mando] sections, so other settings can share the file.
    void write(std::ostream& out) const;
};

// Tracks connected controllers and merges keyboard + every pad into one set of buttons.
class Input {
public:
    explicit Input(InputConfig config) : config_(std::move(config)) {}
    ~Input();

    void openAll();
    void handleEvent(const SDL_Event& e);
    // `useKeyboard` = false ignores the keyboard (e.g. while a menu has focus).
    Buttons poll(bool useKeyboard = true) const;
    // The hotkey this key or pad button press triggers, or Hotkey::Count if none.
    Hotkey hotkeyFor(const SDL_Event& e) const;

    const InputConfig& config() const { return config_; }
    void setConfig(InputConfig config) { config_ = std::move(config); }
    std::vector<std::string> padNames() const;

private:
    InputConfig config_;
    std::vector<SDL_GameController*> pads_;
};

}  // namespace plumgg
