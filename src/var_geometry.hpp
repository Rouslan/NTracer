#ifndef var_geometry_hpp
#define var_geometry_hpp

#include <algorithm>

#include "geom_allocator.hpp"
#include "geometry.hpp"

//#define DEBUG_GEOM_MEM

#if !defined(NDEBUG) && !defined(DEBUG_GEOM_MEM)
  #define DEBUG_GEOM_MEM
#endif


namespace var {
    class def_v_array_allocator_t final : public v_array_allocator {
    public:
        void *alloc(size_t size,size_t align);
        void dealloc(void *ptr,size_t size,size_t align);
    };
    extern def_v_array_allocator_t def_v_array_allocator;

    class simple_seg_storage final : public v_array_allocator {
        char *first_block;
        char *first_free_chunk;
        const size_t chunk_size;
        const size_t alignment;
        const size_t items_per_block;
    #ifdef DEBUG_GEOM_MEM
        long alloc_items = 0;
    #endif

    public:
        simple_seg_storage(size_t item_size,size_t alignment,size_t items_per_block);
        simple_seg_storage(const simple_seg_storage&) = delete;
        ~simple_seg_storage();

        simple_seg_storage &operator=(const simple_seg_storage&) = delete;
        simple_seg_storage &operator=(simple_seg_storage&&) = delete;

        HOT_FUNC void *alloc_item();
        HOT_FUNC void dealloc_item(void *ptr);

        HOT_FUNC void *alloc(size_t size,size_t align);
        HOT_FUNC void dealloc(void *ptr,size_t size,size_t align);
    };

    template<typename T,bool=(simd::v_sizes<T>::value[0] > 1)> class sss_allocator : public geom_allocator {
        simple_seg_storage vec_storage;
        simple_seg_storage batch_vec_storage;

    public:
        sss_allocator(size_t d,size_t items_per_block)
            : vec_storage{sizeof(T) * d,
                simd::largest_fit<T>(d) * sizeof(T),
                items_per_block},
            batch_vec_storage{sizeof(T) * d * simd::v_sizes<T>::value[0],
                simd::v_sizes<T>::value[0] * sizeof(T),
                items_per_block} {}

        v_array_allocator *get_vec_alloc() {
            return &vec_storage;
        }
        v_array_allocator *get_batch_vec_alloc() {
            return &batch_vec_storage;
        }
    };

    template<typename T> class sss_allocator<T,false> : public geom_allocator {
        simple_seg_storage vec_storage;

    public:
        sss_allocator(size_t d,size_t items_per_block)
            : vec_storage{sizeof(T) * d,
                simd::largest_fit<T>(d) * sizeof(T),
                items_per_block} {}

        v_array_allocator *get_vec_alloc() {
            return &vec_storage;
        }
        v_array_allocator *get_batch_vec_alloc() {
            return &vec_storage;
        }
    };

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

        T &operator[](size_t i) { return begin()[i]; }
        const T &operator[](size_t i) const { return begin()[i]; }

        operator T*() { return begin(); }
        operator const T*() const { return begin(); }

        size_t size() const { return _size; }

        template<typename F> init_array(size_t _size,F f) : _size(_size) {
            assert(_size);
            data = global_new(sizeof(T) * _size,alignof(T));

            size_t i=0;
            try {
                for(; i<_size; ++i) new(&(*this)[i]) T(f(i));
            } catch(...) {
                while(i) (*this)[--i].~T();
                global_delete(data,sizeof(T) * _size,alignof(T));
                throw;
            }
        }

        init_array(const init_array&) = delete;
        init_array &operator=(const init_array&) = delete;

        ~init_array() {
            for(size_t i=0; i<_size; ++i) (*this)[i].~T();
            global_delete(data,sizeof(T) * _size,alignof(T));
        }
    };

    template<typename RealItems,typename T> struct item_array {
        static constexpr size_t max_items = std::numeric_limits<size_t>::max();

        template<size_t Size> FORCE_INLINE void store_vec(size_t n,simd::v_type<T,Size> val) {
            val.store(data() + n);
        }

        template<size_t Size> FORCE_INLINE auto vec(size_t n) const {
            return simd::v_type<T,Size>::load(data() + n);
        }

        T *data() { return reinterpret_cast<T*>(items); }
        const T *data() const { return reinterpret_cast<const T*>(items); }

        explicit item_array(size_t d,v_array_allocator *a=&def_v_array_allocator) : size{d}, allocator{a} {
            allocate();
        }

        struct assign {
            typedef T item_t;
            static constexpr int v_score = impl::V_SCORE_THRESHHOLD;
            static constexpr size_t max_items = item_array::max_items;

            item_array &dest;
            const item_array &src;

            assign(item_array &dest,const item_array &src) : dest{dest}, src{src} {}

            template<size_t Size> void operator()(size_t n) const {
                dest.store_vec<Size>(n,src.vec<Size>(n));
            }
        };
        static void copy_data(item_array &dest,const item_array &src) {
            impl::v_rep(RealItems::get(dest.size),assign{dest,src});
        }

        HOT_FUNC item_array(const item_array &b) : size{b.size}, allocator{b.allocator} {
            if(allocator) {
                allocate();
                copy_data(*this,b);
            } else {
                items = b.items;
            }
        }

        item_array(const item_array &b,v_array_allocator *a) : size{b.size}, allocator{a} {
            allocate();
            copy_data(*this,b);
        }

        item_array(const item_array &b,shallow_copy_t) : size{b.size}, allocator{nullptr} {
            items = b.items;
        }

        item_array(item_array &&b) : size{b.size}, allocator{b.allocator} {
            items = b.items;
            b.items = nullptr;
            b.allocator = nullptr;
            b.size = 0;
        }

        ~item_array() {
            if(allocator) deallocate();
        }

        item_array &operator=(const item_array &b) {
            if(size != b.size) {
                if(allocator) deallocate();
                else {
                    assert(b.allocator);
                    allocator = b.allocator;
                }
                size = b.size;
                allocate();
            }
            copy_data(*this,b);

            return *this;
        }

        item_array &operator=(item_array &&b) noexcept {
            if(allocator) deallocate();
            size = b.size;
            items = b.items;
            allocator = b.allocator;
            b.items = nullptr;
            b.allocator = nullptr;
            b.size = 0;
            return *this;
        }

        size_t dimension() const {
            return size;
        }

        void swap(item_array &b) {
            std::swap(size,b.size);
            std::swap(items,b.items);
            std::swap(allocator,b.allocator);
        }

    private:
        void allocate() {
            assert(size > 0 && allocator);
            items = reinterpret_cast<char*>(allocator->alloc(
                RealItems::get(size) * sizeof(T),
                simd::largest_fit<T>(size) * sizeof(T)));
        }

        void deallocate() {
            allocator->dealloc(
                items,
                RealItems::get(size) * sizeof(T),
                simd::largest_fit<T>(size) * sizeof(T));
        }

        size_t size;
        v_array_allocator *allocator;
        char *items;
    };

    template<typename T> struct item_store {
        using item_t = T;

        template<typename U=T> static size_t v_dimension(size_t d) {
            return d;
        }

        static constexpr size_t required_d = 0;

        template<typename U> using init_array = var::init_array<U>;
        template<typename U> using smaller_init_array = var::init_array<U>;

        template<typename RealItems,typename U=T> using type = item_array<RealItems,U>;

        static geom_allocator *new_allocator(size_t d,size_t items_per_block) {
            return new sss_allocator<T>(d,items_per_block);
        }

        static constexpr v_array_allocator *def_allocator = &def_v_array_allocator;

        template<typename U> static v_array_allocator *allocator_for(geom_allocator *a) {
            if constexpr(std::is_same_v<U,vector<item_store>>) {
                return a ? static_cast<sss_allocator<T>*>(a)->get_vec_alloc() : &def_v_array_allocator;
            } else if constexpr(std::is_same_v<U,vector_batch<item_store>>) {
                return a ? static_cast<sss_allocator<T>*>(a)->get_batch_vec_alloc() : &def_v_array_allocator;
            } else {
                return &def_v_array_allocator;
            }
        }
    };
}

template<typename RealItems,typename T> inline void swap(var::item_array<RealItems,T> &a,var::item_array<RealItems,T> &b) {
    a.swap(b);
}

#endif
