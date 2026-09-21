#pragma once
#include <cmath>

// Updated 19 September 2026, 12:23 CEST. Paper-space units are metres internally.
namespace DrawingPlacementGeometry {
struct Box { double left,bottom,right,top; };
struct Point { double x,y; };
inline bool Translation (const Box& b,double horizontal,double vertical,const Point& targetMillimetres,Point& delta) {
    if (!std::isfinite (b.left) || !std::isfinite (b.bottom) || !std::isfinite (b.right) || !std::isfinite (b.top) || b.right < b.left || b.top < b.bottom ||
        !std::isfinite (targetMillimetres.x) || !std::isfinite (targetMillimetres.y) || horizontal < 0 || horizontal > 1 || vertical < 0 || vertical > 1 || !std::isfinite (horizontal) || !std::isfinite (vertical)) return false;
    delta={targetMillimetres.x/1000-(b.left*(1-horizontal)+b.right*horizontal),targetMillimetres.y/1000-(b.bottom*(1-vertical)+b.top*vertical)};
    return std::isfinite (delta.x) && std::isfinite (delta.y);
}
}
