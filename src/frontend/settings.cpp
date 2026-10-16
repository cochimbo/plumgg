#include "settings.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace plumgg {

namespace {

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    size_t e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool yes(const std::string& v) {
    const std::string l = lower(v);
    return l == "1" || l == "si" || l == "sí" || l == "true";
}

constexpr const char* kScaleNames[] = {"entero", "ajustar", "estirar"};

}  // namespace

void Settings::addRecent(const std::string& path) {
    removeRecent(path);
    recent.insert(recent.begin(), path);
    if (recent.size() > kMaxRecent) recent.resize(kMaxRecent);
    size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) lastFolder = path.substr(0, slash);
}

void Settings::removeRecent(const std::string& path) {
    recent.erase(std::remove(recent.begin(), recent.end(), path), recent.end());
}

Settings Settings::load(const std::string& path) {
    Settings s;
    std::ifstream in(path);
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

        if (section == "video") {
            if (key == "vista") s.view = value;
            else if (key == "escalado") {
                for (int i = 0; i < 3; ++i)
                    if (lower(value) == kScaleNames[i]) s.render.scale = static_cast<ScaleMode>(i);
            } else if (key == "suavizado") s.render.smooth = yes(value);
            else if (key == "efecto_3d") s.render.pseudo3d = yes(value);
            else if (key == "tamano_ventana") s.windowScale = std::clamp(std::atoi(value.c_str()), 1, 8);
            else if (key == "pantalla_completa") s.fullscreen = yes(value);
            else if (key == "barra_menus") s.showMenu = yes(value);
        } else if (section == "recientes") {
            if (key == "rom" && !value.empty() && s.recent.size() < kMaxRecent) s.recent.push_back(value);
            else if (key == "ultima_carpeta") s.lastFolder = value;
        }
    }
    return s;
}

bool Settings::save(const std::string& path, const InputConfig& input) const {
    std::ofstream out(path);
    if (!out) return false;
    input.write(out);
    auto flag = [](bool b) { return b ? "si" : "no"; };
    out << "\n[video]\n"
        << "# vista: 2D o capas. escalado: entero, ajustar o estirar.\n"
        << "vista = " << view << "\n"
        << "escalado = " << kScaleNames[static_cast<int>(render.scale)] << "\n"
        << "suavizado = " << flag(render.smooth) << "\n"
        << "efecto_3d = " << flag(render.pseudo3d) << "\n"
        << "tamano_ventana = " << windowScale << "\n"
        << "pantalla_completa = " << flag(fullscreen) << "\n"
        << "barra_menus = " << flag(showMenu) << "\n"
        << "\n[recientes]\n";
    if (!lastFolder.empty()) out << "ultima_carpeta = " << lastFolder << "\n";
    for (const auto& r : recent) out << "rom = " << r << "\n";
    return static_cast<bool>(out);
}

}  // namespace plumgg
