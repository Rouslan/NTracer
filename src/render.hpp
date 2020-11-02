#ifndef render_hpp
#define render_hpp

#include "light.hpp"
#include "pyobject.hpp"

class scene {
public:
    // must be thread-safe
    virtual color calculate_color(int x,int y,int w,int h) const = 0;

    /* Prevent python code from modifying the scene. The object is also expected
       to remain alive until unlock is called */
    virtual void lock() = 0;

    virtual void unlock() throw() = 0;

protected:
    ~scene() = default;
};

struct obj_Scene : virtual py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD

    scene &(*_get_base)(obj_Scene*);

    scene &get_base() {
        return (*_get_base)(this);
    }
};

struct color_obj_base : py::pyobj_subclass {
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

struct material : py::pyobj_subclass {
#ifdef RENDER_MODULE
    CONTAINED_PYTYPE_DEF
#else
    static PyTypeObject *_pytype;
    static PyTypeObject *pytype() { return _pytype; }
#endif

    PY_MEM_NEW_DELETE
    PyObject_HEAD

    material() {
        PyObject_Init(py::ref(this),pytype());
    }

    color c, specular;
    float opacity, reflectivity, specular_intensity, specular_exp;
};


struct wrapped_array {
    py::object obj;
    float *data;
};

struct wrapped_arrays {
    py::object obj;
    std::unique_ptr<float*[]> data;
};

struct wrapped_solid {
    py::object obj;
    float *orientation;
    float *position;
};

struct tracerx_constructors {
    wrapped_array (*vector)(int);
    wrapped_array (*matrix)(int);
    wrapped_arrays (*triangle)(int,material*);
    void (*triangle_extra)(PyObject*);
    wrapped_solid (*solid)(int,int,material*);
    void (*solid_extra)(PyObject*);
};

struct package_common {
    void (*read_color)(color&,PyObject*,const char*);
    PyObject *(*vector_reduce)(int,const float*);
    PyObject *(*matrix_reduce)(int,const float*);
    PyObject *(*triangle_reduce)(int,const float* const*,material*);
    PyObject *(*solid_reduce)(int,char,const float*,const float*,material*);
    void (*invalidate_reference)(PyObject*);
};

#ifndef RENDER_MODULE
extern package_common package_common_data;

inline void read_color(color &to,PyObject *from,const char *field=nullptr) {
    (*package_common_data.read_color)(to,from,field);
}
#endif

#endif
