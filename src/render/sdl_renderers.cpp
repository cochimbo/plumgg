#include "sdl_renderers.h"

#include <algorithm>

namespace plumgg {

namespace {
constexpr int kW = VideoFrame::kVisibleWidth;
constexpr int kH = VideoFrame::kVisibleHeight;
}  // namespace

SdlTextureRenderer::SdlTextureRenderer(SDL_Renderer* sdl) : sdl_(sdl), pixels_(kW * kH) {
    texture_ = SDL_CreateTexture(sdl_, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, kW, kH);
}

SdlTextureRenderer::~SdlTextureRenderer() {
    if (texture_) SDL_DestroyTexture(texture_);
}

void SdlTextureRenderer::draw(const VideoFrame& frame, int windowWidth, int windowHeight) {
    compose(frame, pixels_.data());
    SDL_UpdateTexture(texture_, nullptr, pixels_.data(), kW * static_cast<int>(sizeof(uint32_t)));

    SDL_SetTextureScaleMode(texture_, options_.smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
    int w = windowWidth, h = windowHeight;
    if (options_.scale == ScaleMode::Integer) {
        // Largest integer scale that fits, centred, to keep pixels square and sharp.
        const int scale = std::max(1, std::min(windowWidth / kW, windowHeight / kH));
        w = kW * scale;
        h = kH * scale;
    } else if (options_.scale == ScaleMode::Fit) {
        if (windowWidth * kH > windowHeight * kW) w = windowHeight * kW / kH;
        else h = windowWidth * kH / kW;
    }
    SDL_Rect dst{(windowWidth - w) / 2, (windowHeight - h) / 2, w, h};
    SDL_SetRenderDrawColor(sdl_, 0, 0, 0, 255);
    SDL_RenderClear(sdl_);
    SDL_RenderCopy(sdl_, texture_, nullptr, &dst);
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

}  // namespace plumgg
