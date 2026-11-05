#pragma once
#include <cstddef>
#include <vector>

namespace gfx {
struct PerformanceSeries {
    std::vector<double> x, y;
    size_t sourcePoints = 0;
    bool pointsOnly = false;
};
// Display-only reduction. Raw artifacts remain untouched. NaN separates gaps;
// extrema and segment endpoints survive while line rendering stays bounded.
PerformanceSeries ReducePerformanceSeries(const std::vector<double>& x,
                                         const std::vector<double>& y,
                                         size_t limit = 512);
}
