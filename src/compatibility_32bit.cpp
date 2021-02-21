#include <new>

/* On some 32-bit systems, __STDCPP_DEFAULT_NEW_ALIGNMENT__ is erroneously
defined as 16 but operator new only aligns to 8 bytes. */

void *operator new(size_t size) {
    return operator new(size,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}
void *operator new(size_t size,const std::nothrow_t&) noexcept {
    try {
        return operator new(size,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
    } catch(...) {
        return nullptr;
    }
}
void operator delete(void *ptr) noexcept {
    operator delete(ptr,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}

void *operator new[](size_t size) {
    return operator new[](size,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}
void *operator new[](size_t size,const std::nothrow_t&) noexcept {
    try {
        return operator new[](size,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
    } catch(...) {
        return nullptr;
    }
}
void operator delete[](void *ptr) noexcept {
    operator delete[](ptr,std::align_val_t(__STDCPP_DEFAULT_NEW_ALIGNMENT__));
}
