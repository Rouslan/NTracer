
#include <Python.h>
#include <assert.h>

#include "py_common.hpp"
#include "fixed_geometry.hpp"
#include "var_geometry.hpp"
#include "tracer.hpp"


#define INHERIT_FROM_RENDER_SCENE


#define _STR(X) #X
#define STR(X) _STR(X)
#define MODULE_STR STR(MODULE_NAME)
#define _APPEND(X,Y) X ## Y
#define APPEND(X,Y) _APPEND(X,Y)
#define APPEND_MODULE_NAME(base) APPEND(base,MODULE_NAME)

#define PACKAGE ntracer
#define PACKAGE_STR STR(PACKAGE)


using namespace type_object_abbrev;


typedef vector<module_store> n_vector;
typedef matrix<module_store> n_matrix;
typedef camera<module_store> n_camera;
typedef solid_prototype<module_store> n_solid_prototype;
typedef triangle_prototype<module_store> n_triangle_prototype;
typedef triangle_point<module_store> n_triangle_point;
typedef aabb<module_store> n_aabb;


Py_ssize_t check_dimension(int d) {
    if(d < 3) {
        PyErr_Format(
            PyExc_ValueError,
            "dimension cannot be less than 3");
        throw py_error_set();
    }
    
    if(module_store::required_d && d != module_store::required_d) {
        assert(module_store::required_d >= 3);
        PyErr_Format(
            PyExc_ValueError,
            "this class only supports a dimension of %d",
            module_store::required_d);
        throw py_error_set();
    }
    
    return module_store::required_d ? 0 : d;
}

struct sized_iter {
    py::object itr;
    int expected_len;
    
    sized_iter(const py::object &obj,int expected_len) : itr(py::iter(obj)), expected_len(expected_len) {}
    
    py::object next();
    void finished() const;
};

py::object sized_iter::next() {
    py::nullable_object r = py::next(itr);
    if(!r) {
        PyErr_Format(PyExc_ValueError,"too few items in object, expected %d",expected_len);
        throw py_error_set();
    }
    return *r;
}

void sized_iter::finished() const {
    py::nullable_object r = py::next(itr);
    if(r) {
        PyErr_Format(PyExc_ValueError,"too many items in object, expected %d",expected_len);
        throw py_error_set();
    }
}

template<typename T1,typename T2> inline bool compatible(const T1 &a,const T2 &b) {
    return a.dimension() == b.dimension();
}

template<typename T,bool trivial=std::is_trivially_destructible<T>::value> struct destructor_dealloc {
    static void _function(PyObject *self) {
        reinterpret_cast<T*>(self)->~T();
        Py_TYPE(self)->tp_free(self);
    }
    
    static constexpr void (*value)(PyObject*) = &_function;
};

template<typename T> struct destructor_dealloc<T,true> {
    static constexpr void (*value)(PyObject*) = nullptr;
};

template<typename T> void fill_vector(std::vector<T,simd::allocator<T> > &v,Py_ssize_t size,PyObject **items) {
    v.reserve(size);
    for(Py_ssize_t i=0; i<size; ++i) v.push_back(from_pyobject<T>(items[i]));
}

template<typename T> std::vector<T,simd::allocator<T> > collect(PyObject *src) {
    std::vector<T,simd::allocator<T> > items;
    
    if(PyTuple_Check(src)) {
        fill_vector(items,PyTuple_GET_SIZE(src),&PyTuple_GET_ITEM(src,0));
    } else if(PyList_Check(src)) {
        fill_vector(items,PyList_GET_SIZE(src),&PyList_GET_ITEM(src,0));
    } else {
        py::object itr = py::new_ref(py::check_obj(PyObject_GetIter(src)));
        while(py::nullable_object item = py::next(itr)) {
            items.push_back(from_pyobject<T>(*item));
        }
    }
    return items;
}


template<typename T,typename Base,bool InPlace=(alignof(T) <= PYOBJECT_ALIGNMENT)> struct simple_py_wrapper : Base {
    PyObject_HEAD
    T base;
    PY_MEM_NEW_DELETE
    simple_py_wrapper(const T &b) : base(b) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&Base::pytype);
    }
    simple_py_wrapper(T &&b) : base(std::move(b)) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&Base::pytype);
    }
    
    T &alloc_base() {
        return base;
    }
    
    T &cast_base() { return base; }
    T &get_base() { return base; }
};

template<typename T,typename Base> struct simple_py_wrapper<T,Base,false> : Base {
    PyObject_HEAD
    T *base;
    PY_MEM_NEW_DELETE
    simple_py_wrapper(const T &b) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&Base::pytype);
        alloc_base();
        new(base) T(b);
    }
    simple_py_wrapper(T &&b) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&Base::pytype);
        alloc_base();
        new(base) T(std::move(b));
    }
    ~simple_py_wrapper() {
        if(base) base->~T();
        simd::aligned_free(base);
    }
    
    T &alloc_base() {
        assert(!base);
        base = reinterpret_cast<T*>(simd::aligned_alloc(alignof(T),sizeof(T)));
        return *base;
    }
    
    T &cast_base() { return *base; }
    T &get_base() { return *base; }
};

#define SIMPLE_WRAPPER(ROOT) \
struct ROOT ## _obj_base { \
    static PyTypeObject pytype; \
}; \
template<> struct _wrapped_type<n_ ## ROOT> : ROOT ## _obj_base { \
    typedef simple_py_wrapper<n_ ## ROOT,ROOT ## _obj_base> type; \
}


SIMPLE_WRAPPER(vector);
SIMPLE_WRAPPER(matrix);
SIMPLE_WRAPPER(camera);


template<typename T> PyObject *to_pyobject(const ::impl::vector_expr<T> &e) {
    return to_pyobject(n_vector(e));
}


struct obj_BoxScene {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    BoxScene<module_store> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    obj_BoxScene(int d) : base(d), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    obj_BoxScene(BoxScene<module_store> const &b) : base(b), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    ~obj_BoxScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    BoxScene<module_store> &cast_base() { return base; }
    BoxScene<module_store> &get_base() { return base; }
};

template<> struct _wrapped_type<BoxScene<module_store> > {
    typedef obj_BoxScene type;
};


constexpr char matrix_proxy_name[] = MODULE_STR ".MatrixProxy";
typedef py::obj_array_adapter<real,matrix_proxy_name,false,true> obj_MatrixProxy;


struct obj_CameraAxes {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    py::pyptr<wrapped_type<n_camera> > base;
    PY_MEM_GC_NEW_DELETE

    obj_CameraAxes(wrapped_type<n_camera> *base) : base(py::borrowed_ref(reinterpret_cast<PyObject*>(base))) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
};


struct obj_CompositeScene {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    composite_scene<module_store> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    ~obj_CompositeScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    composite_scene<module_store> &cast_base() { return base; }
    composite_scene<module_store> &get_base() { return base; }
};

template<> struct _wrapped_type<composite_scene<module_store> > {
    typedef obj_CompositeScene type;
};

template<typename T> void ensure_unlocked(T *s) {
    if(s->base.locked) {
        PyErr_SetString(PyExc_RuntimeError,"the scene is locked for reading");
        throw py_error_set();
    }
}

template<typename T> void ensure_unlocked(const py::pyptr<T> &s) {
    if(s) ensure_unlocked(s.get());
}


constexpr char frozen_vector_view_name[] = MODULE_STR ".FrozenVectorView";
typedef py::obj_array_adapter<n_vector,frozen_vector_view_name,true,true> obj_FrozenVectorView;


struct obj_Primitive {
    static PyTypeObject pytype;
    
    PyObject_HEAD
};

typedef solid<module_store> obj_Solid;
typedef triangle<module_store> obj_Triangle;


/* The following wrappers store their associated data in a special way. In large
   scenes, there will be many k-d tree nodes which need to be traversed as
   quickly as possible, so instead of incorporating the Python reference count
   and type data into the native data structures, the wrappers contain a bare
   pointer and a reference to a wrapped parent structure. When the parent is
   null, the data does not have any parent nodes and the wrapper is responsible
   for deleting the data. Otherwise, it is the parent's responsibility to delete
   the data, which won't be destroyed before the child's wrapper is destroyed,
   due to the reference. A child cannot be added to more than one parent. */

struct obj_KDNode {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    PY_MEM_GC_NEW_DELETE
    py::nullable_object parent;
    kd_node<module_store> *_data;
    
    int dimension() const;
    
protected:
    obj_KDNode(py::nullable_object parent,kd_node<module_store> *data) : parent(parent), _data(data) {}
    ~obj_KDNode() = default;
};

struct obj_KDBranch : obj_KDNode {
    static PyTypeObject pytype;
    
    int dimension;
    
    kd_branch<module_store> *&data() {
        assert(_data);
        return reinterpret_cast<kd_branch<module_store>*&>(_data);
    }
    
    kd_branch<module_store> *data() const {
        assert(_data);
        return static_cast<kd_branch<module_store>*>(_data);
    }
    
    obj_KDBranch(py::nullable_object parent,kd_branch<module_store> *data,int dimension) : obj_KDNode(parent,data), dimension(dimension) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    ~obj_KDBranch() {
        if(!parent) delete data();
    }
};

struct obj_KDLeaf : obj_KDNode {
    static PyTypeObject pytype;
    
    kd_leaf<module_store> *&data() {
        assert(_data);
        return reinterpret_cast<kd_leaf<module_store>*&>(_data);
    }
    
    kd_leaf<module_store> *data() const {
        assert(_data);
        return static_cast<kd_leaf<module_store>*>(_data);
    }
    
    obj_KDLeaf(py::nullable_object parent,kd_leaf<module_store> *data) : obj_KDNode(parent,data) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    ~obj_KDLeaf() {
        if(!parent) delete data();
    }
};

int obj_KDNode::dimension() const {
    if(Py_TYPE(this) == &obj_KDBranch::pytype) return static_cast<const obj_KDBranch*>(this)->dimension;
    
    assert(Py_TYPE(this) == &obj_KDLeaf::pytype);
    return static_cast<const obj_KDLeaf*>(this)->data()->dimension();
}


SIMPLE_WRAPPER(aabb);


struct obj_PrimitivePrototype {
    static PyTypeObject pytype;
    PyObject_HEAD
    n_triangle_prototype base;
};


struct triangle_prototype_obj_base {
    static PyTypeObject pytype;
};

/* Whether this object is variable-sized or not depends on whether
   n_triangle_prototype can vary in size and whether n_triangle_prototype can be
   stored in-place (which it can't if it requires extra alignment). If it is
   stored in-place, care must be taken to ensure it has enough room  */
template<bool Var,bool InPlace> struct tproto_base : triangle_prototype_obj_base {
    PyObject_HEAD
    n_triangle_prototype base;
    
    n_triangle_prototype &alloc_base(int dimension) {
        return base;
    }
    n_triangle_prototype &get_base() {
        return base;
    }
    
    typedef tproto_base<Var,InPlace> this_t;
    static const size_t base_size = n_triangle_prototype::item_offset
        + sizeof(n_triangle_point)*module_store::required_d
        + offsetof(this_t,base);
    static const size_t item_size = 0;
};
template<bool Var> struct tproto_base<Var,false> : triangle_prototype_obj_base {
    PyObject_HEAD
    std::unique_ptr<n_triangle_prototype> base;
    
    n_triangle_prototype &alloc_base(int dimension) {
        new(&base) std::unique_ptr<n_triangle_prototype>(
            reinterpret_cast<n_triangle_prototype*>(n_triangle_prototype::operator new(n_triangle_prototype::item_offset,dimension)));
        return *base;
    }
    n_triangle_prototype &get_base() {
        return *base;
    }
    
    static const size_t base_size = sizeof(tproto_base<Var,false>);
    static const size_t item_size = 0;
};
template<> struct tproto_base<true,true> : triangle_prototype_obj_base {
    PyObject_VAR_HEAD
    n_triangle_prototype base;
    
    n_triangle_prototype &alloc_base(int dimension) {
        return base;
    }
    n_triangle_prototype &get_base() {
        return base;
    }

    static const size_t base_size;
    static const size_t item_size = sizeof(n_triangle_point);
};
#define COMMA_WORKAROUND tproto_base<true,true>
const size_t tproto_base<true,true>::base_size = n_triangle_prototype::item_offset + offsetof(COMMA_WORKAROUND,base);
#undef COMMA_WORKAROUND

typedef tproto_base<!module_store::required_d,(alignof(n_triangle_prototype) <= PYOBJECT_ALIGNMENT)> obj_TrianglePrototype;
template<> struct _wrapped_type<n_triangle_prototype> {
    typedef obj_TrianglePrototype type;
};


SIMPLE_WRAPPER(triangle_point);

constexpr char triangle_point_data_name[] = MODULE_STR ".TrianglePointData";
typedef py::obj_array_adapter<n_triangle_point,triangle_point_data_name,false,true> obj_TrianglePointData;


SIMPLE_WRAPPER(solid_prototype);


template<> n_vector from_pyobject<n_vector>(PyObject *o) {
    if(PyTuple_Check(o)) {
        if(sizeof(Py_ssize_t) > sizeof(int) && UNLIKELY(PyTuple_GET_SIZE(o) > std::numeric_limits<int>::max()))
            THROW_PYERR_STRING(ValueError,"too many items for a vector");
        
        check_dimension(PyTuple_GET_SIZE(o));
        return {int(PyTuple_GET_SIZE(o)),[=](int i){ return from_pyobject<real>(PyTuple_GET_ITEM(o,i)); }};
    }
    if(Py_TYPE(o) == &obj_MatrixProxy::pytype) {
        auto &mp = reinterpret_cast<obj_MatrixProxy*>(o)->data;
        return {int(mp.size),[&](int i){ return mp.items[i]; }};
    }
    return get_base<n_vector>(o);
}



int obj_BoxScene_traverse(obj_BoxScene *self,visitproc visit,void *arg) {
    Py_VISIT(self->idict);
    return 0;
}

int obj_BoxScene_clear(obj_BoxScene *self) {
    Py_CLEAR(self->idict);
    return 0;
}

PyObject *obj_BoxScene_set_camera(obj_BoxScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        
        auto &c = get_base<n_camera>(arg);
        if(!compatible(self->base,c)) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return NULL;
        }
        
        self->base.cam = c;
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_BoxScene_get_camera(obj_BoxScene *self,PyObject *) {
    try {
        return to_pyobject(self->base.cam);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_BoxScene_set_fov(obj_BoxScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        self->base.fov = from_pyobject<real>(arg);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_BoxScene_methods[] = {
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_set_camera),METH_O,NULL},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_get_camera),METH_NOARGS,NULL},
    {"set_fov",reinterpret_cast<PyCFunction>(&obj_BoxScene_set_fov),METH_O,NULL},
    {NULL}
};

PyObject *obj_BoxScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_BoxScene*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                const char *names[] = {"dimension"};
                get_arg ga(args,kwds,names,"BoxScene.__new__");
                real d = from_pyobject<real>(ga(true));
                ga.finished();
                
                new(&ptr->base) BoxScene<module_store>(d);
            } catch(...) {
                type->tp_free(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(NULL)

        ptr->weaklist = NULL;
    }
    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_BoxScene_getset[] = {
    {const_cast<char*>("locked"),OBJ_GETTER(obj_BoxScene,self->base.locked),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_BoxScene_members[] = {
    {const_cast<char*>("fov"),member_macro<real>::value,offsetof(obj_BoxScene,base.fov),READONLY,NULL},
    {NULL}
};

PyTypeObject obj_BoxScene::pytype = make_type_object(
    MODULE_STR ".BoxScene",
    sizeof(obj_BoxScene),
    tp_dealloc = destructor_dealloc<obj_BoxScene>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &obj_BoxScene_traverse,
    tp_clear = &obj_BoxScene_clear,
    tp_weaklistoffset = offsetof(obj_BoxScene,weaklist),
    tp_methods = obj_BoxScene_methods,
    tp_members = obj_BoxScene_members,
    tp_getset = obj_BoxScene_getset,
    tp_dictoffset = offsetof(obj_BoxScene,idict),
    tp_new = &obj_BoxScene_new);


int obj_CompositeScene_traverse(obj_CompositeScene *self,visitproc visit,void *arg) {
    Py_VISIT(self->idict);
    return 0;
}


int obj_CompositeScene_clear(obj_CompositeScene *self) {
    Py_CLEAR(self->idict);
    return 0;
}

PyObject *obj_CompositeScene_set_camera(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        
        auto &c = get_base<n_camera>(arg);
        if(!compatible(self->base,c)) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return NULL;
        }
        
        self->base.cam = c;
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_CompositeScene_get_camera(obj_CompositeScene *self,PyObject *) {
    try {
        return to_pyobject(self->base.cam);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_CompositeScene_set_fov(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        self->base.fov = from_pyobject<real>(arg);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_CompositeScene_methods[] = {
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_camera),METH_O,NULL},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_get_camera),METH_NOARGS,NULL},
    {"set_fov",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_fov),METH_O,NULL},
    {NULL}
};

PyObject *obj_CompositeScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_CompositeScene*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                const char *names[] = {"data"};
                get_arg ga(args,kwds,names,"CompositeScene.__new__");
                PyObject *data = ga(true);
                ga.finished();
                
                if(Py_TYPE(data) != &obj_KDBranch::pytype && Py_TYPE(data) != &obj_KDLeaf::pytype) {
                    PyErr_SetString(PyExc_TypeError,"\"data\" must be an instance of " MODULE_STR ".KDNode");
                    throw py_error_set();
                }
                
                auto d_node = reinterpret_cast<obj_KDNode*>(data);
                
                new(&ptr->base) composite_scene<module_store>(d_node->dimension(),d_node->_data);
                d_node->parent = py::borrowed_ref(reinterpret_cast<PyObject*>(ptr));
            } catch(...) {
                type->tp_free(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(NULL)
    }
    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_CompositeScene_getset[] = {
    {const_cast<char*>("locked"),OBJ_GETTER(obj_CompositeScene,self->base.locked),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_CompositeScene_members[] = {
    {const_cast<char*>("fov"),member_macro<real>::value,offsetof(obj_CompositeScene,base.fov),READONLY,NULL},
    {NULL}
};

PyTypeObject obj_CompositeScene::pytype = make_type_object(
    MODULE_STR ".CompositeScene",
    sizeof(obj_CompositeScene),
    tp_dealloc = destructor_dealloc<obj_CompositeScene>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &obj_CompositeScene_traverse,
    tp_clear = &obj_CompositeScene_clear,
    tp_weaklistoffset = offsetof(obj_CompositeScene,weaklist),
    tp_methods = obj_CompositeScene_methods,
    tp_members = obj_CompositeScene_members,
    tp_getset = obj_CompositeScene_getset,
    tp_dictoffset = offsetof(obj_CompositeScene,idict),
    tp_new = &obj_CompositeScene_new);


void check_index(const n_camera &c,Py_ssize_t index) {
    if(index < 0 || index > c.dimension()) THROW_PYERR_STRING(IndexError,"index out of range");
}

PySequenceMethods obj_CameraAxes_sequence_methods = {
    [](PyObject *self) -> Py_ssize_t {
        return reinterpret_cast<obj_CameraAxes*>(self)->base->get_base().dimension();
    },
    NULL,
    NULL,
    [](PyObject *self,Py_ssize_t index) -> PyObject* {
        try {
            auto &base = reinterpret_cast<obj_CameraAxes*>(self)->base->get_base();
            check_index(base,index);
            return to_pyobject(n_vector(base.t_orientation[index]));
        } PY_EXCEPT_HANDLERS(nullptr)
    },
    NULL,
    [](PyObject *self,Py_ssize_t index,PyObject *value) -> int {
        try {
            auto &base = reinterpret_cast<obj_CameraAxes*>(self)->base->get_base();
            check_index(base,index);
            base.t_orientation[index] = from_pyobject<n_vector>(value);
            return 0;
        } PY_EXCEPT_HANDLERS(-1)
    },
    NULL,
    NULL,
    NULL,
    NULL
};

PyTypeObject obj_CameraAxes::pytype = make_type_object(
    MODULE_STR ".CameraAxes",
    sizeof(obj_BoxScene),
    tp_dealloc = destructor_dealloc<obj_CameraAxes>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_as_sequence = &obj_CameraAxes_sequence_methods,
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the CameraAxes type cannot be instantiated directly");
        return nullptr;
    });


PyTypeObject obj_Primitive::pytype = make_type_object(
    MODULE_STR ".Primitive",
    sizeof(obj_Primitive),
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the Primitive type cannot be instantiated directly");
        return nullptr;
    });


template<typename T> int kd_tree_item_traverse(PyObject *self,visitproc visit,void *arg) {
    return reinterpret_cast<T*>(self)->parent.gc_traverse(visit,arg);
}

template<typename T> int kd_tree_item_clear(PyObject *self) {
    T *obj = reinterpret_cast<T*>(self);
    if(obj->parent) {
        obj->data() = nullptr;
        obj->parent.gc_clear();
    }
    return 0;
}

template<> solid_type from_pyobject<solid_type>(PyObject *o) {
    int t = from_pyobject<int>(o);
    if(t != CUBE && t != SPHERE) THROW_PYERR_STRING(ValueError,"invalid shape type");
    return static_cast<solid_type>(t);
}

/*PyObject *obj_Solid_set_type(obj_Solid *self,PyObject *arg) {
    try {
        ensure_unlocked(self->scene);

        self->data()->type = from_pyobject<primitive_type>(arg);
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Solid_set_orientation(obj_Solid *self,PyObject *arg) {
    try {
        ensure_unlocked(self->scene);
        
        self->data()->orientation = get_base<n_matrix>(arg);
        self->data()->inv_orientation = self->data->orientation.inverse();
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Solid_set_position(obj_Solid *self,PyObject *arg) {
    try {
        ensure_unlocked(self->scene);
        
        self->data()->position = from_pyobject<n_vector>(arg);
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}*/

/*PyMethodDef obj_Solid_methods[] = {
    {"set_type",reinterpret_cast<PyCFunction>(&obj_Solid_set_type),METH_O,NULL},
    {"set_orientation",reinterpret_cast<PyCFunction>(&obj_Solid_set_orientation),METH_O,NULL},
    {"set_position",reinterpret_cast<PyCFunction>(&obj_Solid_set_position),METH_O,NULL},
    {NULL}
};*/

PyObject *obj_Solid_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"type","orientation","position"};
        get_arg ga(args,kwds,names,"Solid.__new__");
        auto type = from_pyobject<solid_type>(ga(true));
        auto &orientation = get_base<n_matrix>(ga(true));
        auto position = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        if(!compatible(orientation,position))
            THROW_PYERR_STRING(TypeError,"the orientation and position must have the same dimension");
        
        return reinterpret_cast<PyObject*>(new obj_Solid(type,orientation,position));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Solid_getset[] = {
    {const_cast<char*>("type"),OBJ_GETTER(obj_Solid,int(self->type)),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(obj_Solid,self->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(obj_Solid,self->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(obj_Solid,self->position),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_Solid,self->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject solid_obj_common::pytype = make_type_object(
    MODULE_STR ".Solid",
    sizeof(obj_Solid),
    tp_dealloc = destructor_dealloc<obj_Solid>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_getset = obj_Solid_getset,
    tp_base = &obj_Primitive::pytype,
    tp_new = &obj_Solid_new,
    tp_free = &obj_Solid::operator delete);


void check_origin_dir_compat(const n_vector &o,const n_vector &d) {
    if(!compatible(o,d))
        THROW_PYERR_STRING(TypeError,"\"origin\" and \"direction\" must have the same dimension");
}

PyObject *obj_Triangle_intersects(obj_Triangle *self,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"origin","direction"};
        get_arg ga(args,kwds,names,"Triangle.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<module_store> target(origin,direction);
        ray<module_store> normal(origin.dimension());
        
        real dist = self->intersects(target,normal);
        if(!dist) Py_RETURN_NONE;
        
        return py::make_tuple(dist,normal.origin,normal.direction).new_ref();
    } PY_EXCEPT_HANDLERS(NULL)
}

std::vector<n_vector,simd::allocator<n_vector> > points_for_triangle(PyObject *obj) {
    auto points = collect<n_vector>(obj);
    if(points.empty()) {
        PyErr_SetString(PyExc_TypeError,"a sequence of points (vectors) is required");
        throw py_error_set();
    }
    
    int dim = points[0].dimension();
    for(size_t i=1; i<points.size(); ++i) {
        if(points[i].dimension() != dim) THROW_PYERR_STRING(TypeError,"all points must have the same dimension");
    }
    
    if(size_t(dim) != points.size()) THROW_PYERR_STRING(ValueError,"the number of points must equal their dimension");

    return points;
}

PyObject *obj_Triangle_from_points(PyObject*,PyObject *obj_points) {
    try {
        auto points = points_for_triangle(obj_points);
        return reinterpret_cast<PyObject*>(obj_Triangle::from_points(points.data()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Triangle_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&obj_Triangle_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {"from_points",&obj_Triangle_from_points,METH_O|METH_STATIC,NULL},
    {NULL}
};

PyObject *obj_Triangle_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    const char dim_err[] = "all supplied vectors must have the same dimension";

    try {
        const char *names[] = {"p1","face_normal","edge_normals"};
        get_arg ga(args,kwds,names,"Triangle.__new__");
        auto p1 = from_pyobject<n_vector>(ga(true));
        auto face_normal = from_pyobject<n_vector>(ga(true));
        auto normals = ga(true);
        ga.finished();
        
        if(!compatible(p1,face_normal)) {
            PyErr_SetString(PyExc_TypeError,dim_err);
            return NULL;
        }
        
        sized_iter norm_itr(py::borrowed_ref(normals),p1.dimension()-1);
        
        auto ptr = obj_Triangle::create(p1,face_normal,[&](int i) -> n_vector {
            auto en = from_pyobject<n_vector>(norm_itr.next());
            if(!compatible(p1,en)) THROW_PYERR_STRING(TypeError,dim_err);
            return en;
        });
        try {
            norm_itr.finished();
        } catch(...) {
            delete ptr;
        }
        
        return reinterpret_cast<PyObject*>(ptr);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Triangle_getedges(obj_Triangle *self,void *) {
    try {
        return reinterpret_cast<PyObject*>(new obj_FrozenVectorView(
            reinterpret_cast<PyObject*>(self),
            self->items().size(),
            self->items()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Triangle_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_Triangle,self->dimension()),NULL,NULL,NULL},
    {const_cast<char*>("p1"),OBJ_GETTER(obj_Triangle,self->p1),NULL,NULL,NULL},
    {const_cast<char*>("face_normal"),OBJ_GETTER(obj_Triangle,self->face_normal),NULL,NULL,NULL},
    {const_cast<char*>("edge_normals"),reinterpret_cast<getter>(&obj_Triangle_getedges),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_Triangle_members[] = {
    {const_cast<char*>("d"),member_macro<real>::value,offsetof(obj_Triangle,d),READONLY,NULL},
    {NULL}
};

PyTypeObject triangle_obj_common::pytype = make_type_object(
    MODULE_STR ".Triangle",
    obj_Triangle::base_size,
    tp_itemsize = obj_Triangle::item_size,
    tp_dealloc = destructor_dealloc<obj_Triangle>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_methods = obj_Triangle_methods,
    tp_members = obj_Triangle_members,
    tp_getset = obj_Triangle_getset,
    tp_base = &obj_Primitive::pytype,
    tp_new = &obj_Triangle_new,
    tp_free = &obj_Triangle::operator delete);


PyObject *kdnode_intersects(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);
        
        const char *names[] = {"origin","direction","t_near","t_far"};
        get_arg ga(args,kwds,names,self->_data->type == LEAF ? "KDLeaf.intersects" : "KDBranch.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        
        real t_near = std::numeric_limits<real>::lowest();
        real t_far = std::numeric_limits<real>::max();
        
        PyObject *tmp = ga(false);
        if(tmp) t_near = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) t_far = from_pyobject<real>(tmp);
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<module_store> target(origin,direction);
        ray<module_store> normal(origin.dimension());
        
        if(!self->_data->intersects(target,normal,t_near,t_far)) Py_RETURN_NONE;
        
        return py::make_tuple(normal.origin,normal.direction).new_ref();
    } PY_EXCEPT_HANDLERS(NULL)
}

PyTypeObject obj_KDNode::pytype = make_type_object(
    MODULE_STR ".KDNode",
    sizeof(obj_KDNode),
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the KDNode type cannot be instantiated directly");
        return nullptr;
    });


Py_ssize_t obj_KDLeaf___sequence_len__(obj_KDLeaf *self) {
    return self->data()->size;
}

PyObject *obj_KDLeaf___sequence_getitem__(obj_KDLeaf *self,Py_ssize_t index) {
    if(index < 0 || index >= static_cast<Py_ssize_t>(self->data()->size)) {
        PyErr_SetString(PyExc_IndexError,"index out of range");
        return NULL;
    }
    return py::incref(self->data()->items()[index].ref());
}

PySequenceMethods obj_KDLeaf_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_KDLeaf___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_KDLeaf___sequence_getitem__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PyMethodDef obj_KDLeaf_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&kdnode_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyObject *obj_KDLeaf_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_KDLeaf*>(type->tp_alloc(type,0));
    if(!ptr) return NULL;
    

    try {
        try {
            const char *names[] = {"primitives"};
            get_arg ga(args,kwds,names,"KDLeaf.__new__");
            py::tuple primitives(py::object(py::borrowed_ref(ga(true))));
            ga.finished();
            
            Py_ssize_t size = primitives.size();
            
            if(!size) {
                PyErr_SetString(PyExc_ValueError,"KDLeaf requires at least one item");
                throw py_error_set();
            }
            
            ptr->data() = kd_leaf<module_store>::create(size,[=](size_t i) -> primitive<module_store>* {
                auto p = primitives[i];
                
                if(p.type() != &solid_obj_common::pytype && p.type() != &triangle_obj_common::pytype) {
                    PyErr_SetString(PyExc_TypeError,"object is not an instance of " MODULE_STR ".Primitive");
                    throw py_error_set();
                }

                return reinterpret_cast<primitive<module_store>*>(p.new_ref());
            });
            
            
            int d = ptr->data()->items()[0]->dimension();
            for(Py_ssize_t i=1; i<size; ++i) {
                if(ptr->data()->items()[i]->dimension() != d) {
                    Py_DECREF(ptr);
                    PyErr_SetString(PyExc_TypeError,"every member of KDLeaf must have the same dimension");
                    return NULL;
                }
            }
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(NULL)

    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_KDLeaf_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_KDLeaf,self->data()->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDLeaf::pytype = make_type_object(
    MODULE_STR ".KDLeaf",
    sizeof(obj_KDLeaf),
    tp_dealloc = destructor_dealloc<obj_KDLeaf>::value,
    tp_as_sequence = &obj_KDLeaf_sequence_methods,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_methods = obj_KDLeaf_methods,
    tp_traverse = &kd_tree_item_traverse<obj_KDLeaf>,
    tp_clear = &kd_tree_item_clear<obj_KDLeaf>,
    tp_getset = obj_KDLeaf_getset,
    tp_base = &obj_KDNode::pytype,
    tp_new = &obj_KDLeaf_new);


PyMethodDef obj_KDBranch_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&kdnode_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

obj_KDNode* acceptable_node(PyObject *obj) {
    if(!obj || obj == Py_None) return nullptr;
    
    if(Py_TYPE(obj) != &obj_KDBranch::pytype && Py_TYPE(obj) != &obj_KDLeaf::pytype)
        THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" must be instances of " MODULE_STR ".KDNode");
    
    auto node = reinterpret_cast<obj_KDNode*>(obj);
    
    if(node->parent)
        THROW_PYERR_STRING(ValueError,"\"left\" and \"right\" must not already be attached to another node/scene");
    
    return node;
}

PyObject *obj_KDBranch_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_KDBranch*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                const char *names[] = {"axis","split","left","right"};
                get_arg ga(args,kwds,names,"KDBranch.__new__");
                auto axis = from_pyobject<int>(ga(true));
                auto split = from_pyobject<real>(ga(true));
                auto left = ga(false);
                auto right = ga(false);
                ga.finished();
                
                auto lnode = acceptable_node(left);
                auto rnode = acceptable_node(right);
                
                if(!left && !right)
                    THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" can't both be None");
                
                if(lnode && rnode && !compatible(*lnode,*rnode))
                    THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" must have the same dimension");

                ptr->data() = new kd_branch<module_store>(axis,split,lnode ? lnode->_data : nullptr,rnode ? rnode->_data : nullptr);
                ptr->dimension = (lnode ? lnode : rnode)->dimension();
                auto new_parent = py::borrowed_ref(reinterpret_cast<PyObject*>(ptr));
                if(lnode) lnode->parent = new_parent;
                if(rnode) rnode->parent = new_parent;
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(NULL)
    }
    return reinterpret_cast<PyObject*>(ptr);
}

PyObject *new_obj_node(PyObject *parent,kd_node<module_store> *node,int dimension) {
    if(node->type == LEAF) return reinterpret_cast<PyObject*>(new obj_KDLeaf(py::borrowed_ref(parent),static_cast<kd_leaf<module_store>*>(node)));
    
    assert(node->type == BRANCH);
    return reinterpret_cast<PyObject*>(new obj_KDBranch(py::borrowed_ref(parent),static_cast<kd_branch<module_store>*>(node),dimension));
}

PyObject *obj_KDBranch_get_child(obj_KDBranch *self,void *index) {
    assert(&self->data()->right == (&self->data()->left + 1));
    
    try {  
        return new_obj_node(reinterpret_cast<PyObject*>(self),(&self->data()->left)[reinterpret_cast<size_t>(index)].get(),self->dimension);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_KDBranch_getset[] = {
    {const_cast<char*>("axis"),OBJ_GETTER(obj_KDBranch,self->data()->axis),NULL,NULL,NULL},
    {const_cast<char*>("split"),OBJ_GETTER(obj_KDBranch,self->data()->split),NULL,NULL,NULL},
    {const_cast<char*>("left"),reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(0)},
    {const_cast<char*>("right"),reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(1)},
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_KDBranch,self->dimension),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDBranch::pytype = make_type_object(
    MODULE_STR ".KDBranch",
    sizeof(obj_KDBranch),
    tp_dealloc = destructor_dealloc<obj_KDBranch>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_methods = obj_KDBranch_methods,
    tp_traverse = &kd_tree_item_traverse<obj_KDBranch>,
    tp_clear = &kd_tree_item_clear<obj_KDBranch>,
    tp_getset = obj_KDBranch_getset,
    tp_base = &obj_KDNode::pytype,
    tp_new = &obj_KDBranch_new);


PyObject *obj_Vector_str(wrapped_type<n_vector> *self) {
    try {
        auto &base = self->get_base();
        py::list converted(py::object(py::borrowed_ref(reinterpret_cast<PyObject*>(self))));
        for(int i=0; i<base.dimension(); ++i) converted[i] = py::str(converted[i]);
#if PY_MAJOR_VERSION >= 3
        py::object inner = py::new_ref(py::check_obj(PyUnicode_Join(py::check_obj(PyUnicode_FromString(",")),converted.ref())));
        return PyUnicode_FromFormat("<%U>",inner.ref());
#else
        py::bytes mid = py::new_ref(py::check_obj(_PyString_Join(py::check_obj(PyString_FromString(",")),converted.ref())));
        return PyString_FromFormat("<%s>",mid.data());
#endif
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_repr(wrapped_type<n_vector> *self) {
    try {
        auto &base = self->get_base();
        py::list lt(py::object(py::borrowed_ref(reinterpret_cast<PyObject*>(self))));
#if PY_MAJOR_VERSION >= 3
        return PyUnicode_FromFormat("Vector(%d,%R)",base.dimension(),lt.ref());
#else
        py::bytes lrepr = py::new_ref(PyObject_Repr(lt.ref()));
        return PyString_FromFormat("Vector(%d,%s)",base.dimension(),lrepr.data());
#endif
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_richcompare(wrapped_type<n_vector> *self,PyObject *arg,int op) {
    auto &base = self->get_base();
    
    if(PyObject_TypeCheck(arg,&wrapped_type<n_vector>::pytype)) {
        if(op == Py_EQ || op == Py_NE) {
            auto &b = reinterpret_cast<wrapped_type<n_vector>*>(arg)->get_base();
            return to_pyobject((compatible(base,b) && base == b) == (op == Py_EQ));
        }
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___neg__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(-self->get_base());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector___abs__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(self->get_base().absolute());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector___add__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&wrapped_type<n_vector>::pytype)) {
            if(PyObject_TypeCheck(b,&wrapped_type<n_vector>::pytype)) {
                auto &va = reinterpret_cast<wrapped_type<n_vector>*>(a)->get_base();
                auto &vb = reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base();
                if(!compatible(va,vb)) {
                    PyErr_SetString(PyExc_TypeError,"cannot add vectors of different dimension");
                    return NULL;
                }
                return to_pyobject(va + vb);
            }
        }
    } PY_EXCEPT_HANDLERS(NULL)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___sub__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&wrapped_type<n_vector>::pytype)) {
            if(PyObject_TypeCheck(b,&wrapped_type<n_vector>::pytype)) {
                auto &va = reinterpret_cast<wrapped_type<n_vector>*>(a)->get_base();
                auto &vb = reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base();
                if(!compatible(va,vb)) {
                    PyErr_SetString(PyExc_TypeError,"cannot subtract vectors of different dimension");
                    return NULL;
                }
                return to_pyobject(va - vb);
            }
        }
    } PY_EXCEPT_HANDLERS(NULL)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___mul__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&wrapped_type<n_vector>::pytype)) {
            if(PyNumber_Check(b)) {
                return to_pyobject(reinterpret_cast<wrapped_type<n_vector>*>(a)->get_base() * from_pyobject<real>(b));
            }
        } else {
            if(PyNumber_Check(a)) {
                return to_pyobject(from_pyobject<real>(a) * reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base());
            }
        }
    } PY_EXCEPT_HANDLERS(NULL)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyNumberMethods obj_Vector_number_methods = {
    &obj_Vector___add__,
    &obj_Vector___sub__,
    &obj_Vector___mul__,
#if PY_MAJOR_VERSION < 3
    NULL,
#endif
    NULL,
    NULL,
    NULL,
    reinterpret_cast<unaryfunc>(&obj_Vector___neg__),
    NULL,
    reinterpret_cast<unaryfunc>(&obj_Vector___abs__),
    NULL
};

Py_ssize_t obj_Vector___sequence_len__(wrapped_type<n_vector> *self) {
    return self->get_base().dimension();
}

void v_index_check(const n_vector &v,Py_ssize_t index) {
    if(index < 0 || index >= v.dimension()) THROW_PYERR_STRING(IndexError,"vector index out of range");
}

PyObject *obj_Vector___sequence_getitem__(wrapped_type<n_vector> *self,Py_ssize_t index) {
    try {
        auto &v = self->get_base();
        v_index_check(v,index);
        return to_pyobject(v[index]);
    } PY_EXCEPT_HANDLERS(NULL)
}

PySequenceMethods obj_Vector_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_Vector___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_Vector___sequence_getitem__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject *obj_Vector_square(wrapped_type<n_vector> *self,PyObject *) {
    return to_pyobject(self->get_base().square());
}

PyObject *obj_Vector_axis(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","axis","length"};
        get_arg ga(args,kwds,names,"Vector.axis");
        int dimension = from_pyobject<int>(ga(true));
        int axis = from_pyobject<int>(ga(true));
        PyObject *temp = ga(false);
        real length = temp ? from_pyobject<real>(temp) : real(1);
        ga.finished();

        check_dimension(dimension);
        if(axis < 0 || axis >= dimension) {
            PyErr_SetString(PyExc_ValueError,"axis must be between 0 and dimension-1");
            return NULL;
        }
        return to_pyobject(n_vector::axis(dimension,axis,length));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_absolute(wrapped_type<n_vector> *self,PyObject *) {
    return to_pyobject(self->get_base().absolute());
}

PyObject *obj_Vector_unit(wrapped_type<n_vector> *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().unit());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_apply(wrapped_type<n_vector> *_self,PyObject *_func) {
    try {
        auto &self = _self->get_base();
        
        n_vector r(self.dimension());
        py::object func = py::borrowed_ref(_func);
        
        for(int i=0; i<self.dimension(); ++i)
            r[i] = from_pyobject<real>(func(self[i]));
        
        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_set_c(wrapped_type<n_vector> *self,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"index","value"};
        get_arg ga(args,kwds,names,"Vector.set_c");
        auto index = from_pyobject<Py_ssize_t>(ga(true));
        real value = from_pyobject<real>(ga(true));
        ga.finished();
        
        auto &v = self->get_base();
        
        v_index_check(v,index);
        
        n_vector r = v;
        r[index] = value;
        
        return to_pyobject(std::move(r));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Vector_methods[] = {
    {"square",reinterpret_cast<PyCFunction>(&obj_Vector_square),METH_NOARGS,NULL},
    {"axis",reinterpret_cast<PyCFunction>(&obj_Vector_axis),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"absolute",reinterpret_cast<PyCFunction>(&obj_Vector_absolute),METH_NOARGS,NULL},
    {"unit",reinterpret_cast<PyCFunction>(&obj_Vector_unit),METH_NOARGS,NULL},
    {"apply",reinterpret_cast<PyCFunction>(&obj_Vector_apply),METH_O,NULL},
    {"set_c",reinterpret_cast<PyCFunction>(&obj_Vector_set_c),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyObject *obj_Vector_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"Vector.__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::nullable_object values(py::borrowed_ref(ga(false)));
        ga.finished();
        
        check_dimension(dimension);

        auto ptr = py::check_obj(type->tp_alloc(type,0));
        auto &base = reinterpret_cast<wrapped_type<n_vector>*>(ptr)->alloc_base();
        
        if(values) {
            sized_iter itr(*values,dimension);
            for(int i=0; i<dimension; ++i) {
                base[i] = from_pyobject<real>(itr.next());
            }
            itr.finished();
        }
        
        return ptr;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Vector_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_vector>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject vector_obj_base::pytype = make_type_object(
    MODULE_STR ".Vector",
    sizeof(wrapped_type<n_vector>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_vector> >::value,
    tp_repr = &obj_Vector_repr,
    tp_as_number = &obj_Vector_number_methods,
    tp_as_sequence = &obj_Vector_sequence_methods,
    tp_str = &obj_Vector_str,
    tp_richcompare = &obj_Vector_richcompare,
    tp_methods = obj_Vector_methods,
    tp_getset = obj_Vector_getset,
    tp_new = &obj_Vector_new);


int obj_Camera_setorigin(wrapped_type<n_camera> *self,PyObject *arg,void *) {
    try {
        setter_no_delete(arg);
        self->get_base().origin = from_pyobject<n_vector>(arg);
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
}

PyGetSetDef obj_Camera_getset[] = {
    {const_cast<char*>("origin"),OBJ_GETTER(wrapped_type<n_camera>,self->get_base().origin),reinterpret_cast<setter>(&obj_Camera_setorigin),NULL,NULL},
    {const_cast<char*>("axes"),OBJ_GETTER(wrapped_type<n_camera>,reinterpret_cast<PyObject*>(new obj_CameraAxes(self))),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_camera>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyObject *obj_Camera_normalize(wrapped_type<n_camera> *self,PyObject *) {
    try {
        self->get_base().normalize();
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Camera_translate(wrapped_type<n_camera> *self,PyObject *arg) {
    try {
        self->get_base().translate(from_pyobject<n_vector>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Camera_transform(wrapped_type<n_camera> *self,PyObject *arg) {
    try {
        self->get_base().transform(get_base<n_matrix>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Camera_methods[] = {
    {"normalize",reinterpret_cast<PyCFunction>(&obj_Camera_normalize),METH_NOARGS,NULL},
    {"translate",reinterpret_cast<PyCFunction>(&obj_Camera_translate),METH_O,NULL},
    {"transform",reinterpret_cast<PyCFunction>(&obj_Camera_transform),METH_O,NULL},
    {NULL}
};

PyObject *obj_Camera_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension"};
        get_arg ga(args,kwds,names,"Camera.__new__");
        int dimension = from_pyobject<int>(ga(true));
        ga.finished();
        
        check_dimension(dimension);
        
        PyObject *ptr = py::check_obj(type->tp_alloc(type,0));
        
        new(&reinterpret_cast<wrapped_type<n_camera>*>(ptr)->alloc_base()) n_camera(dimension);
        return ptr;
    } PY_EXCEPT_HANDLERS(NULL)
}

int obj_Camera_init(wrapped_type<n_camera> *self,PyObject *args,PyObject *kwds) {
    auto &base = self->get_base();

    for(int i=0; i<base.dimension()*base.dimension(); ++i) base.t_orientation.data()[i] = 0;
    for(int i=0; i<base.dimension(); ++i) base.t_orientation[i][i] = 1;

    return 0;
}

PyTypeObject camera_obj_base::pytype = make_type_object(
    MODULE_STR ".Camera",
    sizeof(wrapped_type<n_camera>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_camera> >::value,
    tp_methods = obj_Camera_methods,
    tp_getset = obj_Camera_getset,
    tp_init = &obj_Camera_init,
    tp_new = &obj_Camera_new);


PyObject *obj_Matrix___mul__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&wrapped_type<n_matrix>::pytype)) {
            auto &base = reinterpret_cast<wrapped_type<n_matrix>*>(a)->get_base();
            if(PyObject_TypeCheck(b,&wrapped_type<n_matrix>::pytype)) {
                auto &mb = reinterpret_cast<wrapped_type<n_matrix>*>(b)->get_base();
                if(!compatible(base,mb)) {
                    PyErr_SetString(PyExc_TypeError,"cannot multiply matrices of different dimension");
                    return NULL;
                }
                return to_pyobject(base * mb);
            }
            if(PyObject_TypeCheck(b,&wrapped_type<n_vector>::pytype)) {
                auto &vb = reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base();
                if(!compatible(base,vb)) {
                    PyErr_SetString(PyExc_TypeError,"cannot multiply a matrix and a vector of different dimension");
                    return NULL;
                }
                return to_pyobject(base * vb);
            }
        }
    } PY_EXCEPT_HANDLERS(NULL)
    
    return py::incref(Py_NotImplemented);
}

PyNumberMethods obj_Matrix_number_methods = {
    NULL,
    NULL,
    &obj_Matrix___mul__,
#if PY_MAJOR_VERSION < 3
    NULL,
#endif
    NULL
};

Py_ssize_t obj_Matrix___sequence_len__(wrapped_type<n_matrix> *self) {
    return self->get_base().dimension();
}

PyObject *obj_Matrix___sequence_getitem__(wrapped_type<n_matrix> *self,Py_ssize_t index) {
    try {
        auto &base = self->get_base();

        if(index < 0 || index >= base.dimension()) {
            PyErr_SetString(PyExc_IndexError,"matrix row index out of range");
            return NULL;
        }
        return reinterpret_cast<PyObject*>(new obj_MatrixProxy(reinterpret_cast<PyObject*>(self),base.dimension(),base[index]));
    } PY_EXCEPT_HANDLERS(NULL)
}

PySequenceMethods obj_Matrix_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_Matrix___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_Matrix___sequence_getitem__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject *obj_Matrix_scale(PyObject*,PyObject *args) {
    try {
        if(PyTuple_GET_SIZE(args) == 1) {
            if(PyObject_TypeCheck(PyTuple_GET_ITEM(args,0),&wrapped_type<n_vector>::pytype)) {
                return to_pyobject(n_matrix::scale(
                    reinterpret_cast<wrapped_type<n_vector>*>(PyTuple_GET_ITEM(args,0))->get_base()));
            }
        } else if(PyTuple_GET_SIZE(args) == 2
                && is_int_or_long(PyTuple_GET_ITEM(args,0))
                && PyNumber_Check(PyTuple_GET_ITEM(args,1))) {
            int dimension = from_pyobject<int>(PyTuple_GET_ITEM(args,0));
            check_dimension(dimension);
            return to_pyobject(n_matrix::scale(
                dimension,
                from_pyobject<float>(PyTuple_GET_ITEM(args,1))));
        }

        NoSuchOverload(args);
        return NULL;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_rotation(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"a","b","theta"};
        get_arg ga(args,kwds,names,"Matrix.rotation");
        auto a = from_pyobject<n_vector>(ga(true));
        auto b = from_pyobject<n_vector>(ga(true));
        float theta = from_pyobject<float>(ga(true));
        ga.finished();
        
        if(!compatible(a,b)) {
            PyErr_SetString(PyExc_TypeError,"cannot produce rotation matrix using vectors of different dimension");
            return NULL;
        }
        
        return to_pyobject(n_matrix::rotation(a,b,theta));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_identity(PyObject*,PyObject *arg) {
    try {
        int dimension = from_pyobject<int>(arg);
        check_dimension(dimension);
        return to_pyobject(n_matrix::identity(dimension));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_determinant(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().determinant());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_inverse(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().inverse());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_transpose(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().transpose());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Matrix_methods[] = {
    {"scale",reinterpret_cast<PyCFunction>(&obj_Matrix_scale),METH_VARARGS|METH_STATIC,NULL},
    {"rotation",reinterpret_cast<PyCFunction>(&obj_Matrix_rotation),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"identity",reinterpret_cast<PyCFunction>(&obj_Matrix_identity),METH_O|METH_STATIC,NULL},
    {"determinant",reinterpret_cast<PyCFunction>(&obj_Matrix_determinant),METH_NOARGS,NULL},
    {"inverse",reinterpret_cast<PyCFunction>(&obj_Matrix_inverse),METH_NOARGS,NULL},
    {"transpose",reinterpret_cast<PyCFunction>(&obj_Matrix_transpose),METH_NOARGS,NULL},
    {NULL}
};

void copy_row(n_matrix &m,py::object values,int row,int len) {
    sized_iter itr(values,len);
    for(int col=0; col<len; ++col) {
        m[row][col] = from_pyobject<real>(itr.next());
    }
    itr.finished();
}

PyObject *obj_Matrix_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"Matrix.__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::object values(py::borrowed_ref(ga(true)));
        ga.finished();
        
        check_dimension(dimension);

        auto ptr = py::check_obj(type->tp_alloc(type,0));
        auto &base = reinterpret_cast<wrapped_type<n_matrix>*>(ptr)->alloc_base();
        
        new(&base) n_matrix(dimension);
        
        sized_iter itr(values,0);
        py::object item = itr.next();
        if(PyNumber_Check(item.ref())) {
            itr.expected_len = dimension * dimension;
            
            base[0][0] = from_pyobject<real>(item);
            for(int i=1; i<dimension*dimension; ++i) {
                base.data()[i] = from_pyobject<real>(itr.next());
            }
        } else {
            itr.expected_len = dimension;
            copy_row(base,item,0,dimension);
            
            for(int row=1; row<dimension; ++row) copy_row(base,itr.next(),row,dimension);
        }
        
        itr.finished();

        return ptr;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_values(wrapped_type<n_matrix> *self,void*) {
    try {
        auto &base = self->get_base();
        return reinterpret_cast<PyObject*>(
            new obj_MatrixProxy(reinterpret_cast<PyObject*>(self),base.dimension() * base.dimension(),base.data()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Matrix_getset[] = {
    {const_cast<char*>("values"),reinterpret_cast<getter>(&obj_Matrix_values),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_matrix>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject matrix_obj_base::pytype = make_type_object(
    MODULE_STR ".Matrix",
    sizeof(wrapped_type<n_matrix>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_matrix> >::value,
    tp_as_number = &obj_Matrix_number_methods,
    tp_as_sequence = &obj_Matrix_sequence_methods,
    tp_methods = obj_Matrix_methods,
    tp_getset = obj_Matrix_getset,
    tp_new = &obj_Matrix_new);


PyObject *aabb_split(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds,bool right) {
    wrapped_type<n_aabb> *r;
    int axis;
    real split;
    
    try {
        const char *names[] = {"axis","split"};
        get_arg ga(args,kwds,names,right ? "AABB.right" : "AABB.left");
        axis = from_pyobject<int>(ga(true));
        split = from_pyobject<real>(ga(false));
        ga.finished();
        
        auto &base = self->get_base();
        
        if(axis < 0 || axis >= base.start.dimension()) {
            PyErr_SetString(PyExc_ValueError,"invalid axis");
            return NULL;
        }
        if(split <= base.start[axis] || split >= base.end[axis]) {
            PyErr_SetString(PyExc_ValueError,"\"split\" must be inside the box within the given axis");
            return NULL;
        }
    
        r = new wrapped_type<n_aabb>(base);
    } PY_EXCEPT_HANDLERS(NULL)
    
    if(right) r->cast_base().start[axis] = split;
    else r->cast_base().end[axis] = split;
    
    return reinterpret_cast<PyObject*>(r);
}

PyObject *obj_AABB_left(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,false);
}

PyObject *obj_AABB_right(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,true);
}

PyObject *intersect_type_error(PyObject *o) {
    if(Py_TYPE(o) == &triangle_obj_common::pytype || Py_TYPE(o) == &solid_obj_common::pytype) {
        PyErr_SetString(PyExc_TypeError,
            "Instances of Triangle and Solid cannot be used directly. Use TrianglePrototype and SolidPrototype instead.");
    } else {
        PyErr_SetString(PyExc_TypeError,
            "object must be an instance of " MODULE_STR ".PrimitivePrototype");
    }
    return NULL;
}

template<typename T> PyObject *try_intersects(const n_aabb &base,PyObject *obj) {
    if(PyObject_TypeCheck(obj,&wrapped_type<T>::pytype)) {
        auto p = reinterpret_cast<wrapped_type<T>*>(obj);
        
        if(base.dimension() != p->get_base().dimension())
            THROW_PYERR_STRING(TypeError,"cannot perform intersection test on object with different dimension");
        
        return to_pyobject(base.intersects(p->get_base()));
    }
    return nullptr;
}

PyObject *obj_AABB_intersects(wrapped_type<n_aabb> *self,PyObject *obj) {
    try {
        auto &base = self->get_base();
        
        auto r = try_intersects<n_triangle_prototype>(base,obj);
        if(r) return r;
        r = try_intersects<n_solid_prototype>(base,obj);
        if(r) return r;

        return intersect_type_error(obj);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_AABB_methods[] = {
    {"left",reinterpret_cast<PyCFunction>(&obj_AABB_left),METH_VARARGS|METH_KEYWORDS,NULL},
    {"right",reinterpret_cast<PyCFunction>(&obj_AABB_right),METH_VARARGS|METH_KEYWORDS,NULL},
    {"intersects",reinterpret_cast<PyCFunction>(&obj_AABB_intersects),METH_O,NULL},
    {NULL}
};

PyObject *obj_AABB_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return NULL;
    
    try {
        try {
            const char *names[] = {"dimension","start","end"};
            get_arg ga(args,kwds,names,"AABB.__new__");
            int dimension = from_pyobject<int>(ga(true));
            PyObject *start_obj = ga(false);
            PyObject *end_obj = ga(false);
            ga.finished();
            
            check_dimension(dimension);
            
            auto &base = reinterpret_cast<wrapped_type<n_aabb>*>(ptr)->alloc_base();
            
            if(start_obj) {
                auto start = from_pyobject<n_vector>(start_obj);
                if(dimension != start.dimension())
                    THROW_PYERR_STRING(TypeError,"\"start\" has a dimension different from \"dimension\"");
                new(&base.start) n_vector(start);
            } else new(&base.start) n_vector(dimension,std::numeric_limits<real>::lowest());
        

            if(end_obj) {
                auto end = from_pyobject<n_vector>(end_obj);
                if(dimension != end.dimension())
                    THROW_PYERR_STRING(TypeError,"\"end\" has a dimension different from \"dimension\"");
                new(&base.end) n_vector(end);
            } else new(&base.end) n_vector(dimension,std::numeric_limits<real>::max());
 
        } catch(...) {
            Py_DECREF(ptr);
        }
    } PY_EXCEPT_HANDLERS(NULL)
    
    return ptr;
}

PyGetSetDef obj_AABB_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().dimension()),NULL,NULL,NULL},
    {const_cast<char*>("start"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().start),NULL,NULL,NULL},
    {const_cast<char*>("end"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().end),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject aabb_obj_base::pytype = make_type_object(
    MODULE_STR ".AABB",
    sizeof(wrapped_type<n_aabb>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_aabb> >::value,
    tp_methods = obj_AABB_methods,
    tp_getset = obj_AABB_getset,
    tp_new = &obj_AABB_new);


PyTypeObject obj_PrimitivePrototype::pytype = make_type_object(
    MODULE_STR ".PrimitivePrototype",
    sizeof(obj_PrimitivePrototype),
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the PrimitivePrototype type cannot be instantiated directly");
        return nullptr;
    });


PyObject *obj_TrianglePrototype_realize(obj_TrianglePrototype *self,PyObject*) {
    try {
        n_triangle_prototype &base = self->get_base();
        
        return reinterpret_cast<PyObject*>(obj_Triangle::create(
            base.items()[0].point,
            base.face_normal,
            [&](int i){ return base.items()[i+1].edge_normal; }));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_TrianglePrototype_methods[] = {
    {"realize",reinterpret_cast<PyCFunction>(&obj_TrianglePrototype_realize),METH_NOARGS,NULL},
    {NULL}
};

PyObject *obj_TrianglePrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"points"};
        get_arg ga(args,kwds,names,"TrianglePrototype.__new__");
        PyObject *points_obj = ga(true);
        ga.finished();
        
        auto points = points_for_triangle(points_obj);
        int dim = points[0].dimension();
        
        auto ptr = py::check_obj(type->tp_alloc(type,obj_TrianglePrototype::item_size ? 0 : dim));
        n_triangle_prototype &base = reinterpret_cast<obj_TrianglePrototype*>(ptr)->alloc_base(dim);
        
        try {
            new(&base.aabb_max) n_vector(dim,std::numeric_limits<real>::lowest());
            new(&base.aabb_min) n_vector(dim,std::numeric_limits<real>::max());
            
            for(int i=0; i<dim; ++i) {
                for(int j=0; j<dim; ++j) {
                    if(points[i][j] > base.aabb_max[j]) base.aabb_max[j] = points[i][j];
                    if(points[i][j] < base.aabb_min[j]) base.aabb_min[j] = points[i][j];
                }
            }

            smaller<n_matrix> tmp(dim-1);
            
            module_store::smaller_init_array<n_vector> vsides(dim-1,[&](int i) -> n_vector { return points[i+1] - points[0]; });
            new(&base.face_normal) n_vector(dim);
            cross_(base.face_normal,tmp,static_cast<n_vector*>(vsides));
            auto square = base.face_normal.square();
            
            new(&base.items()[0]) n_triangle_point(points[0],n_vector(dim,0));
            
            for(int i=1; i<dim; ++i) {
                auto old = vsides[i-1];
                vsides[i-1] = base.face_normal;
                
                new(&base.items()[i]) n_triangle_point(points[i],n_vector(dim));
                
                cross_(base.items()[i].edge_normal,tmp,static_cast<n_vector*>(vsides));
                vsides[i-1] = old;
                base.items()[i].edge_normal /= square;
                
                base.items()[0].edge_normal -= base.items()[i].edge_normal;
            }
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_TrianglePrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().face_normal.dimension()),NULL,NULL,NULL},
    {const_cast<char*>("face_normal"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().face_normal),NULL,NULL,NULL},
    {const_cast<char*>("point_data"),OBJ_GETTER(
        obj_TrianglePrototype,
        reinterpret_cast<PyObject*>(new obj_TrianglePointData(obj_self,self->get_base().dimension(),self->get_base().items()))),NULL,NULL,NULL},
    {const_cast<char*>("aabb_max"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().aabb_max),NULL,NULL,NULL},
    {const_cast<char*>("aabb_min"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().aabb_min),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject triangle_prototype_obj_base::pytype = make_type_object(
    MODULE_STR ".TrianglePrototype",
    obj_TrianglePrototype::base_size,
    tp_itemsize = obj_TrianglePrototype::item_size,
    tp_dealloc = destructor_dealloc<obj_TrianglePrototype>::value,
    tp_methods = obj_TrianglePrototype_methods,
    tp_getset = obj_TrianglePrototype_getset,
    tp_base = &obj_PrimitivePrototype::pytype,
    tp_new = &obj_TrianglePrototype_new);


PyGetSetDef obj_TrianglePointDatum_getset[] = {
    {const_cast<char*>("point"),OBJ_GETTER(wrapped_type<n_triangle_point>,self->get_base().point),NULL,NULL,NULL},
    {const_cast<char*>("edge_normal"),OBJ_GETTER(wrapped_type<n_triangle_point>,self->get_base().edge_normal),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject triangle_point_obj_base::pytype = make_type_object(
    MODULE_STR ".TrianglePointDatum",
    sizeof(wrapped_type<n_triangle_point>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_triangle_point> >::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_getset = obj_TrianglePointDatum_getset,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"The TrianglePointDatum type cannot be instantiated directly");
        return nullptr;
    });


PyObject *obj_SolidPrototype_realize(wrapped_type<n_solid_prototype> *self,PyObject*) {
    return py::incref(self->get_base().p.ref());
}

PyMethodDef obj_SolidPrototype_methods[] = {
    {"realize",reinterpret_cast<PyCFunction>(&obj_SolidPrototype_realize),METH_NOARGS,NULL},
    {NULL}
};

PyObject *obj_SolidPrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return NULL;
    
    try {
        try {
            const char *names[] = {"type","orientation","position"};
            get_arg ga(args,kwds,names,"SolidPrototype.__new__");
            auto type = from_pyobject<solid_type>(ga(true));
            auto &orientation = get_base<n_matrix>(ga(true));
            auto position = from_pyobject<n_vector>(ga(true));
            ga.finished();
            
            if(!compatible(orientation,position))
                THROW_PYERR_STRING(TypeError,"the orientation and position must have the same dimension");
            
            auto &base = reinterpret_cast<wrapped_type<n_solid_prototype>*>(ptr)->alloc_base();
            
            new(&base.p) py::pyptr<obj_Solid>(new obj_Solid(type,orientation,position));

            if(type == CUBE) {
                new(&base.aabb_max) n_vector(position.dimension(),0);
                for(int i=0; i<position.dimension(); ++i) base.aabb_max += base.p->cube_component(i).apply(static_cast<real (*)(real)>(&std::abs));
                new(&base.aabb_min) n_vector(base.p->position - base.aabb_max);
                base.aabb_max += base.p->position;
            } else {
                assert(type == SPHERE);

                new(&base.aabb_max) n_vector(position.dimension());
                new(&base.aabb_min) n_vector(position.dimension());

                for(int i=0; i<position.dimension(); ++i) {
                    // equivalent to: (orientation.transpose() * n_vector::axis(position->dimension(),i)).unit()
                    n_vector normal = orientation[i].unit();
                    
                    real max = dot(n_vector::axis(position.dimension(),i) - position,normal);
                    real min = dot(n_vector::axis(position.dimension(),i,-1) - position,normal);
                    if(min > max) std::swap(max,min);

                    base.aabb_max[i] = max;
                    base.aabb_min[i] = min;
                }
            }
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_SolidPrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().dimension()),NULL,NULL,NULL},
    {const_cast<char*>("type"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().p->type),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().p->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().p->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().p->position),NULL,NULL,NULL},
    {const_cast<char*>("aabb_max"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().aabb_max),NULL,NULL,NULL},
    {const_cast<char*>("aabb_min"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().aabb_min),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject solid_prototype_obj_base::pytype = make_type_object(
    MODULE_STR ".SolidPrototype",
    sizeof(wrapped_type<n_solid_prototype>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_solid_prototype> >::value,
    tp_methods = obj_SolidPrototype_methods,
    tp_getset = obj_SolidPrototype_getset,
    tp_base = &obj_PrimitivePrototype::pytype,
    tp_new = &obj_SolidPrototype_new);


PyObject *obj_dot(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"a","b"};
        get_arg ga(args,kwds,names,"dot");
        auto a = from_pyobject<n_vector>(ga(true));
        auto b = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        if(!compatible(a,b)) {
            PyErr_SetString(PyExc_TypeError,"cannot perform dot product on vectors of different dimension");
            return NULL;
        }
        return to_pyobject(dot(a,b));

    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_cross(PyObject*,PyObject *arg) {
    try {
        auto vs = collect<n_vector>(arg);
        
        if(!vs.size()) {
            PyErr_SetString(PyExc_TypeError,"argument must be a sequence of vectors");
            return NULL;
        }

        int dim = vs[0].dimension();
        for(size_t i=1; i<vs.size(); ++i) {
            if(vs[i].dimension() != dim) {
                PyErr_SetString(PyExc_TypeError,"the vectors must all have the same dimension");
                return NULL;
            }
        }
        if(size_t(dim) != vs.size()+1) {
            PyErr_SetString(PyExc_ValueError,"the number of vectors must be exactly one less than their dimension");
            return NULL;
        }

        return to_pyobject(cross<module_store>(vs.data()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef func_table[] = {
    {"dot",reinterpret_cast<PyCFunction>(&obj_dot),METH_VARARGS|METH_KEYWORDS,NULL},
    {"cross",&obj_cross,METH_O,NULL},
    {NULL}
};


PyTypeObject *classes[] = {
    &obj_BoxScene::pytype,
    &obj_CompositeScene::pytype,
    &obj_Primitive::pytype,
    &obj_Solid::pytype,
    &obj_Triangle::pytype,
    &obj_FrozenVectorView::pytype,
    &obj_KDNode::pytype,
    &obj_KDLeaf::pytype,
    &obj_KDBranch::pytype,
    &wrapped_type<n_vector>::pytype,
    &obj_MatrixProxy::pytype,
    &wrapped_type<n_camera>::pytype,
    &obj_CameraAxes::pytype,
    &wrapped_type<n_matrix>::pytype,
    &wrapped_type<n_aabb>::pytype,
    &obj_PrimitivePrototype::pytype,
    &obj_TrianglePrototype::pytype,
    &wrapped_type<n_triangle_point>::pytype,
    &obj_TrianglePointData::pytype,
    &wrapped_type<n_solid_prototype>::pytype};


#if PY_MAJOR_VERSION >= 3
#define INIT_ERR_VAL 0

struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    MODULE_STR,
    NULL,
    0,
    func_table,
    NULL,
    NULL,
    NULL,
    NULL
};

extern "C" SHARED(PyObject) * APPEND_MODULE_NAME(PyInit_)() {
#else
#define INIT_ERR_VAL

extern "C" SHARED(void) APPEND_MODULE_NAME(init)() {
#endif
    using namespace py;

#ifdef INHERIT_FROM_RENDER_SCENE
    try {
        object scene = object(new_ref(check_obj(PyImport_ImportModule(PACKAGE_STR ".render")))).attr("Scene");
        if(!PyType_CheckExact(scene.ref())) THROW_PYERR_STRING(TypeError,"render.Scene is supposed to be a class");

        auto stype = reinterpret_cast<PyTypeObject*>(scene.ref());
        obj_BoxScene::pytype.tp_base = stype;
        obj_CompositeScene::pytype.tp_base = stype;
    } PY_EXCEPT_HANDLERS(INIT_ERR_VAL)
#endif

    for(auto &cls : classes) {
        if(UNLIKELY(PyType_Ready(cls) < 0)) return INIT_ERR_VAL;
    }

#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3(MODULE_STR,func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;
        
    for(auto &cls : classes) {
        add_class(m,cls->tp_name + sizeof(MODULE_STR),cls);
    }

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

