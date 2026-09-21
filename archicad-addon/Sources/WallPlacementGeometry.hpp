#pragma once
#include <cmath>

namespace WallPlacementGeometry {
struct Point { double x; double y; };
struct Frame { Point start; Point end; Point tangent; Point leftNormal; double length; };

inline const char* MakeFrame (Point start, Point end, Frame& frame)
{
    if (!std::isfinite (start.x) || !std::isfinite (start.y) ||
        !std::isfinite (end.x) || !std::isfinite (end.y))
        return "Wall coordinates must be finite.";
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    const double length = std::hypot (dx, dy);
    if (!std::isfinite (length) || length <= 1e-9)
        return "Wall reference line has zero or invalid length.";
    frame = {start, end, {dx / length, dy / length}, {-dy / length, dx / length}, length};
    return nullptr;
}

inline const char* ResolveOpening (double length, double width, double distance,
                                  bool fromEnd, bool nearestJamb, double& center)
{
    if (!std::isfinite (length) || !std::isfinite (width) || !std::isfinite (distance) ||
        length <= 1e-9 || width <= 0.0 || distance < 0.0)
        return "Wall length and opening width must be positive; distance must be non-negative and finite.";
    const double station = distance + (nearestJamb ? width / 2.0 : 0.0);
    const double result = fromEnd ? length - station : station;
    if (!std::isfinite (result) || result - width / 2.0 < -1e-9 ||
        result + width / 2.0 > length + 1e-9)
        return "The opening width extends beyond the wall reference-line endpoints.";
    center = result;
    return nullptr;
}

inline Point AtStation (const Frame& frame, double station)
{
    return {frame.start.x + frame.tangent.x * station,
            frame.start.y + frame.tangent.y * station};
}
}

