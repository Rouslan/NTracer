#ifndef render_hpp
#define render_hpp

#include "light.hpp"
#include "py_common.hpp"

class Scene {
public:
    // must be thread-safe
    virtual color calculate_color(int x,int y,int w,int h) const = 0;

    /* Prevent python code from modifying the scene. The object is also expected
       to remain alive until unlock is called */
    virtual void lock() = 0;
    
    virtual void unlock() throw() = 0;
};

struct color_obj_base {
#ifdef RENDER_MODULE
    CONTAINED_PYTYPE_DEF
#else
    static PyTypeObject *_pytype;
    static PyTypeObject *pytype() { return _pytype; }
#endif
};
template<> struct _wrapped_type<color> {
    typedef simple_py_wrapper<color,color_obj_base> type;
};

struct material {
#ifdef RENDER_MODULE
    CONTAINED_PYTYPE_DEF
#else
    static PyTypeObject *_pytype;
    static PyTypeObject *pytype() { return _pytype; }
#endif
    
    PyObject_HEAD
    
    color c;
    float opacity, reflectivity;
};

#endif