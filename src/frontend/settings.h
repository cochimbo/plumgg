#pragma once

#include <string>
#include <vector>

#include "../render/renderer.h"
#include "input.h"

namespace plumgg {

// Everything the menus change apart from the controls. Lives in plumgg.ini next to
// the [teclado] / [mando] sections, under [video] and [recientes].
struct Settings {
    static constexpr size_t kMaxRecent = 10;

    std::string view = "2D";  // Renderer::name() of the active view
    RenderOptions render;
    int windowScale = 4;
    bool fullscreen = false;
    bool showMenu = true;
    std::vector<std::string> recent;  // most recent first
    std::string lastFolder;

    void addRecent(const std::string& path);
    void removeRecent(const std::string& path);

    // Reads the sections it knows from `path`, ignoring the rest.
    static Settings load(const std::string& path);
    // Rewrites the whole file: the controls from `input` plus these settings.
    bool save(const std::string& path, const InputConfig& input) const;
};

}  // namespace plumgg
