#ifndef var_geometry_hpp
#define var_geometry_hpp

#include <algorithm>

#include "geometry.hpp"


namespace var {    
    /* an array that can be initialized with a call-back */
    template<typename T> struct init_array {
        size_t _size;
        void *data;
        
        T *begin() { return reinterpret_cast<T*>(data); }
        const T *begin() const { return reinterpret_cast<const T*>(data); }
        
        T *end() { return begin() + _size; }
        const T *end() const { return begin() + _size; }
        
        T &front() { return begin()[0]; }
        const T &front() const { return begin()[0]; }
        
        T &back() { return begin()[_size-1]; }
        const T &back() const { return begin()[_size-1]; }
        
        T &operator[](int i) { return begin()[i]; }
        const T &operator[](int i) const { return begin()[i]; }
        
        operator T*() { return begin(); }
        operator const T*() const { return begin(); }
        
        size_t size() const { return _size; }
        
        template<typename F> init_array(size_t _size,F f) : _size(_size) {
            assert(_size);
            
            data = operator new(sizeof(T) * _size);
            
            size_t i=0;
            try {
                for(; i<_size; ++i) new(&(*this)[i]) T(f(i));
            } catch(...) {
                while(i) (*this)[--i].~T();
                throw;
            }
        }
        
        init_array(const init_array&) = delete;
        init_array &operator=(const init_array&) = delete;
        
        ~init_array() {
            for(size_t i=0; i<_size; ++i) (*this)[i].~T();
            operator delete(data);
        }
    };
    
    template<typename RealItems,typename T> struct item_array {
        T *items;
        
        explicit item_array(int d) : size(d) {
            allocate();
        }
        
        item_array(const item_array &b) : size(b.size) {
            allocate();
            memcpy(items,b.items,RealItems::get(size) * sizeof(T));
        }
        
        item_array(item_array &&b) : items(b.items), size(b.size) {
            b.items = nullptr;
        }
        
        ~item_array() {
            deallocate();
        }
            
        item_array &operator=(const item_array &b) {
            deallocate();
            size = b.size;
            allocate();
            memcpy(items,b.items,RealItems::get(size) * sizeof(T));
            
            return *this;
        }
        
        item_array &operator=(item_array &&b) noexcept {
            deallocate();
            size = b.size;
            items = b.items;
            b.items = nullptr;
            return *this;
        }
        
        int dimension() const {
            return size;
        }
        int real_size() const {
            return RealItems::get(size);
        }
        
        void swap(item_array &b) {
            std::swap(size,b.size);
            std::swap(items,b.items);
        }
        
    private:
        void allocate() {
            assert(size > 0);

            if(simd::v_sizes<T>::value[0] == 1 && alignof(T) <= alignof(std::max_align_t)) {
                if(!(items = reinterpret_cast<T*>(malloc(real_size() * sizeof(T))))) throw std::bad_alloc();
            } else {
                items = reinterpret_cast<T*>(simd::aligned_alloc(
                    std::max(simd::largest_fit<T>(size) * sizeof(T),alignof(T)),
                    real_size() * sizeof(T)));
            }
        }
        
        void deallocate() {
            if(simd::v_sizes<T>::value[0] == 1) free(items);
            else simd::aligned_free(items);
        }
        
        int size;
    };

    template<typename T> struct item_store {
        typedef T item_t;
        
        template<typename U=T> static int v_dimension(int d) {
            return d;
        }
        
        static const int required_d = 0;
        
        template<typename U> using init_array = var::init_array<U>;
        template<typename U> using smaller_init_array = var::init_array<U>;

        template<typename RealItems,typename U=T> using type = item_array<RealItems,U>;
    };
}

template<typename RealItems,typename T> inline void swap(var::item_array<RealItems,T> &a,var::item_array<RealItems,T> &b) {
    a.swap(b);
}

#endif

