#ifndef render_hpp
#define render_hpp

#include "light.hpp"
#include "py_common.hpp"

class scene {
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
    PyObject_HEAD
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
    
    color c, specular;
    float opacity, reflectivity, specular_intensity ,specular_exp;
};

struct package_common {
    void (*read_color)(color&,PyObject*,const char*);
};

#ifndef RENDER_MODULE
extern const package_common *package_common_data;

inline void read_color(color &to,PyObject *from,const char *field=nullptr) {
    package_common_data->read_color(to,from,field);
}
#endif

#endif