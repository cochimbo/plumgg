#pragma once

#include "../core/video_frame.h"

namespace plumgg {

enum class ScaleMode { Integer, Fit, Stretch };

// How the pseudo-3D view builds each plane: flat cut-outs, or one block per pixel.
enum class Style3D { Layers, Voxels };

// Display options chosen in the video menu.
struct RenderOptions {
    ScaleMode scale = ScaleMode::Integer;
    bool smooth = false;     // bilinear filtering instead of sharp pixels
    bool pseudo3d = false;   // toggled by the "efecto 3D" hotkey: draw with the pseudo-3D renderer
    // Pseudo-3D look, each 0..1 except the tilt.
    Style3D style = Style3D::Voxels;
    float depth = 0.6f;      // how far apart the layers are
    float tilt = 25.0f;      // camera tilt in degrees; at 0 the picture looks flat
    float thickness = 1.0f;  // how much of the gap below a plane its sides (or blocks) fill
    bool shadows = true;     // sprites cast a shadow on the background
};

// Where to draw, in OpenGL window coordinates (origin at the bottom left).
struct Viewport {
    int x = 0, y = 0, width = 0, height = 0;
};

// A renderer turns a VideoFrame into pixels on screen. The emulator core never calls
// into a renderer; the frontend hands it each finished frame. A pseudo-3D renderer
// is just another implementation of this interface. Renderers draw with OpenGL 3.3 into
// the current context.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual const char* name() const = 0;
    virtual void draw(const VideoFrame& frame, const Viewport& area) = 0;
    void setOptions(const RenderOptions& options) { options_ = options; }

protected:
    RenderOptions options_;
};

}  // namespace plumgg
