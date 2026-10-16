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

// One block per pixel of a plane, drawn instanced: instance i is pixel (i % 160,
// i / 160) and its 54 vertices are the front face plus two segments of each side (the
// back face is never seen: the camera stays in front). Blocks rise from uBottom; how
// far towards uTop depends on how far the pixel is from the shape's edge
// (markEdgeDistance() stores it in alpha), so every shape is domed, and the front face
// is lit by the slope of that dome so it reads as rounded rather than stepped. Pixels
// of solid tiles (uSolid) reach a further uExtrude back, like 3DNES's extruded ground.
// A side shows only what its neighbour does not cover: the upper segment is a step up
// from a lower neighbour (lit like the front so it blends into the dome, or as a wall
// at the shape's outline), the lower one the part of a deeper block that reaches past
// a shallower neighbour. Empty pixels and hidden segments collapse to a point. The
// whole plane is scaled by uScale so its front faces line up with the 2D picture when
// the camera looks straight at it.
constexpr const char* kVoxelVertex = R"(#version 330 core
uniform mat4 uViewProj;
uniform sampler2D uImage;
uniform sampler2D uSolid;
uniform float uTop;
uniform float uBottom;
uniform float uExtrude;
uniform float uScale;
out vec3 vColor;

// 0 for an empty pixel, else 1..4 from the edge inwards.
int level(ivec2 p) {
    if (p.x < 0 || p.y < 0 || p.x >= 160 || p.y >= 144) return 0;
    float a = texelFetch(uImage, p, 0).a * 255.0;
    return a < 128.0 ? 0 : 4 - int(round((255.0 - a) / 16.0));
}

// Height of a block's front face: a rounded profile from the edge to the full height.
float height(int level) {
    return level == 0 ? uBottom : mix(uBottom, uTop, sqrt(float(level) / 4.0));
}

float bottom(ivec2 p) {
    return texelFetch(uSolid, p, 0).r > 0.5 ? uBottom - uExtrude : uBottom;
}

void main() {
    ivec2 pixel = ivec2(gl_InstanceID % 160, gl_InstanceID / 160);
    int face = gl_VertexID / 6;
    int k = int[6](0, 1, 2, 2, 1, 3)[gl_VertexID % 6];
    vec2 c = vec2(k & 1, k >> 1);
    // Sides: 0 left, 1 right, 2 bottom, 3 top of the picture.
    ivec2 next[4] = ivec2[4](ivec2(-1, 0), ivec2(1, 0), ivec2(0, 1), ivec2(0, -1));
    vec3 normals[4] = vec3[4](vec3(-1, 0, 0), vec3(1, 0, 0), vec3(0, -1, 0), vec3(0, 1, 0));
    int side = (face - 1) % 4;
    bool lower = face > 4;

    int own = level(pixel);
    float top = height(own), base = bottom(pixel);
    float from = top, to = top;  // the z range a side segment covers
    bool wall = true;
    if (face > 0) {
        ivec2 n = pixel + next[side];
        int other = level(n);
        if (other == 0) {
            if (!lower) from = base;
        } else if (!lower) {
            from = max(height(other), base);
            wall = false;
        } else {
            to = min(bottom(n), top);
            from = base;
        }
    }
    if (own == 0 || (face > 0 && from >= to)) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColor = vec3(0.0);
        return;
    }

    float x0 = float(pixel.x) - 80.0, y0 = 71.0 - float(pixel.y);  // bottom-left corner
    float z = mix(from, to, c.y);
    vec3 p;
    if (face == 0) p = vec3(x0 + c.x, y0 + c.y, top);
    else if (side == 0) p = vec3(x0, y0 + c.x, z);
    else if (side == 1) p = vec3(x0 + 1.0, y0 + c.x, z);
    else if (side == 2) p = vec3(x0 + c.x, y0, z);
    else p = vec3(x0 + c.x, y0 + 1.0, z);

    vec3 normal = face == 0 ? vec3(0, 0, 1) : normals[side];
    if (face == 0 || !wall) {
        float dx = height(level(pixel + ivec2(1, 0))) - height(level(pixel - ivec2(1, 0)));
        float dy = height(level(pixel - ivec2(0, 1))) - height(level(pixel + ivec2(0, 1)));
        normal = normalize(vec3(-0.5 * dx, -0.5 * dy, 1.0));
    }
    // Light from the top left, in front; a flat front face keeps the true colours. Walls
    // also darken the further back they reach.
    const vec3 light = normalize(vec3(-0.5, 0.6, 0.8));
    float shade = (0.4 + 0.6 * max(dot(normal, light), 0.0)) / (0.4 + 0.6 * light.z);
    shade *= 1.0 - 0.45 * clamp((top - z) / 48.0, 0.0, 1.0);
    vColor = texelFetch(uImage, pixel, 0).rgb * min(shade, 1.1);
    gl_Position = uViewProj * vec4(p.xy * uScale, p.z, 1.0);
}
)";

constexpr const char* kVoxelFragment = R"(#version 330 core
in vec3 vColor;
out vec4 outColor;
void main() { outColor = vec4(vColor, 1.0); }
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

// The camera orbits `pivot` (a point on the z axis) at `distance`: the scene turns by
// `yaw` radians around its vertical axis, then tips back by `tilt` so the top of the
// picture moves away from the viewer.
Mat4 view(float distance, float tilt, float yaw, float pivot) {
    const float ct = std::cos(tilt), st = std::sin(tilt), cy = std::cos(yaw), sy = std::sin(yaw);
    const Mat4 translate{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -distance, 1};
    const Mat4 tip{1, 0, 0, 0, 0, ct, -st, 0, 0, st, ct, 0, 0, 0, 0, 1};
    const Mat4 turn{cy, 0, -sy, 0, 0, 1, 0, 0, sy, 0, cy, 0, 0, 0, 0, 1};
    const Mat4 toPivot{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -pivot, 1};
    return multiply(translate, multiply(tip, multiply(turn, toPivot)));
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
    voxelProgram_ = gl::buildProgram(kVoxelVertex, kVoxelFragment);
    gl::GenVertexArrays(1, &vao_);
    for (auto& t : textures_) t = makeTexture();
    solidTexture_ = makeTexture();
}

Pseudo3DRenderer::~Pseudo3DRenderer() {
    gl::DeleteTextures(kPlanes, textures_.data());
    gl::DeleteTextures(1, &solidTexture_);
    gl::DeleteVertexArrays(1, &vao_);
    gl::DeleteProgram(voxelProgram_);
    gl::DeleteProgram(program_);
}

void Pseudo3DRenderer::draw(const VideoFrame& frame, const Viewport& area) {
    splitPlanes(frame, planes_);
    const bool voxels = options_.style == Style3D::Voxels;
    if (voxels) {
        for (int p = PlaneBackground; p < kPlanes; ++p) markEdgeDistance(planes_[p]);
        markSolidTiles(frame, solid_);
        upload(solidTexture_, solid_.data(), false);
    }
    for (int p = 0; p < kPlanes; ++p) upload(textures_[p], planes_[p].data(), options_.smooth);

    // Heights of the planes in Game Gear pixels: sky at 0, the rest stepping forward.
    const float depth = std::clamp(options_.depth, 0.0f, 1.0f) * 48.0f;
    // Voxels push the sky back behind the extruded tiles.
    const float extrude = voxels ? std::clamp(options_.extrusion, 0.0f, 1.0f) * 96.0f : 0.0f;
    const float height[kPlanes] = {voxels ? -extrude - 2.0f : 0.0f, 0.45f * depth, 0.75f * depth, 1.0f * depth};
    // The plane each one's sides reach down to.
    const int below[kPlanes] = {PlaneSky, PlaneSky, PlaneBackground, PlaneBackground};

    // Frame the 160x144 picture with a little margin, whichever side is tighter.
    const float aspect = static_cast<float>(area.width) / static_cast<float>(std::max(area.height, 1));
    const float fov = 30.0f * 3.14159265f / 180.0f;
    const float halfHeight = 1.08f * (aspect >= float(kW) / kH ? kH / 2.0f : kW / 2.0f / aspect);
    const float radians = 3.14159265f / 180.0f;
    const float distance = halfHeight / std::tan(fov / 2) / std::clamp(options_.zoom, kMinZoom, kMaxZoom);
    const float tilt = std::clamp(options_.tilt, kMinTilt, kMaxTilt) * radians;
    const float yaw = std::clamp(options_.yaw, -kMaxYaw, kMaxYaw) * radians;
    // Orbit the middle of the background's blocks. Planes are scaled as seen from where the
    // camera sits when it looks straight on, `eye` along the z axis.
    const float pivot = 0.5f * (height[PlaneBackground] - extrude);
    const float eye = distance + pivot;
    const Mat4 viewProj = multiply(perspective(fov, aspect, distance * 0.1f, distance * 3.0f + 2.0f * extrude),
                                   view(distance, tilt, yaw, pivot));

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
    gl::Uniform1f(gl::GetUniformLocation(program_, "uDistance"), eye);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uImage"), 0);
    gl::Uniform1i(gl::GetUniformLocation(program_, "uMask"), 1);
    const gl::GLint top = gl::GetUniformLocation(program_, "uTop");
    const gl::GLint step = gl::GetUniformLocation(program_, "uStep");
    const gl::GLint slices = gl::GetUniformLocation(program_, "uSlices");
    const gl::GLint mode = gl::GetUniformLocation(program_, "uMode");

    gl::Uniform1i(mode, 0);
    gl::ActiveTexture(gl::TEXTURE0);
    const float thickness = std::clamp(options_.thickness, 0.0f, 1.0f);
    // In the voxels style only the sky is a flat plane; the rest are drawn as blocks below.
    for (int p = 0; p < (voxels ? 1 : int(kPlanes)); ++p) {
        // Sides made of slices about half a pixel apart so they read as solid.
        const float side = (height[p] - height[below[p]]) * thickness;
        const int count = 1 + std::min(32, static_cast<int>(std::ceil(side * 2.0f)));
        gl::Uniform1f(top, height[p]);
        gl::Uniform1f(step, count > 1 ? side / static_cast<float>(count - 1) : 0.0f);
        gl::Uniform1i(slices, count);
        gl::BindTexture(gl::TEXTURE_2D, textures_[p]);
        gl::DrawArraysInstanced(gl::TRIANGLE_STRIP, 0, 4, count);
    }
    if (voxels) {
        gl::UseProgram(voxelProgram_);
        gl::UniformMatrix4fv(gl::GetUniformLocation(voxelProgram_, "uViewProj"), 1, gl::FALSE_, viewProj.data());
        gl::Uniform1i(gl::GetUniformLocation(voxelProgram_, "uImage"), 0);
        gl::Uniform1i(gl::GetUniformLocation(voxelProgram_, "uSolid"), 1);
        gl::ActiveTexture(gl::TEXTURE0 + 1);
        gl::BindTexture(gl::TEXTURE_2D, solidTexture_);
        gl::ActiveTexture(gl::TEXTURE0);
        for (int p = PlaneBackground; p < kPlanes; ++p) {
            // Blocks are at least a pixel deep, so they still read as blocks at zero thickness.
            // Sprites only take their own thickness; solid tiles also reach back by `extrude`.
            const float floor = below[p] == PlaneSky ? 0.0f : height[below[p]];  // not the pushed-back sky
            const float side = std::max(1.0f, (height[p] - floor) * thickness);
            gl::Uniform1f(gl::GetUniformLocation(voxelProgram_, "uTop"), height[p]);
            gl::Uniform1f(gl::GetUniformLocation(voxelProgram_, "uBottom"), height[p] - side);
            gl::Uniform1f(gl::GetUniformLocation(voxelProgram_, "uExtrude"), p == PlaneSprites ? 0.0f : extrude);
            gl::Uniform1f(gl::GetUniformLocation(voxelProgram_, "uScale"), (eye - height[p]) / eye);
            gl::BindTexture(gl::TEXTURE_2D, textures_[p]);
            gl::DrawArraysInstanced(gl::TRIANGLES, 0, 54, kW * kH);
        }
        gl::UseProgram(program_);
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
