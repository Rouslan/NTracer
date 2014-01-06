#ifndef instrumentation_hpp
#define instrumentation_hpp

#ifdef PROFILE_CODE

#include <chrono>
#include <atomic>
#include <iostream>
#include <cstdlib>


#define INSTRUMENTATION_TIMER instrumentation::timer<__COUNTER__> _timer(__PRETTY_FUNCTION__)

namespace instrumentation {
    template<size_t N> class timer {
        static std::atomic<std::chrono::high_resolution_clock::rep> total_time;
        static std::atomic<size_t> total_runs;
        static const char *func;
        
        std::chrono::high_resolution_clock::time_point start;
        const char *_func;
        
    public:
        timer(const char *func);
        ~timer();
        
        static void print_stat() {
            using namespace std::chrono;
            
            std::cout << total_runs.load() << '\t'
                << duration_cast<duration<double> >(high_resolution_clock::duration(total_time)).count()
                << '\t' << func << std::endl;
        }
    };
    
    template<size_t N> __attribute__((noinline)) timer<N>::timer(const char *func) : start(std::chrono::high_resolution_clock::now()), _func(func) {}
    template<size_t N> __attribute__((noinline)) timer<N>::~timer() {
        if(!total_runs++) {
            func = _func;
            std::atexit(&print_stat);
        }
        total_time += (std::chrono::high_resolution_clock::now() - start).count();
    }
    
    template<size_t N> std::atomic<std::chrono::high_resolution_clock::rep> timer<N>::total_time;
    template<size_t N> std::atomic<size_t> timer<N>::total_runs;
    template<size_t N> const char *timer<N>::func;
}


#else
  #define INSTRUMENTATION_TIMER ((void)0)
#endif

#endif
