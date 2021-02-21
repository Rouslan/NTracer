#ifndef geom_allocator_hpp
#define geom_allocator_hpp

#include <new>
#include <cassert>

#include "compatibility.hpp"

constexpr size_t aligned(size_t size,size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}

inline void *global_new(size_t size,size_t align) {
    if(align <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) {
        return ::operator new(size);
    } else {
        return ::operator new(size,std::align_val_t{align});
    }
}
inline void global_delete(void *ptr,size_t size,size_t align) {
    if(align <= __STDCPP_DEFAULT_NEW_ALIGNMENT__) ::operator delete(ptr);
    else ::operator delete(ptr,std::align_val_t{align});
}

class v_array_allocator {
public:
    virtual void *alloc(size_t size,size_t align) = 0;
    virtual void dealloc(void *ptr,size_t size,size_t align) = 0;

protected:
    ~v_array_allocator() = default;
};

class geom_allocator {
public:
    virtual ~geom_allocator();
};

struct shallow_copy_t {};
constexpr shallow_copy_t shallow_copy;

#endif
