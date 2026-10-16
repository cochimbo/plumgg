#pragma once

#include <SDL.h>

#include <vector>

#include "renderer.h"

namespace plumgg {

// Base for renderers that upload a 160x144 image and scale it to the window.
class SdlTextureRenderer : public Renderer {
public:
    explicit SdlTextureRenderer(SDL_Renderer* sdl);
    ~SdlTextureRenderer() override;
    void draw(const VideoFrame& frame, int windowWidth, int windowHeight) override;

protected:
    // Fills `out` (160x144, 0xAARRGGBB) from the frame.
    virtual void compose(const VideoFrame& frame, uint32_t* out) = 0;

private:
    SDL_Renderer* sdl_;
    SDL_Texture* texture_ = nullptr;
    std::vector<uint32_t> pixels_;
};

// The real Game Gear picture.
class FlatRenderer : public SdlTextureRenderer {
public:
    using SdlTextureRenderer::SdlTextureRenderer;
    const char* name() const override { return "2D"; }

protected:
    void compose(const VideoFrame& frame, uint32_t* out) override;
};

// Debug view that tints each pixel by the layer it came from, which is the
// information a pseudo-3D renderer will use to separate depths.
class LayerRenderer : public SdlTextureRenderer {
public:
    using SdlTextureRenderer::SdlTextureRenderer;
    const char* name() const override { return "capas"; }

protected:
    void compose(const VideoFrame& frame, uint32_t* out) override;
};

}  // namespace plumgg
