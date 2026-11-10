#include "analysis/performance_series.h"
#include "analysis/performance_units.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gfx::tests {
void PerformanceSeriesTests() {
    if(PerformanceScale("PERCENT/NONE",100).unit!="%" || PerformanceScale("PERCENT/NONE",100).divisor!=1 ||
       PerformanceScale("BYTE/NONE",2500000).unit!="MB" ||
       FormatPerformanceValue(2500000,PerformanceScale("BYTE/NONE",2500000))!="2.5" ||
       PerformanceScale("NONE/NONE",12).unit!="1" || PerformanceScale("NONE",12).unit!="1" ||
       PerformanceScale("NONE/PIXEL",2).unit!="1/px" ||
       PerformanceScale("FRAGMENT/PIXEL",1.25).unit!="FRAGMENT/PIXEL" ||
       PerformanceScale("BYTE*NONE/SECOND*NONE",2500).unit!="KB/s" ||
       PerformanceScale("UNKNOWN(99)/NONE",5).unit!="UNKNOWN(99)")
        throw std::runtime_error("Mali dimensionless factors obscured readable units or changed count/rate/ratio meaning");
    for(const auto& example:std::vector<std::pair<double,std::string>>{{500,"B/s"},{2500,"KB/s"},{2500000,"MB/s"},{2108770000,"GB/s"}}){
        const auto scale=PerformanceScale("BYTE/SECOND",example.first);
        if(scale.unit!=example.second||FormatPerformanceValue(example.first,scale).find('e')!=std::string::npos)
            throw std::runtime_error("Bandwidth display scale lost readable SI units");
    }
    if(FormatPerformanceValue(2108770000,PerformanceScale("BYTE/SECOND",4230650000))!="2.109" ||
       PerformanceScale("PERCENT",100).unit!="%" || PerformanceScale("ms",12).divisor!=1 ||
       PerformanceScale("MICROSECOND",2500).divisor!=1000 || PerformanceScale("MICROSECOND",2500).unit!="ms" ||
       FormatPerformanceValue(-5,PerformanceScale("CELSIUS",5))!="-5")
        throw std::runtime_error("Shared baseline/modified units changed percentages, timing or signed values");
    std::vector<double> x(100000), y(100000, 2);
    for (size_t i = 0; i < x.size(); ++i) x[i] = i * 0.001;
    y[30123] = 100000; y[60999] = -50000;
    auto reduced = ReducePerformanceSeries(x, y);
    if (reduced.x.size() > 512 || reduced.x.front() != x.front() || reduced.x.back() != x.back() ||
        *std::max_element(reduced.y.begin(), reduced.y.end()) != 100000 ||
        *std::min_element(reduced.y.begin(), reduced.y.end()) != -50000)
        throw std::runtime_error("Performance reduction lost extrema/endpoints or exceeded its budget");
    y[50000] = std::numeric_limits<double>::quiet_NaN();
    reduced = ReducePerformanceSeries(x, y);
    if (std::none_of(reduced.y.begin(), reduced.y.end(), [](double v) { return std::isnan(v); }))
        throw std::runtime_error("Performance reduction hid a sample gap");
    for (size_t i = 0; i < y.size(); i += 2) y[i] = std::numeric_limits<double>::quiet_NaN();
    reduced = ReducePerformanceSeries(x, y);
    if (!reduced.pointsOnly || reduced.x.size() > 512)
        throw std::runtime_error("Discontinuous data must use bounded scatter rendering");
    bool rejected = false;
    try { ReducePerformanceSeries({2, 1}, {0, 1}); } catch (...) { rejected = true; }
    if (!rejected) throw std::runtime_error("Unordered plot timestamps were accepted");
}
}
