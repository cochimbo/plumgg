#pragma once

#include <array>
#include <vector>

#include "gl.h"
#include "planes.h"
#include "renderer.h"

namespace plumgg {

// Base for renderers that build a flat 160x144 image and scale it to the window.
class GlTextureRenderer : public Renderer {
public:
    GlTextureRenderer();
    ~GlTextureRenderer() override;
    GlTextureRenderer(const GlTextureRenderer&) = delete;
    GlTextureRenderer& operator=(const GlTextureRenderer&) = delete;
    void draw(const VideoFrame& frame, const Viewport& area) override;

protected:
    // Fills `out` (160x144, 0xAARRGGBB) from the frame.
    virtual void compose(const VideoFrame& frame, uint32_t* out) = 0;

private:
    gl::GLuint program_ = 0;
    gl::GLuint vao_ = 0;
    gl::GLuint texture_ = 0;
    std::vector<uint32_t> pixels_;
};

// The real Game Gear picture.
class FlatRenderer : public GlTextureRenderer {
public:
    const char* name() const override { return "2D"; }

protected:
    void compose(const VideoFrame& frame, uint32_t* out) override;
};

// Debug view that tints each pixel by the layer it came from, which is the
// information the pseudo-3D renderer uses to separate depths.
class LayerRenderer : public GlTextureRenderer {
public:
    const char* name() const override { return "capas (depuración)"; }

protected:
    void compose(const VideoFrame& frame, uint32_t* out) override;
};

// Pulls the picture apart into planes and stands them up like a diorama seen from a
// tilted camera: the sky at the back, then the background tiles, the sprites, and the
// priority tiles at the front. In the layers style each raised plane is a cut-out
// whose sides reach down towards the plane below it; in the voxels style every pixel
// of a raised plane is a lit block. Sprites can cast a shadow on the background.
class Pseudo3DRenderer : public Renderer {
public:
    Pseudo3DRenderer();
    ~Pseudo3DRenderer() override;
    Pseudo3DRenderer(const Pseudo3DRenderer&) = delete;
    Pseudo3DRenderer& operator=(const Pseudo3DRenderer&) = delete;
    const char* name() const override { return "3D"; }
    void draw(const VideoFrame& frame, const Viewport& area) override;

private:
    gl::GLuint program_ = 0;
    gl::GLuint voxelProgram_ = 0;
    gl::GLuint vao_ = 0;
    std::array<gl::GLuint, kPlanes> textures_{};
    gl::GLuint solidTexture_ = 0;
    Planes planes_;
    std::vector<uint32_t> solid_;  // markSolidTiles() for the voxel style
};

}  // namespace plumgg
