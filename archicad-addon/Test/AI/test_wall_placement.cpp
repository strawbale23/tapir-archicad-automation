#include "../../Sources/WallPlacementGeometry.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace WallPlacementGeometry;
int checks = 0;
void Require (bool condition) { ++checks; if (!condition) { std::cerr << "Failed check " << checks << "\n"; std::exit (1); } }
bool Near (double a, double b) { return std::abs (a - b) < 1e-8; }
int main ()
{
    Frame f = {};
    Require (MakeFrame ({10,20}, {13,24}, f) == nullptr);
    Require (Near (f.length, 5) && Near (f.tangent.x, .6) && Near (f.tangent.y, .8));
    const auto p = AtStation (f, 2.5);
    Require (Near (p.x, 11.5) && Near (p.y, 22));
    Require (Near (f.leftNormal.x, -.8) && Near (f.leftNormal.y, .6));
    double center = -99;
    Require (ResolveOpening (5, .9, .15, false, true, center) == nullptr && Near (center, .6));
    Require (ResolveOpening (5, .9, .15, true, true, center) == nullptr && Near (center, 4.4));
    Require (ResolveOpening (5, .9, 1, true, false, center) == nullptr && Near (center, 4));
    Require (ResolveOpening (5, .9, 1, false, false, center) == nullptr && Near (center, 1));
    Require (ResolveOpening (5, 1, 0, false, true, center) == nullptr && Near (center, .5));
    Require (ResolveOpening (5, 1, 0, true, true, center) == nullptr && Near (center, 4.5));
    Require (ResolveOpening (5, 5, 0, false, true, center) == nullptr && Near (center, 2.5));
    center = -99;
    Require (ResolveOpening (5, 6, 0, false, true, center) != nullptr && center == -99);
    Require (ResolveOpening (5, 1, .2, false, false, center) != nullptr);
    Require (ResolveOpening (5, 1, 5, true, true, center) != nullptr);
    Require (ResolveOpening (5, 0, 1, false, true, center) != nullptr);
    Require (ResolveOpening (5, 1, -1, false, true, center) != nullptr);
    Require (ResolveOpening (0, 1, 0, false, true, center) != nullptr);
    Require (MakeFrame ({0,0}, {0,0}, f) != nullptr);
    const double nan = std::numeric_limits<double>::quiet_NaN ();
    const double inf = std::numeric_limits<double>::infinity ();
    Require (ResolveOpening (5, nan, 1, false, true, center) != nullptr);
    Require (ResolveOpening (inf, 1, 1, false, true, center) != nullptr);
    Require (ResolveOpening (5, 1, inf, false, true, center) != nullptr);
    Require (MakeFrame ({nan,0}, {1,1}, f) != nullptr);
    Require (MakeFrame ({-1e308,0}, {1e308,0}, f) != nullptr);
    // Reversing a host's endpoints and measuring from the opposite end
    // must give the same world placement, at many different orientations.
    for (int i = 0; i < 360; ++i) {
        const double angle = i * 3.14159265358979323846 / 180;
        Point a = {100, -75}, b = {100 + 5*std::cos(angle), -75 + 5*std::sin(angle)};
        Frame forward = {}, reverse = {};
        Require (MakeFrame (a, b, forward) == nullptr && MakeFrame (b, a, reverse) == nullptr);
        double c1 = 0, c2 = 0;
        Require (ResolveOpening (forward.length, .9, .15, false, true, c1) == nullptr);
        Require (ResolveOpening (reverse.length, .9, .15, true, true, c2) == nullptr);
        auto p1 = AtStation (forward, c1), p2 = AtStation (reverse, c2);
        Require (Near (p1.x, p2.x) && Near (p1.y, p2.y));
    }
    std::cout << checks << " geometry checks passed\n";
}

