#include "input.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace plumgg {

namespace {

// Names used in the [teclado] / [mando] sections of the .ini file.
constexpr const char* kButtonNames[InputConfig::kButtons] = {"arriba", "abajo", "izquierda", "derecha",
                                                             "boton1", "boton2", "start"};
constexpr const char* kHotkeyNames[InputConfig::kHotkeys] = {"efecto_3d"};

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int buttonIndex(const std::string& name) {
    for (int i = 0; i < InputConfig::kButtons; ++i)
        if (name == kButtonNames[i]) return i;
    return -1;
}

int hotkeyIndex(const std::string& name) {
    for (int i = 0; i < InputConfig::kHotkeys; ++i)
        if (name == kHotkeyNames[i]) return i;
    return -1;
}

std::vector<std::string> splitList(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ',')) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

}  // namespace

InputConfig InputConfig::defaults() {
    InputConfig c;
    auto set = [&](GgButton b, std::vector<SDL_Scancode> k, std::vector<SDL_GameControllerButton> p) {
        c.keys[static_cast<int>(b)] = std::move(k);
        c.pad[static_cast<int>(b)] = std::move(p);
    };
    set(GgButton::Up, {SDL_SCANCODE_UP}, {SDL_CONTROLLER_BUTTON_DPAD_UP});
    set(GgButton::Down, {SDL_SCANCODE_DOWN}, {SDL_CONTROLLER_BUTTON_DPAD_DOWN});
    set(GgButton::Left, {SDL_SCANCODE_LEFT}, {SDL_CONTROLLER_BUTTON_DPAD_LEFT});
    set(GgButton::Right, {SDL_SCANCODE_RIGHT}, {SDL_CONTROLLER_BUTTON_DPAD_RIGHT});
    set(GgButton::Button1, {SDL_SCANCODE_Z}, {SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_X});
    set(GgButton::Button2, {SDL_SCANCODE_X}, {SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_Y});
    set(GgButton::Start, {SDL_SCANCODE_RETURN}, {SDL_CONTROLLER_BUTTON_START});
    c.hotkeyKeys[static_cast<int>(Hotkey::Toggle3D)] = {SDL_SCANCODE_F3};
    c.hotkeyPad[static_cast<int>(Hotkey::Toggle3D)] = {SDL_CONTROLLER_BUTTON_BACK};
    return c;
}

InputConfig InputConfig::loadOrCreate(const std::string& path) {
    InputConfig c = defaults();
    std::ifstream in(path);
    if (!in) {
        c.save(path);
        return c;
    }
    std::string line, section;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = lower(trim(line.substr(1, line.size() - 2)));
            continue;
        }
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = lower(trim(line.substr(0, eq)));
        const std::string value = trim(line.substr(eq + 1));

        if (section == "mando" && key == "stick_como_cruceta") {
            c.stickAsDpad = value == "1" || lower(value) == "si" || lower(value) == "true";
            continue;
        }
        if (section == "mando" && key == "zona_muerta") {
            c.stickDeadzone = std::clamp(std::atoi(value.c_str()), 0, 32767);
            continue;
        }
        const int b = buttonIndex(key);
        const int h = hotkeyIndex(key);
        if (b < 0 && h < 0) continue;
        if (section == "teclado") {
            auto& list = b >= 0 ? c.keys[b] : c.hotkeyKeys[h];
            list.clear();
            for (const auto& name : splitList(value)) {
                SDL_Scancode sc = SDL_GetScancodeFromName(name.c_str());
                if (sc != SDL_SCANCODE_UNKNOWN) list.push_back(sc);
            }
        } else if (section == "mando") {
            auto& list = b >= 0 ? c.pad[b] : c.hotkeyPad[h];
            list.clear();
            for (const auto& name : splitList(value)) {
                SDL_GameControllerButton gb = SDL_GameControllerGetButtonFromString(lower(name).c_str());
                if (gb != SDL_CONTROLLER_BUTTON_INVALID) list.push_back(gb);
            }
        }
    }
    return c;
}

bool InputConfig::save(const std::string& path) const {
    std::ofstream out(path);
    if (!out) return false;
    write(out);
    return static_cast<bool>(out);
}

void InputConfig::write(std::ostream& out) const {
    auto keyList = [&](const char* name, const std::vector<SDL_Scancode>& list) {
        out << name << " = ";
        for (size_t k = 0; k < list.size(); ++k) out << (k ? ", " : "") << SDL_GetScancodeName(list[k]);
        out << "\n";
    };
    auto padList = [&](const char* name, const std::vector<SDL_GameControllerButton>& list) {
        out << name << " = ";
        for (size_t k = 0; k < list.size(); ++k) out << (k ? ", " : "") << SDL_GameControllerGetStringForButton(list[k]);
        out << "\n";
    };
    out << "# Controles de plumgg. Puedes poner varias teclas o botones separados por comas.\n"
           "# Teclas: nombres de SDL (Up, Down, Left, Right, Z, X, Return, Space, Left Shift...).\n"
           "# Botones de mando: a, b, x, y, back, guide, start, leftstick, rightstick,\n"
           "# leftshoulder, rightshoulder, dpup, dpdown, dpleft, dpright.\n"
           "# efecto_3d activa y desactiva el efecto pseudo-3D.\n\n[teclado]\n";
    for (int i = 0; i < kButtons; ++i) keyList(kButtonNames[i], keys[i]);
    for (int i = 0; i < kHotkeys; ++i) keyList(kHotkeyNames[i], hotkeyKeys[i]);
    out << "\n[mando]\n";
    for (int i = 0; i < kButtons; ++i) padList(kButtonNames[i], pad[i]);
    for (int i = 0; i < kHotkeys; ++i) padList(kHotkeyNames[i], hotkeyPad[i]);
    out << "stick_como_cruceta = " << (stickAsDpad ? "si" : "no") << "\n";
    out << "zona_muerta = " << stickDeadzone << "\n";
}

Input::~Input() {
    for (auto* p : pads_) SDL_GameControllerClose(p);
}

void Input::openAll() {
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (!SDL_IsGameController(i)) continue;
        if (SDL_GameController* p = SDL_GameControllerOpen(i)) pads_.push_back(p);
    }
}

void Input::handleEvent(const SDL_Event& e) {
    if (e.type == SDL_CONTROLLERDEVICEADDED) {
        SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(e.cdevice.which);
        for (auto* p : pads_)
            if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)) == id) return;
        if (SDL_GameController* p = SDL_GameControllerOpen(e.cdevice.which)) pads_.push_back(p);
    } else if (e.type == SDL_CONTROLLERDEVICEREMOVED) {
        auto it = std::find_if(pads_.begin(), pads_.end(), [&](SDL_GameController* p) {
            return SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(p)) == e.cdevice.which;
        });
        if (it != pads_.end()) {
            SDL_GameControllerClose(*it);
            pads_.erase(it);
        }
    }
}

Hotkey Input::hotkeyFor(const SDL_Event& e) const {
    for (int h = 0; h < InputConfig::kHotkeys; ++h) {
        if (e.type == SDL_KEYDOWN && !e.key.repeat) {
            for (SDL_Scancode sc : config_.hotkeyKeys[h])
                if (sc == e.key.keysym.scancode) return static_cast<Hotkey>(h);
        } else if (e.type == SDL_CONTROLLERBUTTONDOWN) {
            for (SDL_GameControllerButton gb : config_.hotkeyPad[h])
                if (gb == e.cbutton.button) return static_cast<Hotkey>(h);
        }
    }
    return Hotkey::Count;
}

std::vector<std::string> Input::padNames() const {
    std::vector<std::string> names;
    for (auto* p : pads_) {
        const char* n = SDL_GameControllerName(p);
        names.push_back(n ? n : "Mando");
    }
    return names;
}

void Input::rightStick(float* x, float* y) const {
    *x = *y = 0;
    for (auto* p : pads_) {
        const int ax = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTX);
        const int ay = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_RIGHTY);
        if (std::abs(ax) < config_.stickDeadzone && std::abs(ay) < config_.stickDeadzone) continue;
        *x = static_cast<float>(ax) / 32767.0f;
        *y = static_cast<float>(ay) / 32767.0f;
        return;
    }
}

Buttons Input::poll(bool useKeyboard) const {
    std::array<bool, InputConfig::kButtons> down{};
    const Uint8* kb = SDL_GetKeyboardState(nullptr);
    for (int b = 0; b < InputConfig::kButtons; ++b) {
        if (useKeyboard)
            for (SDL_Scancode sc : config_.keys[b]) down[b] = down[b] || kb[sc];
        for (auto* p : pads_)
            for (SDL_GameControllerButton gb : config_.pad[b]) down[b] = down[b] || SDL_GameControllerGetButton(p, gb);
    }
    if (config_.stickAsDpad) {
        for (auto* p : pads_) {
            const int x = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTX);
            const int y = SDL_GameControllerGetAxis(p, SDL_CONTROLLER_AXIS_LEFTY);
            down[static_cast<int>(GgButton::Left)] |= x < -config_.stickDeadzone;
            down[static_cast<int>(GgButton::Right)] |= x > config_.stickDeadzone;
            down[static_cast<int>(GgButton::Up)] |= y < -config_.stickDeadzone;
            down[static_cast<int>(GgButton::Down)] |= y > config_.stickDeadzone;
        }
    }
    Buttons out;
    out.up = down[static_cast<int>(GgButton::Up)];
    out.down = down[static_cast<int>(GgButton::Down)];
    out.left = down[static_cast<int>(GgButton::Left)];
    out.right = down[static_cast<int>(GgButton::Right)];
    out.button1 = down[static_cast<int>(GgButton::Button1)];
    out.button2 = down[static_cast<int>(GgButton::Button2)];
    out.start = down[static_cast<int>(GgButton::Start)];
    return out;
}

}  // namespace plumgg
