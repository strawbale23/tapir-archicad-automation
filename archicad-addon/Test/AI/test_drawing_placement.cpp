// Updated 19 September 2026, 12:29 CEST. Actual paper-space placement arithmetic.
#include "../../Sources/DrawingPlacementGeometry.hpp"
#include <cassert>
#include <cmath>
#include <limits>
using namespace DrawingPlacementGeometry;
int main () {
    Point d = {};
    const Box frame {-.1,.02,.3,.12};
    assert (Translation (frame,0,0,{20,30},d));
    assert (std::abs (d.x-.12)<1e-12 && std::abs (d.y-.01)<1e-12);
    assert (Translation (frame,1,1,{400,200},d));
    assert (std::abs (d.x-.1)<1e-12 && std::abs (d.y-.08)<1e-12);
    assert (Translation (frame,.5,.5,{100,70},d));
    assert (std::abs (d.x)<1e-12 && std::abs (d.y)<1e-12);
    assert (!Translation ({1,0,0,1},0,0,{0,0},d));
    assert (!Translation (frame,2,0,{0,0},d));
    assert (!Translation (frame,0,0,{std::numeric_limits<double>::infinity (),0},d));
    assert (!Translation (frame,std::numeric_limits<double>::quiet_NaN (),0,{0,0},d));
}
