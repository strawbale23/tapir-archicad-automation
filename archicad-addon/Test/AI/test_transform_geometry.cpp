#include "../../Sources/TransformGeometry.hpp"
#include <cmath>
#include <cstdlib>
#include <iostream>

void Require (bool pass) { if (!pass) { std::cerr << "Z rotation invariant failed\n"; std::exit (1); } }
bool Near (double a, double b) { return std::abs (a - b) < 1e-10; }
int main ()
{
    const double pi = std::acos (-1.0);
    double matrix[12] = {1,0,0,3, 0,1,0,4, 0,0,1,7};
    TapirTransformGeometry::RotateAboutProjectZ (matrix, pi / 2);
    Require (Near (matrix[3], -4) && Near (matrix[7], 3) && Near (matrix[11], 7));
    Require (Near (matrix[0], 0) && Near (matrix[4], 1));
    Require (Near (matrix[1], -1) && Near (matrix[5], 0));
    Require (Near (matrix[8], 0) && Near (matrix[9], 0) && Near (matrix[10], 1));
    TapirTransformGeometry::RotateAboutProjectZ (matrix, -pi / 2);
    const double original[12] = {1,0,0,3, 0,1,0,4, 0,0,1,7};
    for (int i = 0; i < 12; ++i) Require (Near (matrix[i], original[i]));
    double tilted[12] = {2,.3,.5,10, -.2,3,.4,-5, .7,.8,4,12};
    const double oldZ[4] = {.7,.8,4,12};
    const double oldDistance = std::hypot (tilted[3], tilted[7]);
    TapirTransformGeometry::RotateAboutProjectZ (tilted, .713);
    for (int i = 0; i < 4; ++i) Require (tilted[8 + i] == oldZ[i]);
    Require (Near (std::hypot (tilted[3], tilted[7]), oldDistance));
    std::cout << "Project Z rotation invariants passed\n";
}
