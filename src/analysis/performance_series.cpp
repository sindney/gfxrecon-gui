#include "analysis/performance_series.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace gfx {
namespace {
void ReduceSegment(const std::vector<double>& x, const std::vector<double>& y,
                   size_t first, size_t end, size_t budget, PerformanceSeries& out) {
    const auto append = [&](size_t i) { out.x.push_back(x[i]); out.y.push_back(y[i]); };
    if (end - first <= budget) { for (size_t i = first; i < end; ++i) append(i); return; }
    append(first);
    const size_t buckets = std::max<size_t>(1, (budget - 2) / 2), interior = end - first - 2;
    for (size_t b = 0; b < buckets; ++b) {
        const auto begin = first + 1 + interior * b / buckets;
        const auto stop = first + 1 + interior * (b + 1) / buckets;
        if (begin >= stop) continue;
        auto low = begin, high = begin;
        for (size_t i = begin + 1; i < stop; ++i) {
            if (y[i] < y[low]) low = i;
            if (y[i] > y[high]) high = i;
        }
        append(std::min(low, high));
        if (low != high) append(std::max(low, high));
    }
    append(end - 1);
}
}
PerformanceSeries ReducePerformanceSeries(const std::vector<double>& x,
                                         const std::vector<double>& y, size_t limit) {
    if (x.size() != y.size() || limit < 8) throw std::runtime_error("Invalid performance plot series");
    PerformanceSeries out; out.sourcePoints = x.size();
    std::vector<std::pair<size_t, size_t>> segments;
    size_t valid = 0;
    double previous = -std::numeric_limits<double>::infinity();
    for (size_t i = 0; i < x.size();) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i])) { ++i; continue; }
        const auto first = i;
        for (; i < x.size() && std::isfinite(x[i]) && std::isfinite(y[i]); ++i) {
            if (x[i] < previous) throw std::runtime_error("Performance plot times are not ordered");
            previous = x[i]; ++valid;
        }
        segments.emplace_back(first, i);
    }
    if (!valid) return out;
    if (segments.size() * 5 > limit) {
        // Too many discontinuities to represent under the renderer budget.
        // Scatter mode never invents connections across the omitted gaps.
        std::vector<double> cleanX, cleanY;
        cleanX.reserve(valid); cleanY.reserve(valid);
        for (const auto& segment : segments) for (auto i = segment.first; i < segment.second; ++i) {
            cleanX.push_back(x[i]); cleanY.push_back(y[i]);
        }
        out.pointsOnly = true;
        ReduceSegment(cleanX, cleanY, 0, valid, limit, out);
        return out;
    }
    size_t remaining = limit - (segments.size() - 1), remainingValid = valid;
    for (size_t s = 0; s < segments.size(); ++s) {
        const auto first = segments[s].first, end = segments[s].second, count = end - first;
        const auto reserve = (segments.size() - s - 1) * 4;
        const auto budget = std::min(count, std::min(remaining - reserve,
            std::max<size_t>(4, remaining * count / remainingValid)));
        const auto before = out.x.size();
        ReduceSegment(x, y, first, end, budget, out);
        remaining -= out.x.size() - before; remainingValid -= count;
        if (s + 1 < segments.size()) {
            out.x.push_back(x[end - 1]); out.y.push_back(std::numeric_limits<double>::quiet_NaN());
        }
    }
    return out;
}
}
