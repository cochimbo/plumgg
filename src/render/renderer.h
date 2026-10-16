#pragma once

#include "../core/video_frame.h"

namespace plumgg {

// A renderer turns a VideoFrame into pixels on screen. The emulator core never calls
// into a renderer; the frontend hands it each finished frame. A pseudo-3D renderer
// is just another implementation of this interface.
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual const char* name() const = 0;
    virtual void draw(const VideoFrame& frame, int windowWidth, int windowHeight) = 0;
};

}  // namespace plumgg
