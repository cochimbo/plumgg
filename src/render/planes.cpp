#include "planes.h"

#include <algorithm>

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

void markEdgeDistance(std::vector<uint32_t>& plane) {
    constexpr int kW = VideoFrame::kVisibleWidth;
    constexpr int kH = VideoFrame::kVisibleHeight;
    // Two-pass chamfer distance: forward from the top left, then back from the bottom right.
    std::vector<uint8_t> d(plane.size());
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const int i = y * kW + x;
            if (!(plane[i] >> 24)) continue;
            const int up = y > 0 ? d[i - kW] : 0;
            const int left = x > 0 ? d[i - 1] : 0;
            d[i] = static_cast<uint8_t>(std::min({up, left, kMaxEdgeDistance - 1}) + 1);
        }
    for (int y = kH - 1; y >= 0; --y)
        for (int x = kW - 1; x >= 0; --x) {
            const int i = y * kW + x;
            if (!d[i]) continue;
            const int down = y < kH - 1 ? d[i + kW] : 0;
            const int right = x < kW - 1 ? d[i + 1] : 0;
            d[i] = static_cast<uint8_t>(std::min<int>({d[i], down + 1, right + 1}));
            plane[i] = (plane[i] & 0x00FFFFFFu) | static_cast<uint32_t>(255 - 16 * (kMaxEdgeDistance - d[i])) << 24;
        }
}

void markSolidTiles(const VideoFrame& frame, std::vector<uint32_t>& solid) {
    constexpr int kW = VideoFrame::kVisibleWidth;
    constexpr int kH = VideoFrame::kVisibleHeight;
    std::array<uint16_t, 512> total{}, clear{};  // per tile pattern
    auto at = [&](int x, int y) { return (y + VideoFrame::kVisibleY) * VideoFrame::kWidth + x + VideoFrame::kVisibleX; };
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const PixelInfo& bg = frame.backgroundInfo[at(x, y)];
            if (bg.layer == PixelLayer::Backdrop) continue;
            ++total[bg.source & 511];
            if ((bg.colorIndex & 0x0F) == 0) ++clear[bg.source & 511];
        }
    solid.assign(kW * kH, 0);
    for (int y = 0; y < kH; ++y)
        for (int x = 0; x < kW; ++x) {
            const PixelInfo& bg = frame.backgroundInfo[at(x, y)];
            if (bg.layer == PixelLayer::Backdrop || (bg.colorIndex & 0x0F) == 0) continue;
            const int pattern = bg.source & 511;
            if (clear[pattern] * 4 <= total[pattern]) solid[y * kW + x] = 0xFFFFFFFFu;
        }
}

}  // namespace plumgg
