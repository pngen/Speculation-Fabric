// Speculation Fabric — clock implementation.
#include "speculation_fabric/core/clock.hpp"

namespace speculation_fabric {

std::string format_ns(std::uint64_t ns) {
    if (ns < 1000) return std::to_string(ns) + "ns";
    if (ns < 1000000) return std::to_string(ns / 1000) + "us";
    if (ns < 1000000000ULL) return std::to_string(ns / 1000000) + "ms";
    return std::to_string(ns / 1000000000ULL) + "s";
}

}  // namespace speculation_fabric
