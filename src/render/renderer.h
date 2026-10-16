#pragma once

#include "../core/video_frame.h"

namespace plumgg {

enum class ScaleMode { Integer, Fit, Stretch };

// Display options chosen in the video menu.
struct RenderOptions {
    ScaleMode scale = ScaleMode::Integer;
    bool smooth = false;     // bilinear filtering instead of sharp pixels
    bool pseudo3d = false;   // toggled by the "efecto 3D" hotkey; read by the pseudo-3D renderer
};

// A renderer turns a VideoFrame into pixels on screen. The emulator core never calls
// into a renderer; the frontend hands it each finished frame. A pseudo-3D renderer
// is just another implementation of this interface.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual const char* name() const = 0;
    virtual void draw(const VideoFrame& frame, int windowWidth, int windowHeight) = 0;
    void setOptions(const RenderOptions& options) { options_ = options; }

protected:
    RenderOptions options_;
};

}  // namespace plumgg
