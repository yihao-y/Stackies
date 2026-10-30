#pragma once
#include <algorithm>
#include <cmath>

// Broad diagonal surface-light band from the Qt glass lab.
// Normalized coordinates make the appearance independent of panel size.
// The surrounding glass stays clear, with no solid background fill.
namespace sticky::glass {
struct ReflectionLayout {
    double centerTop=0.40;
    double centerDrift=0.34;
    double sigma=0.13;
    double strength=0.19;

    double center(double y) const { return centerTop+centerDrift*y; }
};

inline ReflectionLayout reflectionLayout(double /*width*/,double /*height*/){
    return {};
}

inline double reflectionOpacity(double x,double y,const ReflectionLayout &layout){
    if(x<0.0||x>1.0||y<0.0||y>1.0)return 0.0;
    const double distance=(x-layout.center(y))/layout.sigma;
    return layout.strength*std::exp(-0.5*distance*distance)
           *(0.98-0.08*y);
}

} // namespace sticky::glass
