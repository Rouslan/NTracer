#ifndef render_hpp
#define render_hpp

#include "light.hpp"
#include "pyobject.hpp"
#include "geom_allocator.hpp"

class scene {
public:
    virtual void set_view_size(int w,int h) = 0;

    // must be thread-safe
    virtual color calculate_color(int x,int y,geom_allocator *a) const = 0;

    // may return null
    virtual geom_allocator *new_allocator() const = 0;

    /* Prevent python code from modifying the scene. The object is also expected
       to remain alive until unlock is called */
    virtual void lock() = 0;

    virtual void unlock() noexcept = 0;

protected:
    ~scene() = default;
};

struct obj_Scene : py::pyobj_subclass {
#ifdef RENDER_MODULE
    CONTAINED_PYTYPE_DEF
#endif
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

    PY_MEM_NEW_DELETE
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

struct wrapped_aabb {
    py::object obj;
    float *start;
    float *end;
};

struct tracerx_constructors {
    unsigned int batch_size;
    wrapped_array (*vector)(size_t);
    wrapped_array (*matrix)(size_t);
    wrapped_arrays (*triangle)(size_t,material*);
    wrapped_arrays (*triangle_batch)(size_t,material**);
    void (*triangle_extra)(PyObject*);
    void (*triangle_batch_extra)(PyObject*);
    wrapped_solid (*solid)(size_t,int,material*);
    wrapped_aabb (*aabb)(size_t);
    void (*solid_extra)(PyObject*);
};

struct package_common {
    void (*read_color)(color&,PyObject*,PyObject*);
    PyObject *(*vector_reduce)(size_t,const float*);
    PyObject *(*matrix_reduce)(size_t,const float*);
    PyObject *(*triangle_reduce)(size_t,const float* const*,material*);
    PyObject *(*triangle_batch_reduce)(size_t,size_t,const float* const*,py::pyptr<material> *m);
    PyObject *(*solid_reduce)(size_t,char,const float*,const float*,material*);
    PyObject *(*aabb_reduce)(size_t dim,const float *start,const float *end);
    void (*invalidate_reference)(PyObject*);
};

#ifndef RENDER_MODULE
extern package_common package_common_data;

inline void read_color(color &to,PyObject *from,PyObject *field=nullptr) {
    (*package_common_data.read_color)(to,from,field);
}
#endif

#endif
