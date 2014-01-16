#include "simd.hpp"

namespace simd {
    /*namespace impl {
        template<typename T> constexpr typename negation_mask_item<T>::type negation_mask<T>::value[];
        
        template struct negation_mask<double>;
        template struct negation_mask<float>;
        template struct negation_mask<int64_t>;
        template struct negation_mask<int32_t>;
        template struct negation_mask<int16_t>;
        template struct negation_mask<int8_t>;
    }*/
    
    constexpr size_t v_sizes<double>::value[];
    constexpr size_t v_sizes<float>::value[];
    constexpr size_t v_sizes<int64_t>::value[];
    constexpr size_t v_sizes<int32_t>::value[];
    constexpr size_t v_sizes<int16_t>::value[];
    constexpr size_t v_sizes<int8_t>::value[];
}
