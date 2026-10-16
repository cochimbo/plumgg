#include "planes.h"

namespace plumgg {

void splitPlanes(const VideoFrame& frame, Planes& planes) {
    constexpr int kW = VideoFrame::kVisibleWidth;
    constexpr int kH = VideoFrame::kVisibleHeight;
    for (auto& p : planes) p.assign(kW * kH, 0);

    for (int y = 0; y < kH; ++y) {
        uint32_t* sky = &planes[PlaneSky][y * kW];
        int firstSky = -1;
        for (int x = 0; x < kW; ++x) {
            const int i = (y + VideoFrame::kVisibleY) * VideoFrame::kWidth + x + VideoFrame::kVisibleX;
            const int o = y * kW + x;
            const PixelInfo& bg = frame.backgroundInfo[i];
            const uint32_t color = frame.backgroundRgba[i];
            const bool transparent = bg.layer == PixelLayer::Backdrop || (bg.colorIndex & 0x0F) == 0;
            if (transparent) {
                if (firstSky < 0) firstSky = x;
                sky[x] = color;
            } else if (bg.layer == PixelLayer::BackgroundPriority) {
                planes[PlanePriority][o] = color;
            } else {
                planes[PlaneBackground][o] = color;
            }
            planes[PlaneSprites][o] = frame.spriteRgba[i];
        }
        // Fill the sky behind solid tiles from the nearest sky pixel to the left (the
        // first one for the start of the line). A line with no sky at all, like solid
        // ground, keeps its own tiles, so tilting never shows sky below the ground.
        if (firstSky < 0) {
            for (int x = 0; x < kW; ++x)
                sky[x] = frame.backgroundRgba[(y + VideoFrame::kVisibleY) * VideoFrame::kWidth + x + VideoFrame::kVisibleX];
            continue;
        }
        uint32_t fill = sky[firstSky];
        for (int x = 0; x < kW; ++x) {
            if (sky[x]) fill = sky[x];
            else sky[x] = fill;
        }
    }
}

}  // namespace plumgg
