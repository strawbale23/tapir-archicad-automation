#pragma once
#include <cmath>

namespace TapirTransformGeometry {
// Left-multiply a row-major 3x4 affine matrix by a project-origin Z rotation.
// Translation is in the final column; the Z row must remain unchanged.
inline void RotateAboutProjectZ (double (&matrix)[12], double radians)
{
    const double cosine = std::cos (radians), sine = std::sin (radians);
    for (int column = 0; column < 4; ++column) {
        const double x = matrix[column], y = matrix[4 + column];
        matrix[column] = cosine * x - sine * y;
        matrix[4 + column] = sine * x + cosine * y;
    }
}
}
