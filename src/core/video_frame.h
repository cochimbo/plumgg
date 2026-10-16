#pragma once

#include <array>
#include <cstdint>

namespace plumgg {

// Which hardware layer produced a pixel. A pseudo-3D renderer can use this to
// push each layer to a different depth instead of flattening them.
enum class PixelLayer : uint8_t {
    Backdrop = 0,
    Background = 1,
    BackgroundPriority = 2,  // background tile flagged to be drawn over sprites
    Sprite = 3,
};

struct PixelInfo {
    uint8_t colorIndex;  // 0..31 into the CRAM palette (16..31 = sprite palette)
    PixelLayer layer;
    uint16_t source;     // tile pattern index for background, sprite number for sprites
};

// Everything the VDP produced for one frame. It is plain data: the VDP fills it and
// any renderer (flat 2D now, pseudo-3D later) reads it without touching the VDP.
struct VideoFrame {
    static constexpr int kWidth = 256;   // full Master System mode 4 raster
    static constexpr int kHeight = 192;
    // The Game Gear LCD only shows a 160x144 window of that raster.
    static constexpr int kVisibleX = 48;
    static constexpr int kVisibleY = 24;
    static constexpr int kVisibleWidth = 160;
    static constexpr int kVisibleHeight = 144;

    std::array<uint32_t, kWidth * kHeight> rgba{};  // final colour, 0xAARRGGBB
    std::array<PixelInfo, kWidth * kHeight> info{};

    // The two planes before the VDP combined them, so a renderer can pull them apart
    // without holes: what lies under a sprite, and the parts of sprites that priority
    // tiles hide.
    std::array<uint32_t, kWidth * kHeight> backgroundRgba{};
    std::array<PixelInfo, kWidth * kHeight> backgroundInfo{};  // layer is never Sprite
    std::array<uint32_t, kWidth * kHeight> spriteRgba{};       // alpha 0 where no sprite

    struct LineState {
        uint8_t scrollX;
        uint8_t scrollY;
        bool displayEnabled;
    };
    std::array<LineState, kHeight> lines{};

    uint64_t frameNumber = 0;
};

}  // namespace plumgg
