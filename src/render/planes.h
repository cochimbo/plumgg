#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "../core/video_frame.h"

namespace plumgg {

// The planes the pseudo-3D view stands apart, back to front.
enum Plane { PlaneSky, PlaneBackground, PlaneSprites, PlanePriority, kPlanes };

using Planes = std::array<std::vector<uint32_t>, kPlanes>;

// Splits the visible 160x144 window of the frame into planes (0xAARRGGBB, alpha 0
// where a plane is empty). The sky is always full: background pixels drawn with colour
// 0 of their palette (the usual "transparent" colour, often the sky) and the backdrop,
// with the gaps behind solid tiles filled from the nearest sky pixel on the line.
// Background holds the other background tiles, Priority the tiles flagged to cover
// sprites, and Sprites every sprite pixel, including those that priority tiles hide.
void splitPlanes(const VideoFrame& frame, Planes& planes);

}  // namespace plumgg
