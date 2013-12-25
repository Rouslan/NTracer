#include "simd.hpp"

namespace simd {
    constexpr size_t v_sizes<double>::value[];
    constexpr size_t v_sizes<float>::value[];
    constexpr size_t v_sizes<int64_t>::value[];
    constexpr size_t v_sizes<int32_t>::value[];
    constexpr size_t v_sizes<int16_t>::value[];
    constexpr size_t v_sizes<int8_t>::value[];
}
