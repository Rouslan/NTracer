#include "var_geometry.hpp"

#ifdef DEBUG_GEOM_MEM
#include <cstdio>
#include <cstdlib>

namespace {
void abort_error(const char *cond) {
    std::fprintf(stderr,"assertion \"%s\" failed in allocator\n",cond);
    std::abort();
}
}

// we define our own assert macro that doesn't rely on the value of NDEBUG
#define geom_assert(X) if(!(X)) abort_error(#X)

#define geom_do(X) X
#else
#define geom_assert(X) void(0)
#define geom_do(X) void(0)
#endif

namespace var {

void *def_v_array_allocator_t::alloc(size_t size,size_t align) { return global_new(size,align); }
void def_v_array_allocator_t::dealloc(void *ptr,size_t size,size_t align) { global_delete(ptr,size,align); }

def_v_array_allocator_t def_v_array_allocator;

simple_seg_storage::simple_seg_storage(size_t item_size,size_t alignment,size_t items_per_block)
    : first_block{nullptr},
    first_free_chunk{nullptr},
    chunk_size{aligned(std::max(item_size,sizeof(char*)),alignment)},
    alignment{alignment},
    items_per_block{items_per_block}
{
    geom_assert(items_per_block > 1);
    geom_assert(alignment > 1);
}

simple_seg_storage::~simple_seg_storage() {
    geom_assert(alloc_items == 0);
}

void *simple_seg_storage::alloc_item() {
    // every unallocated chunk contains a pointer to the next unallocated chunk

    if(!first_free_chunk) {
        /* allocate a new block with a pointer at the end to the previous
        "first_block" */
        char *block = reinterpret_cast<char*>(::operator new(
            aligned(chunk_size * items_per_block,alignof(char*)) + sizeof (char*),
            std::align_val_t{alignment}));
        size_t i=0;
        for(; i<items_per_block-1; ++i) {
            *reinterpret_cast<char**>(block + i*chunk_size) = block + (i+1)*chunk_size;
        }
        *reinterpret_cast<char**>(block + i++ * chunk_size) = nullptr;
        first_free_chunk = block;
        *reinterpret_cast<char**>(block + i * chunk_size) = first_block;
        first_block = block;
    }
    void *r = first_free_chunk;
    first_free_chunk = *reinterpret_cast<char**>(first_free_chunk);
    return r;
}

void simple_seg_storage::dealloc_item(void *ptr) {
    if(!ptr) return;
    *reinterpret_cast<char**>(ptr) = first_free_chunk;
    first_free_chunk = reinterpret_cast<char*>(ptr);
}

void *simple_seg_storage::alloc(size_t size,size_t align) {
    geom_assert(size <= chunk_size);
    geom_assert(align <= alignment);
    return alloc_item();
}

void simple_seg_storage::dealloc(void *ptr,size_t size,size_t align) {
    geom_assert(size <= chunk_size);
    geom_assert(align <= alignment);
    dealloc_item(ptr);
}

}
