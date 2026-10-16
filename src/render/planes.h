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

// Writes into the alpha of every solid pixel how far it is from the plane's edge, in
// pixels (city-block distance to the nearest empty pixel or the picture border), capped
// at kMaxEdgeDistance: alpha = 255 - 16 * (kMaxEdgeDistance - distance). Empty pixels
// keep alpha 0, so anything testing alpha >= 128 still sees the same shape. The voxel
// view uses it to round off the edges of every shape.
constexpr int kMaxEdgeDistance = 4;
void markEdgeDistance(std::vector<uint32_t>& plane);

// Marks the visible background pixels (160x144, 0xFFFFFFFF or 0) that belong to solid
// tiles: tile patterns of which at most a quarter of the pixels on screen are colour 0.
// Ground, walls and platforms are usually solid; foliage, clouds and outlines are not.
// The voxel view extrudes solid tiles far back, like 3DNES does with its profiles.
void markSolidTiles(const VideoFrame& frame, std::vector<uint32_t>& solid);

}  // namespace plumgg
