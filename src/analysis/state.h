#pragma once
#include "analysis/capture.h"
#include <unordered_map>

namespace gfx {
class StateTracker {
public:
    explicit StateTracker(Capture& capture) : capture_(capture) {}
    void Observe(Event& event, const Json& arguments, bool successful);
    void Resolve(Execution& execution, const Event& event);
private:
    Json& Mutable(uint64_t cb);
    Capture& capture_;
    std::unordered_map<uint64_t, std::shared_ptr<Json>> states_;
    std::unordered_map<uint64_t, uint64_t> pools_;
    std::map<uint64_t, Json> descriptorLayouts_, descriptorSets_;
    uint64_t descriptorVersion_ = 0;
};
} // namespace gfx
