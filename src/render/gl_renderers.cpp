#include "gl_renderers.h"

#include <algorithm>
#include <cmath>

namespace plumgg {

namespace {

constexpr int kW = VideoFrame::kVisibleWidth;
constexpr int kH = VideoFrame::kVisibleHeight;

gl::GLuint makeTexture() {
    gl::GLuint t = 0;
    gl::GenTextures(1, &t);
    gl::BindTexture(gl::TEXTURE_2D, t);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, kW, kH, 0, gl::BGRA, gl::UNSIGNED_INT_8_8_8_8_REV, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    return t;
}

// Uploads 160x144 0xAARRGGBB pixels into the bound texture.
void upload(gl::GLuint texture, const uint32_t* pixels, bool smooth) {
    gl::BindTexture(gl::TEXTURE_2D, texture);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, kW, kH, gl::BGRA, gl::UNSIGNED_INT_8_8_8_8_REV, pixels);
    const gl::GLint filter = smooth ? gl::LINEAR : gl::NEAREST;
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, filter);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, filter);
}

// Both programs draw a unit quad from gl_VertexID with a triangle strip, so they need
// no vertex buffer, only an (empty) vertex array object.
constexpr const char* kQuadVertex = R"(#version 330 core
out vec2 vUv;
void main() {
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    vUv = vec2(corner.x, 1.0 - corner.y);
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

constexpr const char* kQuadFragment = R"(#version 330 core
uniform sampler2D uImage;
in vec2 vUv;
out vec4 outColor;
void main() { outColor = texture(uImage, vUv); }
)";

// One plane of the diorama, drawn as `instances` stacked slices: the top one at uTop,
// the rest stepping down by uStep to make the plane's sides. Every slice is scaled so
// that, seen from the untilted camera, it covers exactly the same pixels as the plane
// at height 0; tilting the camera is what reveals the depth.
constexpr const char* kPlaneVertex = R"(#version 330 core
uniform mat4 uViewProj;
uniform float uTop;
uniform float uStep;
uniform float uDistance;
out vec2 vUv;
flat out int vSlice;
void main() {
    vec2 corner = vec2(gl_VertexID & 1, gl_VertexID >> 1);
    float z = uTop - uStep * float(gl_InstanceID);
    float scale = (uDistance - z) / uDistance;
    vec3 p = vec3((corner.x - 0.5) * 160.0 * scale, (corner.y - 0.5) * 144.0 * scale, z);
    vUv = vec2(corner.x, 1.0 - corner.y);
    vSlice = gl_InstanceID;
    gl_Position = uViewProj * vec4(p, 1.0);
}
)";

// uMode 0 draws the plane: the top slice in its own colours and the sides darker the
// deeper they go. uMode 1 draws the sprite shadow: uImage is the sprite plane, shifted
// by uShadowShift, and uMask the plane the shadow falls on.
constexpr const char* kPlaneFragment = R"(#version 330 core
uniform sampler2D uImage;
uniform sampler2D uMask;
uniform int uMode;
uniform int uSlices;
uniform vec2 uShadowShift;
in vec2 vUv;
flat in int vSlice;
out vec4 outColor;
void main() {
    if (uMode == 1) {
        if (texture(uImage, vUv - uShadowShift).a < 0.5 || texture(uMask, vUv).a < 0.5) discard;
        outColor = vec4(0.0, 0.0, 0.0, 0.4);
        return;
    }
    vec4 c = texture(uImage, vUv);
    if (c.a < 0.5) discard;
    float shade = vSlice == 0 ? 1.0 : mix(0.7, 0.4, float(vSlice) / float(max(uSlices - 1, 1)));
    outColor = vec4(c.rgb * shade, 1.0);
}
)";

// Column-major 4x4 matrices, as OpenGL expects them.
using Mat4 = std::array<float, 16>;

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}

Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1;
    m[14] = 2 * zFar * zNear / (zNear - zFar);
    return m;
}

// Camera `distance` away along +z, looking at the origin, with the scene tipped back
// by `tilt` radians so the top of the picture moves away from the viewer.
Mat4 view(float distance, float tilt) {
    const float c = std::cos(tilt), s = std::sin(tilt);
    Mat4 m{1, 0, 0, 0, 0, c, -s, 0, 0, s, c, 0, 0, 0, -distance, 1};
    return m;
}

}  // namespace

GlTextureRenderer::GlTextureRenderer() : pixels_(kW * kH) {
    program_ = gl::buildProgram(kQuadVertex, kQuadFragment);
    gl::GenVertexArrays(1, &vao_);
    texture_ = makeTexture();
}

GlTextureRenderer::~GlTextureRenderer() {
    gl::DeleteTextures(1, &texture_);
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteProgram(program_);
}

void GlTextureRenderer::draw(const VideoFrame& frame, const Viewport& area) {
    compose(frame, pixels_.data());
    upload(texture_, pixels_.data(), options_.smooth);

    int w = area.width, h = area.height;
    if (options_.scale == ScaleMode::Integer) {
        // Largest integer scale that fits, centred, to keep pixels square and sharp.
        const int scale = std::max(1, std::min(area.width / kW, area.height / kH));
        w = kW * scale;
        h = kH * scale;
    } else if (options_.scale == ScaleMode::Fit) {
        if (area.width * kH > area.height * kW) w = area.height * kW / kH;
        else h = area.width * kH / kW;
    }
    gl::Viewport(area.x + (area.width - w) / 2, area.y + (area.height - h) / 2, w, h);
    gl::UseProgram(program_);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uImage"), 0);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, texture_);
    gl::BindVertexArray(vao_);
    gl::DrawArrays(gl::TRIANGLE_STRIP, 0, 4);
}

void FlatRenderer::compose(const VideoFrame& frame, uint32_t* out) {
    for (int y = 0; y < kH; ++y) {
        const uint32_t* src = &frame.rgba[(y + VideoFrame::kVisibleY) * VideoFrame::kWidth + VideoFrame::kVisibleX];
        std::copy(src, src + kW, out + y * kW);
    }
}

void LayerRenderer::compose(const VideoFrame& frame, uint32_t* out) {
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const int i = (y + VideoFrame::kVisibleY) * VideoFrame::kWidth + x + VideoFrame::kVisibleX;
            const uint32_t c = frame.rgba[i];
            const uint32_t luma = (((c >> 16) & 0xFF) + ((c >> 8) & 0xFF) + (c & 0xFF)) / 3 / 2 + 64;
            uint32_t r = 0, g = 0, b = 0;
            switch (frame.info[i].layer) {
                case PixelLayer::Backdrop: r = g = b = luma / 3; break;
                case PixelLayer::Background: b = luma; g = luma / 2; break;
                case PixelLayer::BackgroundPriority: g = luma; b = luma / 2; break;
                case PixelLayer::Sprite: r = luma; g = luma / 3; break;
            }
            out[y * kW + x] = 0xFF000000u | (r << 16) | (g << 8) | b;
        }
    }
}

Pseudo3DRenderer::Pseudo3DRenderer() {
    program_ = gl::buildProgram(kPlaneVertex, kPlaneFragment);
    gl::GenVertexArrays(1, &vao_);
    for (auto& t : textures_) t = makeTexture();
}

Pseudo3DRenderer::~Pseudo3DRenderer() {
    gl::DeleteTextures(kPlanes, textures_.data());
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteProgram(program_);
}

void Pseudo3DRenderer::draw(const VideoFrame& frame, const Viewport& area) {
    splitPlanes(frame, planes_);
    for (int p = 0; p < kPlanes; ++p) upload(textures_[p], planes_[p].data(), options_.smooth);

    // Heights of the planes in Game Gear pixels: sky at 0, the rest stepping forward.
    const float depth = std::clamp(options_.depth, 0.0f, 1.0f) * 48.0f;
    const float height[kPlanes] = {0.0f, 0.45f * depth, 0.75f * depth, 1.0f * depth};
    // The plane each one's sides reach down to.
    const int below[kPlanes] = {PlaneSky, PlaneSky, PlaneBackground, PlaneBackground};

    // Frame the 160x144 picture with a little margin, whichever side is tighter.
    const float aspect = static_cast<float>(area.width) / static_cast<float>(std::max(area.height, 1));
    const float fov = 30.0f * 3.14159265f / 180.0f;
    const float halfHeight = 1.08f * (aspect >= float(kW) / kH ? kH / 2.0f : kW / 2.0f / aspect);
    const float distance = halfHeight / std::tan(fov / 2);
    const float tilt = std::clamp(options_.tilt, 0.0f, 60.0f) * 3.14159265f / 180.0f;
    const Mat4 viewProj = multiply(perspective(fov, aspect, distance * 0.25f, distance * 3.0f), view(distance, tilt));

    gl::Viewport(area.x, area.y, area.width, area.height);
    gl::Enable(gl::SCISSOR_TEST);
    gl::Scissor(area.x, area.y, area.width, area.height);
    gl::ClearColor(0.05f, 0.05f, 0.07f, 1.0f);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::Disable(gl::SCISSOR_TEST);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);

    gl::UseProgram(program_);
    gl::BindVertexArray(vao_);
    gl::UniformMatrix4fv(gl::GetUniformLocation(program_, "uViewProj"), 1, gl::FALSE_, viewProj.data());
    gl::Uniform1f(gl::GetUniformLocation(program_, "uDistance"), distance);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uImage"), 0);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uMask"), 1);
    const gl::GLint top = gl::GetUniformLocation(program_, "uTop");
    const gl::GLint step = gl::GetUniformLocation(program_, "uStep");
    const gl::GLint slices = gl::GetUniformLocation(program_, "uSlices");
    const gl::GLint mode = gl::GetUniformLocation(program_, "uMode");

    gl::Uniform1i(mode, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    for (int p = 0; p < kPlanes; ++p) {
        // Sides made of slices about half a pixel apart so they read as solid.
        const float side = (height[p] - height[below[p]]) * std::clamp(options_.thickness, 0.0f, 1.0f);
        const int count = 1 + std::min(32, static_cast<int>(std::ceil(side * 2.0f)));
        gl::Uniform1f(top, height[p]);
        gl::Uniform1f(step, count > 1 ? side / static_cast<float>(count - 1) : 0.0f);
        gl::Uniform1i(slices, count);
        gl::BindTexture(gl::TEXTURE_2D, textures_[p]);
        gl::DrawArraysInstanced(gl::TRIANGLE_STRIP, 0, 4, count);
    }

    if (options_.shadows && depth > 0) {
        // The light comes from the top left, so the shadow falls down and to the right,
        // further the higher the sprites stand over the background.
        const float lift = height[PlaneSprites] - height[PlaneBackground];
        gl::Uniform1i(mode, 1);
        gl::Uniform1f(top, height[PlaneBackground] + 0.05f);
        gl::Uniform1f(step, 0.0f);
        gl::Uniform2f(gl::GetUniformLocation(program_, "uShadowShift"), 0.3f * lift / kW, 0.3f * lift / kH);
        gl::BindTexture(gl::TEXTURE_2D, textures_[PlaneSprites]);
        gl::ActiveTexture(gl::TEXTURE0 + 1);
        gl::BindTexture(gl::TEXTURE_2D, textures_[PlaneBackground]);
        gl::Enable(gl::BLEND);
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
        gl::DepthMask(gl::FALSE_);
        gl::DrawArraysInstanced(gl::TRIANGLE_STRIP, 0, 4, 1);
        gl::DepthMask(gl::TRUE_);
        gl::Disable(gl::BLEND);
        gl::ActiveTexture(gl::TEXTURE0);
    }
    gl::Disable(gl::DEPTH_TEST);
}

}  // namespace plumgg
