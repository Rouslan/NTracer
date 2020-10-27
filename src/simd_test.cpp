
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <algorithm>
#include <numeric>

#include "simd.hpp"


#define MUST_EQUAL(X,F) must_equal(X,[=](int i) { return F; },__LINE__)
#define MUST_EQUAL_BARE(A,B) must_equal(A,B,__LINE__)

template<typename T> const T rand_a[64] = {
    -52,  6,108, -16,-48, 97, 80, 14, 26, -5, 105,-18,-20, -7, 12,-107,
    -82, 10, 75, -99, 89, 42, 83, 85,-53,-83,  77,111, 53, 25,-66,   3,
     68,118, 35, -54,-95, 37,-32, 41,-10,116,-109,-41,-29, 33, 23, -60,
     72,-17, 49,-118,-47, 81,-36, 69, 99, -6, -40, 24, 13,-69,-102, -2};

template<typename T> const T rand_b[64] = {
      1,-61,-112, 90, 71,-86, 96, 56,-11,  73, -82,  6,-75,  35, 52, 24,
    -84,-36,  20,-34, 89,-87, 50,119,112,  25,  95,106,  5,  98, 80,-39,
    -90, 65,   2,-56,-28, -5, 14,-41,104,   8, -40, 83,-63, 107,-19, 84,
     16,-64, 110,-59, -6,  0, 92,-31,116,-101,-109, 34,-60,-118,-85, 33};

template<typename T> bool is_equal(T a,T b) { return a == b; }
template<> bool is_equal<double>(double a,double b) { return std::abs(a - b) < 0.001; }
template<> bool is_equal<float>(float a,float b) { return is_equal<double>(a,b); }

template<typename T,size_t Size> struct tester {
    using v_type = simd::v_type<T,Size>;
    using v_mask = typename v_type::mask;

    template<typename F> static v_type make_v_type(F f) {
        v_type r;
        for(unsigned int i=0; i<Size; ++i) r[i] = static_cast<T>(f(i));
        return r;
    }

    static void must_equal(T a,T b,unsigned int line) {
        if(!is_equal(a,b)) {
            std::fprintf(stderr,"test failed on line %u: %f != %f\n",line,double(a),double(b));
            std::exit(EXIT_FAILURE);
        }
    }

    template<typename F> static void must_equal(v_type x,F f,unsigned int line) {
        for(unsigned int i=0; i<Size; ++i) {
            if(!is_equal(x[i],static_cast<T>(f(i)))) {
                std::fprintf(stderr,"test failed on line %u, element %u: %f != %f\n",line,i,double(x[i]),double(f(i)));
                std::exit(EXIT_FAILURE);
            }
        }
    }

    template<typename F> static void must_equal(v_mask x,F f,unsigned int line) {
        auto bits = x.to_bits();
        for(unsigned int i=0; i<Size; ++i) {
            if(((bits & 1) != 0) != static_cast<bool>(f(i))) {
                std::fprintf(
                    stderr,
                    "test failed on line %u, element %u: %u != %u\n",
                    line,
                    i,
                    static_cast<unsigned int>(bits & 1),
                    f(i) ? 1 : 0);
                std::exit(EXIT_FAILURE);
            }
            bits >>= v_mask::bit_granularity;
        }
    }

    static void tests() {
        const T *ra = rand_a<T>;
        const T *rb = rand_b<T>;

        MUST_EQUAL(v_type::zeros(), T(0));
        MUST_EQUAL(v_type::repeat(3), T(3));

        v_type a = make_v_type([=](int i) { return ra[i]; });
        MUST_EQUAL(    a, ra[i]);
        v_type b = make_v_type([=](int i) { return rb[i]; });
        MUST_EQUAL(a + b, ra[i] + rb[i]);
        MUST_EQUAL(a - b, ra[i] - rb[i]);

        MUST_EQUAL(a == v_type::repeat(108), ra[i] == T(108));
        MUST_EQUAL(b != v_type::repeat(-61), rb[i] != T(-61));
        MUST_EQUAL(a > b, ra[i] > rb[i]);
        MUST_EQUAL(b >= v_type::repeat(-61), rb[i] >= T(-61));
        MUST_EQUAL(a < b, ra[i] < rb[i]);
        MUST_EQUAL(a <= v_type::repeat(6), ra[i] <= T(6));

        MUST_EQUAL(!(a < b), ra[i] >= rb[i]);
        MUST_EQUAL(a > v_type::zeros() && b < v_type::zeros(), ra[i] > 0 && rb[i] < 0);
        MUST_EQUAL(a > v_type::zeros() || b < v_type::zeros(), ra[i] > 0 || rb[i] < 0);
        MUST_EQUAL(simd::l_andn(a > v_type::zeros(),b < v_type::zeros()), ra[i] <= 0 && rb[i] < 0);
        MUST_EQUAL(simd::l_xor(a > v_type::zeros(),b < v_type::zeros()), (ra[i] > 0) != (rb[i] < 0));
        MUST_EQUAL(simd::l_xnor(a > v_type::zeros(),b < v_type::zeros()), (ra[i] > 0) == (rb[i] < 0));

        MUST_EQUAL(simd::apply([](auto x) { return x/2; },a), ra[i] / 2);
        MUST_EQUAL(simd::apply([](auto x,auto y) { return (x+y)/2; },a,b), (ra[i] + rb[i]) / 2);
        auto op1 = [](auto x,auto y) { return x*y/2; };
        MUST_EQUAL_BARE(simd::reduce(op1,a), std::accumulate(ra+1,ra+Size,ra[0],op1));
        MUST_EQUAL_BARE(simd::reduce_add(a), std::accumulate(ra+1,ra+Size,ra[0]));
        MUST_EQUAL_BARE(simd::reduce_max(a), std::accumulate(ra+1,ra+Size,ra[0],[](auto x,auto y) { return std::max(x,y); }));
        MUST_EQUAL_BARE(simd::reduce_min(a), std::accumulate(ra+1,ra+Size,ra[0],[](auto x,auto y) { return std::min(x,y); }));

        MUST_EQUAL(simd::max(a,b), std::max(ra[i],rb[i]));
        MUST_EQUAL(simd::min(a,b), std::min(ra[i],rb[i]));
        MUST_EQUAL(simd::abs(a), std::abs(ra[i]));

        if constexpr(std::is_floating_point_v<T>) {
            MUST_EQUAL(a * b, ra[i] * rb[i]);
            MUST_EQUAL(a * T(4), ra[i] * T(4));
            MUST_EQUAL(T(5) * b, T(5) * rb[i]);
            MUST_EQUAL(a / b, ra[i] / rb[i]);
            MUST_EQUAL(a / T(6), ra[i] / T(6));
            MUST_EQUAL(T(7) / b, T(7) / rb[i]);

            auto abs_a = simd::abs(a);
            MUST_EQUAL(simd::sqrt(abs_a), std::sqrt(std::abs(ra[i])));
            MUST_EQUAL(simd::rsqrt(abs_a), T(1) / std::sqrt(std::abs(ra[i])));
            MUST_EQUAL(simd::approx_rsqrt(abs_a), T(1) / std::sqrt(std::abs(ra[i])));
        } else {
            MUST_EQUAL(a | b, ra[i] | rb[i]);
            MUST_EQUAL(a & b, ra[i] & rb[i]);
            MUST_EQUAL(a ^ b, ra[i] ^ rb[i]);
        }
    }
};

template<typename T,int I=0> void run_tests_for_type() {
    if constexpr(simd::v_sizes<T>::value[I]) {
        std::printf("testing size %u\n",static_cast<unsigned int>(simd::v_sizes<T>::value[I]));
        tester<T,simd::v_sizes<T>::value[I]>::tests();
        run_tests_for_type<T,I+1>();
    }
}

int main() {
    std::printf("testing float\n");
    run_tests_for_type<float>();
    std::printf("testing double\n");
    run_tests_for_type<double>();
    std::printf("testing int32_t\n");
    run_tests_for_type<int32_t>();
    std::printf("testing int64_t\n");
    run_tests_for_type<int64_t>();
    std::printf("testing int16_t\n");
    run_tests_for_type<int16_t>();
    std::printf("testing int8_t\n");
    run_tests_for_type<int8_t>();
    std::printf("all tests succeeded\n");
    return 0;
}
