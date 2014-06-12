
#include "py_common.hpp"

#include <assert.h>

#include "pyobject.hpp"
#include "fixed_geometry.hpp"
#include "var_geometry.hpp"
#include "tracer.hpp"


#define _STR(X) #X
#define STR(X) _STR(X)
#define MODULE_STR STR(MODULE_NAME)
#define _APPEND(X,Y) X ## Y
#define APPEND(X,Y) _APPEND(X,Y)
#define APPEND_MODULE_NAME(base) APPEND(base,MODULE_NAME)

#define PACKAGE ntracer
#define PACKAGE_STR STR(PACKAGE)

#define FULL_MODULE_STR PACKAGE_STR "." MODULE_STR


using namespace type_object_abbrev;


typedef vector<module_store> n_vector;
typedef matrix<module_store> n_matrix;
typedef camera<module_store> n_camera;
typedef solid_prototype<module_store> n_solid_prototype;
typedef triangle_prototype<module_store> n_triangle_prototype;
typedef triangle_point<module_store> n_triangle_point;
typedef aabb<module_store> n_aabb;
typedef point_light<module_store> n_point_light;
typedef global_light<module_store> n_global_light;


package_common package_common_data = {nullptr};


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
    py::nullable<py::object> r = py::next(itr);
    if(!r) {
        PyErr_Format(PyExc_ValueError,"too few items in object, expected %d",expected_len);
        throw py_error_set();
    }
    return *r;
}

void sized_iter::finished() const {
    py::nullable<py::object> r = py::next(itr);
    if(r) {
        PyErr_Format(PyExc_ValueError,"too many items in object, expected %d",expected_len);
        throw py_error_set();
    }
}

template<typename T1,typename T2> inline bool compatible(const T1 &a,const T2 &b) {
    return a.dimension() == b.dimension();
}

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
        auto size = PyObject_LengthHint(src,0);
        if(size < 0) throw py_error_set();
        if(size > 0) items.reserve(size);
        auto itr = py::iter(src);
        while(auto item = py::next(itr)) {
            items.push_back(from_pyobject<T>(item.ref()));
        }
    }
    return items;
}


PyTypeObject *color_obj_base::_pytype = nullptr;
PyTypeObject *material::_pytype = nullptr;


#define SIMPLE_WRAPPER(ROOT) \
struct ROOT ## _obj_base { \
    CONTAINED_PYTYPE_DEF \
    PyObject_HEAD \
}; \
template<> struct _wrapped_type<n_ ## ROOT> { \
    typedef simple_py_wrapper<n_ ## ROOT,ROOT ## _obj_base> type; \
}


SIMPLE_WRAPPER(vector);
SIMPLE_WRAPPER(matrix);
SIMPLE_WRAPPER(camera);


template<typename T,typename Base> PyObject *to_pyobject(const ::impl::vector_expr<T,Base> &e) {
    return to_pyobject(n_vector(e));
}


struct obj_BoxScene {
    CONTAINED_PYTYPE_DEF
    
    PyObject_HEAD
    box_scene<module_store> &(obj_BoxScene::*_get_base)();
    box_scene<module_store> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    obj_BoxScene(int d) : _get_base(&obj_BoxScene::get_base), base(d), idict(nullptr), weaklist(nullptr) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
    obj_BoxScene(box_scene<module_store> const &b) : _get_base(&obj_BoxScene::get_base), base(b), idict(nullptr), weaklist(nullptr) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
    ~obj_BoxScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    box_scene<module_store> &cast_base() { return base; }
    box_scene<module_store> &get_base() { return base; }
};

template<> struct _wrapped_type<box_scene<module_store> > {
    typedef obj_BoxScene type;
};


constexpr char matrix_proxy_name[] = FULL_MODULE_STR ".MatrixProxy";
typedef py::obj_array_adapter<real,matrix_proxy_name,false,true> obj_MatrixProxy;


struct obj_CameraAxes {
    CONTAINED_PYTYPE_DEF
    
    PyObject_HEAD
    py::pyptr<wrapped_type<n_camera> > base;
    PY_MEM_NEW_DELETE

    obj_CameraAxes(wrapped_type<n_camera> *base) : base(py::borrowed_ref(reinterpret_cast<PyObject*>(base))) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
};


struct obj_CompositeScene;
struct composite_scene_obj_base {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
    composite_scene<module_store> &(obj_CompositeScene::*_get_base)();
};
struct obj_CompositeScene : simple_py_wrapper<composite_scene<module_store>,composite_scene_obj_base> {
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    obj_CompositeScene(const n_aabb &bound,kd_node<module_store> *data) : simple_py_wrapper(bound,data), idict(nullptr), weaklist(nullptr) {
        _get_base = &obj_CompositeScene::get_base;
    }
    
    ~obj_CompositeScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
};

template<> struct _wrapped_type<composite_scene<module_store> > {
    typedef obj_CompositeScene type;
};

template<typename T> void ensure_unlocked(T *s) {
    if(s->get_base().locked) {
        PyErr_SetString(PyExc_RuntimeError,"the scene is locked for reading");
        throw py_error_set();
    }
}

template<typename T> void ensure_unlocked(const py::pyptr<T> &s) {
    if(s) ensure_unlocked(s.get());
}


constexpr char frozen_vector_view_name[] = FULL_MODULE_STR ".FrozenVectorView";
typedef py::obj_array_adapter<n_vector,frozen_vector_view_name,true,true> obj_FrozenVectorView;


struct obj_Primitive {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

typedef solid<module_store> obj_Solid;
typedef triangle<module_store> obj_Triangle;

template<> primitive<module_store> *checked_py_cast<primitive<module_store> >(PyObject *o) {
    if(UNLIKELY(Py_TYPE(o) != solid_obj_common::pytype() && Py_TYPE(o) != triangle_obj_common::pytype())) {
        PyErr_Format(PyExc_TypeError,"object is not an instance of %s",obj_Primitive::pytype()->tp_name);
        throw py_error_set();
    }
    return reinterpret_cast<primitive<module_store>*>(o);
}


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
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
    PY_MEM_GC_NEW_DELETE
    py::nullable<py::object> parent;
    kd_node<module_store> *_data;
    
    int dimension() const;
    
protected:
    obj_KDNode(py::nullable<py::object> parent,kd_node<module_store> *data) : parent(parent), _data(data) {}
    ~obj_KDNode() = default;
};

struct obj_KDBranch : obj_KDNode {
    CONTAINED_PYTYPE_DEF
    
    int dimension;
    
    kd_branch<module_store> *&data() {
        assert(_data);
        return reinterpret_cast<kd_branch<module_store>*&>(_data);
    }
    
    kd_branch<module_store> *data() const {
        assert(_data);
        return static_cast<kd_branch<module_store>*>(_data);
    }
    
    obj_KDBranch(py::nullable<py::object> parent,kd_branch<module_store> *data,int dimension) : obj_KDNode(parent,data), dimension(dimension) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
    ~obj_KDBranch() {
        if(!parent) delete data();
    }
};

struct obj_KDLeaf : obj_KDNode {
    CONTAINED_PYTYPE_DEF
    
    kd_leaf<module_store> *&data() {
        assert(_data);
        return reinterpret_cast<kd_leaf<module_store>*&>(_data);
    }
    
    kd_leaf<module_store> *data() const {
        assert(_data);
        return static_cast<kd_leaf<module_store>*>(_data);
    }
    
    obj_KDLeaf(py::nullable<py::object> parent,kd_leaf<module_store> *data) : obj_KDNode(parent,data) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
    ~obj_KDLeaf() {
        if(!parent) delete data();
    }
};

int obj_KDNode::dimension() const {
    if(Py_TYPE(this) == obj_KDBranch::pytype()) return static_cast<const obj_KDBranch*>(this)->dimension;
    
    assert(Py_TYPE(this) == obj_KDLeaf::pytype());
    return static_cast<const obj_KDLeaf*>(this)->data()->dimension();
}

template<> obj_KDNode *checked_py_cast<obj_KDNode>(PyObject *o) {
    if(Py_TYPE(o) != obj_KDBranch::pytype() && Py_TYPE(o) != obj_KDLeaf::pytype())
        THROW_PYERR_STRING(TypeError,"object is not an an instance of " MODULE_STR ".KDNode");
                
    return reinterpret_cast<obj_KDNode*>(o);
}


struct aabb_obj_base {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

template<> struct _wrapped_type<n_aabb> {
    typedef simple_py_wrapper<n_aabb,aabb_obj_base,true> type;
};


struct obj_PrimitivePrototype {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
    
    primitive_prototype<module_store> &get_base();
};


struct triangle_prototype_obj_base {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

/* Whether this object is variable-sized or not depends on whether
   n_triangle_prototype can vary in size and whether n_triangle_prototype can be
   stored in-place (which it can't if it requires extra alignment). If it is
   stored in-place, care must be taken to ensure it has enough room  */
template<bool Var,bool InPlace> struct tproto_base : triangle_prototype_obj_base {
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
    
    static const size_t base_size;
    static const size_t item_size = 0;
};
template<bool Var> const size_t tproto_base<Var,false>::base_size = sizeof(tproto_base<Var,false>);

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
    static const size_t item_size = n_triangle_prototype::item_size;
};
#define COMMA_WORKAROUND tproto_base<true,true>
const size_t tproto_base<true,true>::base_size = n_triangle_prototype::item_offset + offsetof(COMMA_WORKAROUND,base);
#undef COMMA_WORKAROUND

typedef tproto_base<!module_store::required_d,(alignof(n_triangle_prototype) <= PYOBJECT_ALIGNMENT)> obj_TrianglePrototype;
template<> struct _wrapped_type<n_triangle_prototype> {
    typedef obj_TrianglePrototype type;
};


struct n_detached_triangle_point {
    n_vector point;
    n_vector edge_normal;
    
    n_detached_triangle_point(const n_triangle_point &tp) : point(tp.point), edge_normal(tp.edge_normal) {}
};

SIMPLE_WRAPPER(detached_triangle_point);

PyObject *to_pyobject(const n_triangle_point &tp) {
    return reinterpret_cast<PyObject*>(new wrapped_type<n_detached_triangle_point>(tp));
}

constexpr char triangle_point_data_name[] = FULL_MODULE_STR ".TrianglePointData";
typedef py::obj_array_adapter<n_triangle_point,triangle_point_data_name,false,true> obj_TrianglePointData;


SIMPLE_WRAPPER(solid_prototype);

primitive_prototype<module_store> &obj_PrimitivePrototype::get_base() {
    if(PyObject_TypeCheck(reinterpret_cast<PyObject*>(this),obj_TrianglePrototype::pytype()))
        return reinterpret_cast<obj_TrianglePrototype*>(this)->get_base();
    
    assert(PyObject_TypeCheck(reinterpret_cast<PyObject*>(this),wrapped_type<n_solid_prototype>::pytype()));
    return reinterpret_cast<wrapped_type<n_solid_prototype>*>(this)->get_base();
}

SIMPLE_WRAPPER(point_light);
SIMPLE_WRAPPER(global_light);

template<typename T> struct cs_light_list : T {
    static PySequenceMethods sequence_methods;
    static PyMethodDef methods[];
    
    CONTAINED_PYTYPE_DEF
    PY_MEM_NEW_DELETE
    PyObject_HEAD
    py::pyptr<obj_CompositeScene> parent;
    
    cs_light_list(obj_CompositeScene *parent) : parent(py::borrowed_ref(reinterpret_cast<PyObject*>(parent))) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
};

struct point_light_list_base {
    typedef n_point_light item_t;
    
    static const char *name;
    static std::vector<item_t> &value(obj_CompositeScene *parent) {
        return parent->cast_base().point_lights;
    }
};
const char *point_light_list_base::name = FULL_MODULE_STR ".PointLightList";

struct global_light_list_base {
    typedef n_global_light item_t;
    
    static const char *name;
    static std::vector<item_t> &value(obj_CompositeScene *parent) {
        return parent->cast_base().global_lights;
    }
};
const char *global_light_list_base::name = FULL_MODULE_STR ".GlobalLightList";


template<> n_vector from_pyobject<n_vector>(PyObject *o) {
    if(PyTuple_Check(o)) {
        if(sizeof(Py_ssize_t) > sizeof(int) && UNLIKELY(PyTuple_GET_SIZE(o) > std::numeric_limits<int>::max()))
            THROW_PYERR_STRING(ValueError,"too many items for a vector");
        
        check_dimension(PyTuple_GET_SIZE(o));
        return {int(PyTuple_GET_SIZE(o)),[=](int i){ return from_pyobject<real>(PyTuple_GET_ITEM(o,i)); }};
    }
    if(Py_TYPE(o) == obj_MatrixProxy::pytype()) {
        auto &mp = reinterpret_cast<obj_MatrixProxy*>(o)->data;
        return {int(mp.size),[&](int i){ return mp.items[i]; }};
    }
    return get_base<n_vector>(o);
}



PyObject *obj_BoxScene_set_camera(obj_BoxScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        
        auto &c = get_base<n_camera>(arg);
        if(UNLIKELY(!compatible(self->base,c))) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return nullptr;
        }
        
        self->base.cam = c;
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_BoxScene_get_camera(obj_BoxScene *self,PyObject *) {
    try {
        return to_pyobject(self->base.cam);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_BoxScene_set_fov(obj_BoxScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        self->base.fov = from_pyobject<real>(arg);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
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
                real d = std::get<0>(get_arg::get_args("BoxScene.__new__",args,kwds,param<real>("dimension")));
                
                check_dimension(d);
                
                new(&ptr->base) box_scene<module_store>(d);
                ptr->_get_base = &obj_BoxScene::get_base;
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(nullptr)

        ptr->weaklist = nullptr;
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

PyTypeObject obj_BoxScene::_pytype = make_type_object(
    FULL_MODULE_STR ".BoxScene",
    sizeof(obj_BoxScene),
    tp_dealloc = destructor_dealloc<obj_BoxScene>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &traverse_idict<obj_BoxScene>,
    tp_clear = &clear_idict<obj_BoxScene>,
    tp_weaklistoffset = offsetof(obj_BoxScene,weaklist),
    tp_methods = obj_BoxScene_methods,
    tp_members = obj_BoxScene_members,
    tp_getset = obj_BoxScene_getset,
    tp_dictoffset = offsetof(obj_BoxScene,idict),
    tp_new = &obj_BoxScene_new);


PyObject *obj_CompositeScene_set_camera(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        
        auto &c = get_base<n_camera>(arg);
        if(UNLIKELY(!compatible(self->get_base(),c))) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return nullptr;
        }
        
        self->get_base().cam = c;
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_CompositeScene_get_camera(obj_CompositeScene *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().cam);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_CompositeScene_set_ambient(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        read_color(self->get_base().ambient,arg);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> T &light_compat_check(const composite_scene<module_store> &scene,T &light) {
    if(!compatible(scene,light)) THROW_PYERR_STRING(TypeError,"the light must have the same dimension as the scene");
    return light;
}

PyObject *obj_CompositeScene_add_light(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        auto &base = self->get_base();
        if(auto lobj = get_base_if_is_type<n_point_light>(arg)) base.point_lights.push_back(light_compat_check(base,*lobj));
        else if(auto lobj = get_base_if_is_type<n_global_light>(arg)) base.global_lights.push_back(light_compat_check(base,*lobj));
        else {
            PyErr_SetString(PyExc_TypeError,"object must be an instance of PointLight or GlobalLight");
            return nullptr;
        }

        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_CompositeScene_set_background(obj_CompositeScene *self,PyObject *args,PyObject *kwds) {
    try {
        ensure_unlocked(self);
        auto &base = self->get_base();
        
        const char *names[] = {"c1","c2","c3","axis"};
        get_arg ga(args,kwds,names,"CompositeScene.set_background");
        color c1;
        read_color(c1,ga(true));
        
        color c2;
        auto tmp = ga(false);
        if(tmp) read_color(c2,tmp);
        else c2 = c1;
        
        color c3;
        tmp = ga(false);
        if(tmp) read_color(c3,tmp);
        else c3 = c1;
        
        int axis = composite_scene<module_store>::default_bg_gradient_axis;
        tmp = ga(false);
        if(tmp) {
            axis = from_pyobject<int>(tmp);
            if(UNLIKELY(axis < 0 || axis >= base.dimension())) {
                PyErr_SetString(PyExc_ValueError,"\"axis\" must be between 0 and one less than the dimension of the scene");
                return nullptr;
            }
        }
        
        ga.finished();
        
        base.bg1 = c1;
        base.bg2 = c2;
        base.bg3 = c3;
        base.bg_gradient_axis = axis;
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

#define CS_SET_ATTR(ATTR) [](PyObject *_self,PyObject *arg) -> PyObject* { \
    auto self = reinterpret_cast<obj_CompositeScene*>(_self); \
    try { \
        ensure_unlocked(self); \
        self->get_base().ATTR = from_pyobject<typename std::decay<decltype(self->get_base().ATTR)>::type>(arg); \
        Py_RETURN_NONE; \
    } PY_EXCEPT_HANDLERS(nullptr) \
}

PyMethodDef obj_CompositeScene_methods[] = {
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_camera),METH_O,NULL},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_get_camera),METH_NOARGS,NULL},
    {"set_fov",CS_SET_ATTR(fov),METH_O,NULL},
    {"set_max_reflect_depth",CS_SET_ATTR(max_reflect_depth),METH_O,NULL},
    {"set_shadows",CS_SET_ATTR(shadows),METH_O,NULL},
    {"set_camera_light",CS_SET_ATTR(camera_light),METH_O,NULL},
    {"set_ambient_color",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_ambient),METH_O,NULL},
    {"set_background",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_background),METH_VARARGS|METH_KEYWORDS,NULL},
    {"add_light",reinterpret_cast<PyCFunction>(&obj_CompositeScene_add_light),METH_O,NULL},
    {NULL}
};

PyObject *obj_CompositeScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(ptr) {
        try {
            try {
                const char *names[] = {"boundary","data"};
                get_arg ga(args,kwds,names,"CompositeScene.__new__");
                
                auto &boundary = get_base<n_aabb>(ga(true));
                auto d_node = checked_py_cast<obj_KDNode>(ga(true));
                
                ga.finished();

                if(d_node->parent) THROW_PYERR_STRING(ValueError,"\"data\" must not be already attached to another node");
                
                if(boundary.dimension() != d_node->dimension())
                    THROW_PYERR_STRING(TypeError,"\"boundary\" and \"data\" must have the same dimesion");
                
                auto &base = reinterpret_cast<obj_CompositeScene*>(ptr)->alloc_base();
                
                new(&base) composite_scene<module_store>(boundary,d_node->_data);
                reinterpret_cast<obj_CompositeScene*>(ptr)->_get_base = &obj_CompositeScene::get_base; 
                d_node->parent = py::borrowed_ref(ptr);
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(nullptr)
    }
    return ptr;
}

PyObject *new_obj_node(PyObject *parent,kd_node<module_store> *node,int dimension) {
    if(!node) Py_RETURN_NONE;
    
    if(node->type == LEAF) return reinterpret_cast<PyObject*>(new obj_KDLeaf(py::borrowed_ref(parent),static_cast<kd_leaf<module_store>*>(node)));
    
    assert(node->type == BRANCH);
    return reinterpret_cast<PyObject*>(new obj_KDBranch(py::borrowed_ref(parent),static_cast<kd_branch<module_store>*>(node),dimension));
}

PyGetSetDef obj_CompositeScene_getset[] = {
    {const_cast<char*>("locked"),OBJ_GETTER(obj_CompositeScene,self->get_base().locked),NULL,NULL,NULL},
    {const_cast<char*>("fov"),OBJ_GETTER(obj_CompositeScene,self->get_base().fov),NULL,NULL,NULL},
    {const_cast<char*>("max_reflect_depth"),OBJ_GETTER(obj_CompositeScene,self->get_base().max_reflect_depth),NULL,NULL,NULL},
    {const_cast<char*>("shadows"),OBJ_GETTER(obj_CompositeScene,self->get_base().shadows),NULL,NULL,NULL},
    {const_cast<char*>("camera_light"),OBJ_GETTER(obj_CompositeScene,self->get_base().camera_light),NULL,NULL,NULL},
    {const_cast<char*>("ambient_color"),OBJ_GETTER(obj_CompositeScene,self->get_base().ambient),NULL,NULL,NULL},
    {const_cast<char*>("bg1"),OBJ_GETTER(obj_CompositeScene,self->get_base().bg1),NULL,NULL,NULL},
    {const_cast<char*>("bg2"),OBJ_GETTER(obj_CompositeScene,self->get_base().bg2),NULL,NULL,NULL},
    {const_cast<char*>("bg3"),OBJ_GETTER(obj_CompositeScene,self->get_base().bg3),NULL,NULL,NULL},
    {const_cast<char*>("bg_gradient_axis"),OBJ_GETTER(obj_CompositeScene,self->get_base().bg_gradient_axis),NULL,NULL,NULL},
    {const_cast<char*>("boundary"),OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(reinterpret_cast<PyObject*>(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary)))),NULL,NULL,NULL},
    {const_cast<char*>("root"),OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(new_obj_node(obj_self,self->get_base().root.get(),self->get_base().dimension()))),NULL,NULL,NULL},
    {const_cast<char*>("point_lights"),OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(reinterpret_cast<PyObject*>(new cs_light_list<point_light_list_base>(self)))),NULL,NULL,NULL},
    {const_cast<char*>("global_lights"),OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(reinterpret_cast<PyObject*>(new cs_light_list<global_light_list_base>(self)))),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject composite_scene_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".CompositeScene",
    sizeof(obj_CompositeScene),
    tp_dealloc = destructor_dealloc<obj_CompositeScene>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &traverse_idict<obj_CompositeScene>,
    tp_clear = &clear_idict<obj_CompositeScene>,
    tp_weaklistoffset = offsetof(obj_CompositeScene,weaklist),
    tp_methods = obj_CompositeScene_methods,
    tp_getset = obj_CompositeScene_getset,
    tp_dictoffset = offsetof(obj_CompositeScene,idict),
    tp_new = &obj_CompositeScene_new);


void check_index(const n_camera &c,Py_ssize_t index) {
    if(index < 0 || index >= c.dimension()) THROW_PYERR_STRING(IndexError,"index out of range");
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
        if(UNLIKELY(!value)) {
            PyErr_SetString(PyExc_TypeError,"items of CameraAxes cannot be deleted");
            return -1;
        }
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

PyTypeObject obj_CameraAxes::_pytype = make_type_object(
    FULL_MODULE_STR ".CameraAxes",
    sizeof(obj_BoxScene),
    tp_dealloc = destructor_dealloc<obj_CameraAxes>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_as_sequence = &obj_CameraAxes_sequence_methods,
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the CameraAxes type cannot be instantiated directly");
        return nullptr;
    });


void check_origin_dir_compat(const n_vector &o,const n_vector &d) {
    if(!compatible(o,d))
        THROW_PYERR_STRING(TypeError,"\"origin\" and \"direction\" must have the same dimension");
}

PyObject *obj_Primitive_intersects(primitive<module_store> *self,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"origin","direction"};
        get_arg ga(args,kwds,names,"Primitive.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<module_store> target(origin,direction);
        ray<module_store> normal(origin.dimension());
        
        real dist = self->intersects(target,normal);
        if(!dist) Py_RETURN_NONE;
        
        return py::make_tuple(dist,normal.origin,normal.direction).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Primitive_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&obj_Primitive_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyTypeObject obj_Primitive::_pytype = make_type_object(
    FULL_MODULE_STR ".Primitive",
    sizeof(obj_Primitive),
    tp_methods = obj_Primitive_methods,
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

PyObject *obj_Solid_reduce(obj_Solid *self,PyObject*) {
    try {
        return (*package_common_data.solid_reduce)(
            self->dimension(),
            self->type,
            self->orientation.data(),
            self->position.data(),self->m.get());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Solid_methods[] = {
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Solid_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

PyObject *obj_Solid_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"type","position","orientation","material"};
        get_arg ga(args,kwds,names,"Solid.__new__");
        auto type = from_pyobject<solid_type>(ga(true));
        auto position = from_pyobject<n_vector>(ga(true));
        auto &orientation = get_base<n_matrix>(ga(true));
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();
        
        if(!compatible(orientation,position))
            THROW_PYERR_STRING(TypeError,"the position and orientation must have the same dimension");
        
        return reinterpret_cast<PyObject*>(new obj_Solid(type,orientation,position,m));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Solid_getset[] = {
    {const_cast<char*>("type"),OBJ_GETTER(obj_Solid,int(self->type)),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(obj_Solid,self->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(obj_Solid,self->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(obj_Solid,self->position),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_Solid,self->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_Solid_members[] = {
    {const_cast<char*>("material"),T_OBJECT_EX,offsetof(obj_Triangle,m),READONLY,NULL},
    {NULL}
};

PyTypeObject solid_obj_common::_pytype = make_type_object(
    FULL_MODULE_STR ".Solid",
    sizeof(obj_Solid),
    tp_dealloc = destructor_dealloc<obj_Solid>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_methods = obj_Solid_methods,
    tp_members = obj_Solid_members,
    tp_getset = obj_Solid_getset,
    tp_base = obj_Primitive::pytype(),
    tp_new = &obj_Solid_new,
    tp_free = &obj_Solid::operator delete);


std::vector<n_vector,simd::allocator<n_vector> > points_for_triangle(PyObject *obj) {
    auto points = collect<n_vector>(obj);
    if(points.empty()) THROW_PYERR_STRING(TypeError,"a sequence of points (vectors) is required");
    
    int dim = points[0].dimension();
    for(size_t i=1; i<points.size(); ++i) {
        if(points[i].dimension() != dim) THROW_PYERR_STRING(TypeError,"all points must have the same dimension");
    }
    
    if(size_t(dim) != points.size()) THROW_PYERR_STRING(ValueError,"the number of points must equal their dimension");

    return points;
}

PyObject *obj_Triangle_from_points(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"points","material"};
        get_arg ga(args,kwds,names,"Triangle.from_points");
        auto obj_points = ga(true);
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();
        
        return reinterpret_cast<PyObject*>(obj_Triangle::from_points(points_for_triangle(obj_points).data(),m));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Triangle_reduce(obj_Triangle *self,PyObject*) {
    try {
        struct item_size {
            static constexpr int get(int d) { return d+1; }
        };
        
        module_store::type<item_size,const real*> values(self->dimension());
        values.items[0] = self->p1.data();
        values.items[1] = self->face_normal.data();
        for(int i=0; i<self->dimension()-1; ++i) values.items[i+2] = self->items()[i].data();
        
        return (*package_common_data.triangle_reduce)(self->dimension(),values.items,self->m.get());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Triangle_methods[] = {
    {"from_points",reinterpret_cast<PyCFunction>(&obj_Triangle_from_points),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Triangle_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

PyObject *obj_Triangle_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    const char dim_err[] = "all supplied vectors must have the same dimension";

    try {
        const char *names[] = {"p1","face_normal","edge_normals","material"};
        get_arg ga(args,kwds,names,"Triangle.__new__");
        auto p1 = from_pyobject<n_vector>(ga(true));
        auto face_normal = from_pyobject<n_vector>(ga(true));
        auto normals = ga(true);
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();
        
        if(!compatible(p1,face_normal)) {
            PyErr_SetString(PyExc_TypeError,dim_err);
            return nullptr;
        }
        
        sized_iter norm_itr(py::borrowed_ref(normals),p1.dimension()-1);
        
        auto ptr = obj_Triangle::create(p1,face_normal,[&](int i) -> n_vector {
            auto en = from_pyobject<n_vector>(norm_itr.next());
            if(!compatible(p1,en)) THROW_PYERR_STRING(TypeError,dim_err);
            return en;
        },m);
        try {
            norm_itr.finished();
        } catch(...) {
            delete ptr;
            throw;
        }
        
        return reinterpret_cast<PyObject*>(ptr);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Triangle_getedges(obj_Triangle *self,void *) {
    try {
        return reinterpret_cast<PyObject*>(new obj_FrozenVectorView(
            reinterpret_cast<PyObject*>(self),
            self->items().size(),
            self->items()));
    } PY_EXCEPT_HANDLERS(nullptr)
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
    {const_cast<char*>("material"),T_OBJECT_EX,offsetof(obj_Triangle,m),READONLY,NULL},
    {NULL}
};

PyTypeObject triangle_obj_common::_pytype = make_type_object(
    FULL_MODULE_STR ".Triangle",
    obj_Triangle::base_size,
    tp_itemsize = obj_Triangle::item_size,
    tp_dealloc = destructor_dealloc<obj_Triangle>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_methods = obj_Triangle_methods,
    tp_members = obj_Triangle_members,
    tp_getset = obj_Triangle_getset,
    tp_base = obj_Primitive::pytype(),
    tp_new = &obj_Triangle_new,
    tp_free = &obj_Triangle::operator delete);


size_t fill_ray_intersections(const ray_intersections<module_store> &hits,PyObject *list) {
    auto data = hits.data();
    size_t i = 0;
    for(; i<hits.size(); ++i) PyList_SET_ITEM(
        list,
        i,
        py::make_tuple(data[i].dist,data[i].normal.origin,data[i].normal.direction,reinterpret_cast<PyObject*>(data[i].p)).new_ref());
    return i;
}

PyObject *kdnode_intersects(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);
        
        const char *names[] = {"origin","direction","t_near","t_far","source"};
        get_arg ga(args,kwds,names,"KDNode.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        
        real t_near = std::numeric_limits<real>::lowest();
        real t_far = std::numeric_limits<real>::max();
        
        auto tmp = ga(false);
        if(tmp) t_near = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) t_far = from_pyobject<real>(tmp);
        
        tmp = ga(false);
        auto source = tmp && tmp != Py_None ? checked_py_cast<primitive<module_store> >(tmp) : nullptr;
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<module_store> normal(origin.dimension());
        ray_intersections<module_store> hits;
        real dist;
        {
            py::allow_threads _;
            ray<module_store> target(origin,direction);
            
            dist = self->_data->intersects(target,normal,source,hits,t_near,t_far);
        }
        
        size_t r_size = hits.size();
        if(dist) ++r_size;
        PyObject *r = PyList_New(r_size);
        if(UNLIKELY(!r)) return nullptr;

        try {
            auto i = fill_ray_intersections(hits,r);
            if(dist) PyList_SET_ITEM(
                r,
                i,
                py::make_tuple(dist,normal.origin,normal.direction,reinterpret_cast<PyObject*>(source)).new_ref());
        } catch(...) {
            Py_DECREF(r);
            throw;
        }
        
        return r;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *kdnode_occludes(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);
        
        const char *names[] = {"origin","direction","distance","t_near","t_far","source"};
        get_arg ga(args,kwds,names,"KDNode.occludes");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        
        real distance = std::numeric_limits<real>::max();
        real t_near = std::numeric_limits<real>::lowest();
        real t_far = std::numeric_limits<real>::max();
        
        auto tmp = ga(false);
        if(tmp) distance = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) t_near = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) t_far = from_pyobject<real>(tmp);
        
        tmp = ga(false);
        auto source = tmp && tmp != Py_None ? checked_py_cast<primitive<module_store> >(tmp) : nullptr;
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray_intersections<module_store> hits;
        bool occ;
        {
            py::allow_threads _;
            ray<module_store> target(origin,direction);
            
            occ = self->_data->occludes(target,distance,source,hits,t_near,t_far);
        }
        
        py::object b;
        if(!occ) {
            b = py::check_new_ref(PyList_New(hits.size()));
            fill_ray_intersections(hits,b.ref());
        }
        
        return py::make_tuple(occ,b).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_KDNode_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&kdnode_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {"occludes",reinterpret_cast<PyCFunction>(&kdnode_occludes),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyTypeObject obj_KDNode::_pytype = make_type_object(
    FULL_MODULE_STR ".KDNode",
    sizeof(obj_KDNode),
    tp_methods = obj_KDNode_methods,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the KDNode type cannot be instantiated directly");
        return nullptr;
    });


Py_ssize_t obj_KDLeaf___sequence_len__(obj_KDLeaf *self) {
    return self->data()->size;
}

PyObject *obj_KDLeaf___sequence_getitem__(obj_KDLeaf *self,Py_ssize_t index) {
    if(UNLIKELY(index < 0 || index >= static_cast<Py_ssize_t>(self->data()->size))) {
        PyErr_SetString(PyExc_IndexError,"index out of range");
        return nullptr;
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

PyObject *obj_KDLeaf_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_KDLeaf*>(type->tp_alloc(type,0));
    if(!ptr) return nullptr;
    

    try {
        try {
            const char *names[] = {"primitives"};
            get_arg ga(args,kwds,names,"KDLeaf.__new__");
            py::tuple primitives(py::object(py::borrowed_ref(ga(true))));
            ga.finished();
            
            Py_ssize_t size = primitives.size();
            
            if(!size) THROW_PYERR_STRING(ValueError,"KDLeaf requires at least one item");
            
            ptr->data() = kd_leaf<module_store>::create(size,[=](size_t i) -> primitive<module_store>* {
                auto p = primitives[i];
                checked_py_cast<primitive<module_store> >(p.ref());
                return reinterpret_cast<primitive<module_store>*>(p.new_ref());
            });
            
            
            int d = ptr->data()->items()[0]->dimension();
            for(Py_ssize_t i=1; i<size; ++i) {
                if(ptr->data()->items()[i]->dimension() != d) {
                    THROW_PYERR_STRING(TypeError,"every member of KDLeaf must have the same dimension");
                }
            }
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)

    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_KDLeaf_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_KDLeaf,self->data()->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDLeaf::_pytype = make_type_object(
    FULL_MODULE_STR ".KDLeaf",
    sizeof(obj_KDLeaf),
    tp_dealloc = destructor_dealloc<obj_KDLeaf>::value,
    tp_as_sequence = &obj_KDLeaf_sequence_methods,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &kd_tree_item_traverse<obj_KDLeaf>,
    tp_clear = &kd_tree_item_clear<obj_KDLeaf>,
    tp_getset = obj_KDLeaf_getset,
    tp_base = obj_KDNode::pytype(),
    tp_new = &obj_KDLeaf_new);


obj_KDNode* acceptable_node(PyObject *obj) {
    if(!obj || obj == Py_None) return nullptr;
    
    if(Py_TYPE(obj) != obj_KDBranch::pytype() && Py_TYPE(obj) != obj_KDLeaf::pytype())
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
        } PY_EXCEPT_HANDLERS(nullptr)
    }
    return reinterpret_cast<PyObject*>(ptr);
}

PyObject *obj_KDBranch_get_child(obj_KDBranch *self,void *index) {
    assert(&self->data()->right == (&self->data()->left + 1));
    
    try {  
        return new_obj_node(reinterpret_cast<PyObject*>(self),(&self->data()->left)[reinterpret_cast<size_t>(index)].get(),self->dimension);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_KDBranch_getset[] = {
    {const_cast<char*>("axis"),OBJ_GETTER(obj_KDBranch,self->data()->axis),NULL,NULL,NULL},
    {const_cast<char*>("split"),OBJ_GETTER(obj_KDBranch,self->data()->split),NULL,NULL,NULL},
    {const_cast<char*>("left"),reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(0)},
    {const_cast<char*>("right"),reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(1)},
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_KDBranch,self->dimension),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDBranch::_pytype = make_type_object(
    FULL_MODULE_STR ".KDBranch",
    sizeof(obj_KDBranch),
    tp_dealloc = destructor_dealloc<obj_KDBranch>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &kd_tree_item_traverse<obj_KDBranch>,
    tp_clear = &kd_tree_item_clear<obj_KDBranch>,
    tp_getset = obj_KDBranch_getset,
    tp_base = obj_KDNode::pytype(),
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
    } PY_EXCEPT_HANDLERS(nullptr)
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
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_richcompare(wrapped_type<n_vector> *self,PyObject *arg,int op) {
    if(op == Py_EQ || op == Py_NE) {
        auto &base = self->get_base();
        auto b = get_base_if_is_type<n_vector>(arg);
        
        if(b) return to_pyobject((compatible(base,*b) && base == *b) == (op == Py_EQ));
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___neg__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(-self->get_base());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector___abs__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(self->get_base().absolute());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector___add__(PyObject *a,PyObject *b) {
    n_vector *va, *vb;
    
    try {
        if((va = get_base_if_is_type<n_vector>(a)) && (vb = get_base_if_is_type<n_vector>(b))) {
            if(UNLIKELY(!compatible(*va,*vb))) {
                PyErr_SetString(PyExc_TypeError,"cannot add vectors of different dimension");
                return nullptr;
            }
            return to_pyobject(*va + *vb);
        }
    } PY_EXCEPT_HANDLERS(nullptr)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___sub__(PyObject *a,PyObject *b) {
    n_vector *va, *vb;
    
    try {
        if((va = get_base_if_is_type<n_vector>(a)) && (vb = get_base_if_is_type<n_vector>(b))) {
            if(UNLIKELY(!compatible(*va,*vb))) {
                PyErr_SetString(PyExc_TypeError,"cannot subtract vectors of different dimension");
                return nullptr;
            }
            return to_pyobject(*va - *vb);
        }
    } PY_EXCEPT_HANDLERS(nullptr)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___mul__(PyObject *a,PyObject *b) {
    try {
        auto va = get_base_if_is_type<n_vector>(a);
        
        if(va) {
            if(PyNumber_Check(b)) return to_pyobject(*va * from_pyobject<real>(b));
        } else if(PyNumber_Check(a)) {
            assert(PyObject_TypeCheck(b,wrapped_type<n_vector>::pytype()));
            return to_pyobject(from_pyobject<real>(a) * reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base());
        }
    } PY_EXCEPT_HANDLERS(nullptr)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___div__(PyObject *a,PyObject *b) {
    try {
        auto va = get_base_if_is_type<n_vector>(a);
        
        if(va && PyNumber_Check(b)) return to_pyobject(*va / from_pyobject<real>(b));
    } PY_EXCEPT_HANDLERS(nullptr)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyNumberMethods obj_Vector_number_methods = {
    &obj_Vector___add__,
    &obj_Vector___sub__,
    &obj_Vector___mul__,
#if PY_MAJOR_VERSION < 3
    &obj_Vector___div__,
#endif
    NULL,
    NULL,
    NULL,
    reinterpret_cast<unaryfunc>(&obj_Vector___neg__),
    NULL,
    reinterpret_cast<unaryfunc>(&obj_Vector___abs__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
#if PY_MAJOR_VERSION < 3
    NULL,
#endif
    NULL,
    NULL,
    NULL,
#if PY_MAJOR_VERSION < 3
    NULL,
    NULL,
#endif
    NULL,
    NULL,
    NULL,
#if PY_MAJOR_VERSION < 3
    NULL,
#endif
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    &obj_Vector___div__,
    NULL,
    NULL,
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
    } PY_EXCEPT_HANDLERS(nullptr)
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
        auto vals = get_arg::get_args("Vector.axis",args,kwds,
            param<int>("dimension"),
            param<int>("axis"),
            param<real>("length",1));

        check_dimension(std::get<0>(vals));
        if(UNLIKELY(std::get<1>(vals) < 0 || std::get<1>(vals) >= std::get<0>(vals))) {
            PyErr_SetString(PyExc_ValueError,"axis must be between 0 and dimension-1");
            return nullptr;
        }
        return to_pyobject(apply(&n_vector::axis,vals));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_absolute(wrapped_type<n_vector> *self,PyObject *) {
    return to_pyobject(self->get_base().absolute());
}

PyObject *obj_Vector_unit(wrapped_type<n_vector> *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().unit());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_apply(wrapped_type<n_vector> *_self,PyObject *_func) {
    try {
        auto &self = _self->get_base();
        
        n_vector r(self.dimension());
        py::object func = py::borrowed_ref(_func);
        
        for(int i=0; i<self.dimension(); ++i)
            r[i] = from_pyobject<real>(func(self[i]));
        
        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_set_c(wrapped_type<n_vector> *self,PyObject *args,PyObject *kwds) {
    try {
        auto &v = self->get_base();

        auto vals = get_arg::get_args("Vector.set_c",args,kwds,
            param<Py_ssize_t>("index"),
            param<real>("value"));
        
        v_index_check(v,std::get<0>(vals));
        
        n_vector r = v;
        r[std::get<0>(vals)] = std::get<1>(vals);
        
        return to_pyobject(std::move(r));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_reduce(wrapped_type<n_vector> *self,PyObject*) {
    try {
        auto &v = self->get_base();
        return (*package_common_data.vector_reduce)(v.dimension(),v.data());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Vector_methods[] = {
    {"square",reinterpret_cast<PyCFunction>(&obj_Vector_square),METH_NOARGS,NULL},
    {"axis",reinterpret_cast<PyCFunction>(&obj_Vector_axis),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"absolute",reinterpret_cast<PyCFunction>(&obj_Vector_absolute),METH_NOARGS,NULL},
    {"unit",reinterpret_cast<PyCFunction>(&obj_Vector_unit),METH_NOARGS,NULL},
    {"apply",reinterpret_cast<PyCFunction>(&obj_Vector_apply),METH_O,NULL},
    {"set_c",reinterpret_cast<PyCFunction>(&obj_Vector_set_c),METH_VARARGS|METH_KEYWORDS,NULL},
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Vector_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

PyObject *obj_Vector_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"Vector.__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::nullable<py::object> values(py::borrowed_ref(ga(false)));
        ga.finished();
        
        check_dimension(dimension);

        auto ptr = py::check_obj(type->tp_alloc(type,0));
        auto &base = reinterpret_cast<wrapped_type<n_vector>*>(ptr)->alloc_base();
        new(&base) n_vector(dimension);
        
        if(values) {
            sized_iter itr(*values,dimension);
            for(int i=0; i<dimension; ++i) {
                base[i] = from_pyobject<real>(itr.next());
            }
            itr.finished();
        } else {
            for(int i=0; i<dimension; ++i) base[i] = 0;
        }
        
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Vector_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_vector>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject vector_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".Vector",
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


PyGetSetDef obj_Camera_getset[] = {
    {const_cast<char*>("origin"),OBJ_GETTER(wrapped_type<n_camera>,self->get_base().origin),OBJ_SETTER(wrapped_type<n_camera>,self->get_base().origin),NULL,NULL},
    {const_cast<char*>("axes"),OBJ_GETTER(wrapped_type<n_camera>,reinterpret_cast<PyObject*>(new obj_CameraAxes(self))),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_camera>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyObject *obj_Camera_normalize(wrapped_type<n_camera> *self,PyObject *) {
    try {
        self->get_base().normalize();
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Camera_translate(wrapped_type<n_camera> *self,PyObject *arg) {
    try {
        self->get_base().translate(from_pyobject<n_vector>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Camera_transform(wrapped_type<n_camera> *self,PyObject *arg) {
    try {
        self->get_base().transform(get_base<n_matrix>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Camera_methods[] = {
    {"normalize",reinterpret_cast<PyCFunction>(&obj_Camera_normalize),METH_NOARGS,NULL},
    {"translate",reinterpret_cast<PyCFunction>(&obj_Camera_translate),METH_O,NULL},
    {"transform",reinterpret_cast<PyCFunction>(&obj_Camera_transform),METH_O,NULL},
    {NULL}
};

PyObject *obj_Camera_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        int dimension = std::get<0>(get_arg::get_args("Camera.__new__",args,kwds,param<int>("dimension")));
        
        check_dimension(dimension);
        
        PyObject *ptr = py::check_obj(type->tp_alloc(type,0));
        
        new(&reinterpret_cast<wrapped_type<n_camera>*>(ptr)->alloc_base()) n_camera(dimension);
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

int obj_Camera_init(wrapped_type<n_camera> *self,PyObject *args,PyObject *kwds) {
    auto &base = self->get_base();

    for(int i=0; i<base.dimension()*base.dimension(); ++i) base.t_orientation.data()[i] = 0;
    for(int i=0; i<base.dimension(); ++i) base.t_orientation[i][i] = 1;

    return 0;
}

PyTypeObject camera_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".Camera",
    sizeof(wrapped_type<n_camera>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_camera> >::value,
    tp_methods = obj_Camera_methods,
    tp_getset = obj_Camera_getset,
    tp_init = &obj_Camera_init,
    tp_new = &obj_Camera_new);


PyObject *obj_Matrix___mul__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,wrapped_type<n_matrix>::pytype())) {
            auto &base = reinterpret_cast<wrapped_type<n_matrix>*>(a)->get_base();
            if(PyObject_TypeCheck(b,wrapped_type<n_matrix>::pytype())) {
                auto &mb = reinterpret_cast<wrapped_type<n_matrix>*>(b)->get_base();
                if(UNLIKELY(!compatible(base,mb))) {
                    PyErr_SetString(PyExc_TypeError,"cannot multiply matrices of different dimension");
                    return nullptr;
                }
                return to_pyobject(base * mb);
            }
            if(PyObject_TypeCheck(b,wrapped_type<n_vector>::pytype())) {
                auto &vb = reinterpret_cast<wrapped_type<n_vector>*>(b)->get_base();
                if(UNLIKELY(!compatible(base,vb))) {
                    PyErr_SetString(PyExc_TypeError,"cannot multiply a matrix and a vector of different dimension");
                    return nullptr;
                }
                return to_pyobject(base * vb);
            }
        }
    } PY_EXCEPT_HANDLERS(nullptr)
    
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

        if(UNLIKELY(index < 0 || index >= base.dimension())) {
            PyErr_SetString(PyExc_IndexError,"matrix row index out of range");
            return nullptr;
        }
        return reinterpret_cast<PyObject*>(new obj_MatrixProxy(reinterpret_cast<PyObject*>(self),base.dimension(),base[index]));
    } PY_EXCEPT_HANDLERS(nullptr)
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

PyObject *obj_Matrix_reflection(PyObject*,PyObject *arg) {
    try {
        return to_pyobject(n_matrix::reflection(from_pyobject<n_vector>(arg)));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_scale(PyObject*,PyObject *args) {
    try {
        if(PyTuple_GET_SIZE(args) == 1) {
            if(PyObject_TypeCheck(PyTuple_GET_ITEM(args,0),wrapped_type<n_vector>::pytype())) {
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
        return nullptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_rotation(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto vals = get_arg::get_args("Matrix.rotation",args,kwds,
            param<n_vector>("a"),
            param<n_vector>("b"),
            param<real>("b"));
        
        if(UNLIKELY(!compatible(std::get<0>(vals),std::get<1>(vals)))) {
            PyErr_SetString(PyExc_TypeError,"cannot produce rotation matrix using vectors of different dimension");
            return nullptr;
        }
        
        return to_pyobject(apply(&n_matrix::rotation,vals));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_identity(PyObject*,PyObject *arg) {
    try {
        int dimension = from_pyobject<int>(arg);
        check_dimension(dimension);
        return to_pyobject(n_matrix::identity(dimension));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_determinant(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().determinant());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_inverse(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().inverse());
    } catch(std::domain_error &e) {
        PyErr_SetString(PyExc_ValueError,e.what());
        return nullptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_transpose(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().transpose());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_reduce(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        auto &m = self->get_base();
        return (*package_common_data.matrix_reduce)(m.dimension(),m.data());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Matrix_methods[] = {
    {"reflection",reinterpret_cast<PyCFunction>(&obj_Matrix_reflection),METH_O|METH_STATIC,NULL},
    {"scale",reinterpret_cast<PyCFunction>(&obj_Matrix_scale),METH_VARARGS|METH_STATIC,NULL},
    {"rotation",reinterpret_cast<PyCFunction>(&obj_Matrix_rotation),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"identity",reinterpret_cast<PyCFunction>(&obj_Matrix_identity),METH_O|METH_STATIC,NULL},
    {"determinant",reinterpret_cast<PyCFunction>(&obj_Matrix_determinant),METH_NOARGS,NULL},
    {"inverse",reinterpret_cast<PyCFunction>(&obj_Matrix_inverse),METH_NOARGS,NULL},
    {"transpose",reinterpret_cast<PyCFunction>(&obj_Matrix_transpose),METH_NOARGS,NULL},
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Matrix_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
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
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Matrix_values(wrapped_type<n_matrix> *self,void*) {
    try {
        auto &base = self->get_base();
        return reinterpret_cast<PyObject*>(
            new obj_MatrixProxy(reinterpret_cast<PyObject*>(self),base.dimension() * base.dimension(),base.data()));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Matrix_getset[] = {
    {const_cast<char*>("values"),reinterpret_cast<getter>(&obj_Matrix_values),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_matrix>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject matrix_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".Matrix",
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
        std::tie(axis,split) = get_arg::get_args(right ? "AABB.right" : "AABB.left",args,kwds,
            param<int>("axis"),
            param<real>("split"));
        
        auto &base = self->get_base();
        
        if(UNLIKELY(axis < 0 || axis >= base.start.dimension())) {
            PyErr_SetString(PyExc_ValueError,"invalid axis");
            return nullptr;
        }
        if(UNLIKELY(split <= base.start[axis] || split >= base.end[axis])) {
            PyErr_SetString(PyExc_ValueError,"\"split\" must be inside the box within the given axis");
            return nullptr;
        }
    
        r = new wrapped_type<n_aabb>(base);
    } PY_EXCEPT_HANDLERS(nullptr)
    
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
    if(Py_TYPE(o) == triangle_obj_common::pytype() || Py_TYPE(o) == solid_obj_common::pytype()) {
        PyErr_SetString(PyExc_TypeError,
            "Instances of Triangle and Solid cannot be used directly. Use TrianglePrototype and SolidPrototype instead.");
    } else {
        PyErr_SetString(PyExc_TypeError,
            "object must be an instance of " MODULE_STR ".PrimitivePrototype");
    }
    return nullptr;
}

template<typename T> PyObject *try_intersects(const n_aabb &base,PyObject *obj) {
    if(PyObject_TypeCheck(obj,wrapped_type<T>::pytype())) {
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
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_AABB_intersects_flat(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"primitive","skip"};
        get_arg ga(args,kwds,names,"AABB.intersects_flat");
        auto &p = get_base<n_triangle_prototype>(ga(true));
        int skip = from_pyobject<int>(ga(true));
        ga.finished();
        
        return to_pyobject(self->get_base().intersects_flat(p,skip));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_AABB_methods[] = {
    {"left",reinterpret_cast<PyCFunction>(&obj_AABB_left),METH_VARARGS|METH_KEYWORDS,NULL},
    {"right",reinterpret_cast<PyCFunction>(&obj_AABB_right),METH_VARARGS|METH_KEYWORDS,NULL},
    {"intersects",reinterpret_cast<PyCFunction>(&obj_AABB_intersects),METH_O,NULL},
    {"intersects_flat",reinterpret_cast<PyCFunction>(&obj_AABB_intersects_flat),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyObject *obj_AABB_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;
    
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
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
    
    return ptr;
}

PyGetSetDef obj_AABB_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().dimension()),NULL,NULL,NULL},
    {const_cast<char*>("start"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().start),NULL,NULL,NULL},
    {const_cast<char*>("end"),OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().end),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject aabb_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".AABB",
    sizeof(wrapped_type<n_aabb>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_aabb> >::value,
    tp_methods = obj_AABB_methods,
    tp_getset = obj_AABB_getset,
    tp_new = &obj_AABB_new);


PyTypeObject obj_PrimitivePrototype::_pytype = make_type_object(
    FULL_MODULE_STR ".PrimitivePrototype",
    sizeof(obj_PrimitivePrototype),
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the PrimitivePrototype type cannot be instantiated directly");
        return nullptr;
    });


PyObject *obj_TrianglePrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"points","material"};
        get_arg ga(args,kwds,names,"TrianglePrototype.__new__");
        auto points_obj = ga(true);
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();
        
        auto points = points_for_triangle(points_obj);
        int dim = points[0].dimension();
        
        auto ptr = py::check_obj(type->tp_alloc(type,obj_TrianglePrototype::item_size ? dim : 0));
        
        try {
            auto &base = reinterpret_cast<obj_TrianglePrototype*>(ptr)->alloc_base(dim);
            
            new(&base.boundary) n_aabb(n_vector(dim,std::numeric_limits<real>::max()),n_vector(dim,std::numeric_limits<real>::lowest()));
            
            for(int i=0; i<dim; ++i) {
                for(int j=0; j<dim; ++j) {
                    if(points[i][j] > base.boundary.end[j]) base.boundary.end[j] = points[i][j];
                    if(points[i][j] < base.boundary.start[j]) base.boundary.start[j] = points[i][j];
                }
            }
            
            new(&base.p) py::pyptr<obj_Primitive>(py::new_ref(reinterpret_cast<PyObject*>(obj_Triangle::from_points(points.data(),m))));
            new(&base.first_edge_normal) n_vector(dim,0);
            new(&base.items()[0]) n_triangle_point(points[0],base.first_edge_normal);
            
            for(int i=1; i<dim; ++i) {
                new(&base.items()[i]) n_triangle_point(points[i],base.pt()->items()[i-1]);
                base.first_edge_normal -= base.pt()->items()[i-1];
            }
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_TrianglePrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().dimension()),NULL,NULL,NULL},
    {const_cast<char*>("face_normal"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().pt()->face_normal),NULL,NULL,NULL},
    {const_cast<char*>("point_data"),OBJ_GETTER(
        obj_TrianglePrototype,
        reinterpret_cast<PyObject*>(new obj_TrianglePointData(obj_self,self->get_base().dimension(),self->get_base().items()))),NULL,NULL,NULL},
    {const_cast<char*>("boundary"),OBJ_GETTER(
        obj_TrianglePrototype,
        py::new_ref(reinterpret_cast<PyObject*>(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary)))),NULL,NULL,NULL},
    {const_cast<char*>("material"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().pt()->m),NULL,NULL,NULL},
    {const_cast<char*>("primitive"),OBJ_GETTER(obj_TrianglePrototype,self->get_base().p),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject triangle_prototype_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".TrianglePrototype",
    obj_TrianglePrototype::base_size,
    tp_itemsize = obj_TrianglePrototype::item_size,
    tp_dealloc = destructor_dealloc<obj_TrianglePrototype>::value,
    tp_getset = obj_TrianglePrototype_getset,
    tp_base = obj_PrimitivePrototype::pytype(),
    tp_new = &obj_TrianglePrototype_new);


PyGetSetDef obj_TrianglePointDatum_getset[] = {
    {const_cast<char*>("point"),OBJ_GETTER(wrapped_type<n_detached_triangle_point>,self->get_base().point),NULL,NULL,NULL},
    {const_cast<char*>("edge_normal"),OBJ_GETTER(wrapped_type<n_detached_triangle_point>,self->get_base().edge_normal),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject detached_triangle_point_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".TrianglePointDatum",
    sizeof(wrapped_type<n_detached_triangle_point>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_detached_triangle_point> >::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_getset = obj_TrianglePointDatum_getset,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"The TrianglePointDatum type cannot be instantiated directly");
        return nullptr;
    });


PyObject *obj_SolidPrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;
    
    try {
        try {
            const char *names[] = {"type","position","orientation","material"};
            get_arg ga(args,kwds,names,"SolidPrototype.__new__");
            auto type = from_pyobject<solid_type>(ga(true));
            auto position = from_pyobject<n_vector>(ga(true));
            auto &orientation = get_base<n_matrix>(ga(true));
            auto m = checked_py_cast<material>(ga(true));
            ga.finished();
            
            if(!compatible(orientation,position))
                THROW_PYERR_STRING(TypeError,"the orientation and position must have the same dimension");
            
            auto &base = reinterpret_cast<wrapped_type<n_solid_prototype>*>(ptr)->alloc_base();
            
            new(&base.p) py::pyptr<obj_Solid>(new obj_Solid(type,orientation,position,m));

            if(type == CUBE) {
                n_vector extent{position.dimension(),0};
                for(int i=0; i<position.dimension(); ++i) v_expr(extent) += v_expr(base.ps()->cube_component(i)).abs();
                
                new(&base.boundary) n_aabb(position - extent,position + extent);
            } else {
                assert(type == SPHERE);

                new(&base.boundary) n_aabb(position.dimension());

                for(int i=0; i<position.dimension(); ++i) {
                    // equivalent to: (orientation.transpose() * n_vector::axis(position->dimension(),i)).unit()
                    n_vector normal = orientation[i].unit();
                    
                    real max = dot(n_vector::axis(position.dimension(),i) - position,normal);
                    real min = dot(n_vector::axis(position.dimension(),i,-1) - position,normal);
                    if(min > max) std::swap(max,min);

                    base.boundary.end[i] = max;
                    base.boundary.start[i] = min;
                }
            }
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_SolidPrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().dimension()),NULL,NULL,NULL},
    {const_cast<char*>("type"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->type),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->position),NULL,NULL,NULL},
    {const_cast<char*>("material"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->m),NULL,NULL,NULL},
    {const_cast<char*>("boundary"),OBJ_GETTER(
        wrapped_type<n_solid_prototype>,
        py::new_ref(reinterpret_cast<PyObject*>(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary)))),NULL,NULL,NULL},
    {const_cast<char*>("primitive"),OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject solid_prototype_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".SolidPrototype",
    sizeof(wrapped_type<n_solid_prototype>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_solid_prototype> >::value,
    tp_getset = obj_SolidPrototype_getset,
    tp_base = obj_PrimitivePrototype::pytype(),
    tp_new = &obj_SolidPrototype_new);


PyObject *obj_PointLight_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;
    
    try {
        try {
            const char *names[] = {"position","color"};
            get_arg ga(args,kwds,names,"PointLight.__new__");
            auto position = from_pyobject<n_vector>(ga(true));
            auto c = ga(false);
            ga.finished();
            
            auto &base = reinterpret_cast<wrapped_type<n_point_light>*>(ptr)->alloc_base();
            
            new(&base.position) n_vector(position);
            read_color(base.c,c,names[1]);
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_PointLight_getset[] = {
    {const_cast<char*>("position"),OBJ_GETTER(wrapped_type<n_point_light>,self->get_base().position),NULL,NULL,NULL},
    {const_cast<char*>("color"),OBJ_GETTER(wrapped_type<n_point_light>,self->get_base().c),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_point_light>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject point_light_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".PointLight",
    sizeof(wrapped_type<n_point_light>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_point_light> >::value,
//    tp_repr = &obj_PointLight_repr,
    tp_getset = obj_PointLight_getset,
    tp_new = &obj_PointLight_new);


PyObject *obj_GlobalLight_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;
    
    try {
        try {
            auto vals = get_arg::get_args("GlobalLight.__new__",args,kwds,
                param<n_vector>("direction"),
                param("color"));
            
            auto &base = reinterpret_cast<wrapped_type<n_global_light>*>(ptr)->alloc_base();
            
            new(&base.direction) n_vector(std::get<0>(vals));
            read_color(base.c,std::get<1>(vals),"color");
            
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_GlobalLight_getset[] = {
    {const_cast<char*>("direction"),OBJ_GETTER(wrapped_type<n_global_light>,self->get_base().direction),NULL,NULL,NULL},
    {const_cast<char*>("color"),OBJ_GETTER(wrapped_type<n_global_light>,self->get_base().c),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(wrapped_type<n_global_light>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject global_light_obj_base::_pytype = make_type_object(
    FULL_MODULE_STR ".GlobalLight",
    sizeof(wrapped_type<n_global_light>),
    tp_dealloc = destructor_dealloc<wrapped_type<n_global_light> >::value,
//    tp_repr = &obj_GlobalLight_repr,
    tp_getset = obj_GlobalLight_getset,
    tp_new = &obj_GlobalLight_new);


template<typename T> void check_index(const cs_light_list<T> *ll,Py_ssize_t index) {
    if(index < 0 || size_t(index) >= T::value(ll->parent.get()).size()) THROW_PYERR_STRING(IndexError,"index out of range");
}

template<typename T> Py_ssize_t cs_light_list_len(cs_light_list<T> *self) {
    return static_cast<Py_ssize_t>(T::value(self->parent.get()).size());
}

template<typename T> PyObject *cs_light_list_getitem(cs_light_list<T> *self,Py_ssize_t index) {
    try {
        check_index(self,index);
        return to_pyobject(T::value(self->parent.get())[index]);
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> int cs_light_list_setitem(cs_light_list<T> *self,Py_ssize_t index,PyObject *value) {
    try {
        ensure_unlocked(self->parent);
        check_index(self,index);
        auto &vals = T::value(self->parent.get());
        
        if(value) {
            vals[index] = light_compat_check(self->parent->cast_base(),get_base<typename T::item_t>(value));
            return 0;
        }
        
        if(index != Py_ssize_t(vals.size()) - 1) vals[index] = vals.back();
        vals.pop_back();
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
}

// Note: due to a bug in GCC 4.7, lambda functions cannot be used here
template<typename T> PySequenceMethods cs_light_list<T>::sequence_methods = {
    reinterpret_cast<lenfunc>(&cs_light_list_len<T>),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&cs_light_list_getitem<T>),
    NULL,
    reinterpret_cast<ssizeobjargproc>(&cs_light_list_setitem<T>),
    NULL,
    NULL,
    NULL,
    NULL
};

template<typename T> PyObject *cs_light_list_append(cs_light_list<T> *self,PyObject *arg) {
    try {
        ensure_unlocked(self->parent);
        T::value(self->parent.get()).push_back(light_compat_check(self->parent->cast_base(),get_base<typename T::item_t>(arg)));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> PyObject *cs_light_list_extend(cs_light_list<T> *self,PyObject *arg) {
    try {
        ensure_unlocked(self->parent);
        
        auto &vals = T::value(self->parent.get());
        
        Py_ssize_t hint = PyObject_LengthHint(arg,0);
        if(hint < 0) return nullptr;
        if(hint > 0) vals.reserve(vals.size() + hint);
        
        auto &pbase = self->parent->cast_base();
        auto itr = py::iter(arg);
        while(auto v = py::next(itr))
            vals.push_back(light_compat_check(pbase,get_base<typename T::item_t>(v.ref())));
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

// Note: due to a bug in GCC 4.7, lambda functions cannot be used here
template<typename T> PyMethodDef cs_light_list<T>::methods[] = {
    {"append",reinterpret_cast<PyCFunction>(&cs_light_list_append<T>),METH_O,NULL},
    {"extend",reinterpret_cast<PyCFunction>(&cs_light_list_extend<T>),METH_O,NULL},
    {NULL}
};

template<typename T> PyObject *cs_light_list_new(PyTypeObject*,PyObject*,PyObject*) {
    PyErr_Format(PyExc_TypeError,"the %s type cannot be instantiated directly",T::name + sizeof(FULL_MODULE_STR));
    return nullptr;
}

// Note: due to a bug in GCC 4.7, lambda functions cannot be used here
template<typename T> PyTypeObject cs_light_list<T>::_pytype = make_type_object(
    T::name,
    sizeof(cs_light_list<T>),
    tp_dealloc = destructor_dealloc<cs_light_list<T> >::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_methods = cs_light_list<T>::methods,
    tp_as_sequence = &cs_light_list<T>::sequence_methods,
    tp_new = &cs_light_list_new<T>);


PyObject *obj_dot(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto vals = get_arg::get_args("dot",args,kwds,
            param<n_vector>("a"),
            param<n_vector>("b"));
        
        if(UNLIKELY(!compatible(std::get<0>(vals),std::get<1>(vals)))) {
            PyErr_SetString(PyExc_TypeError,"cannot perform dot product on vectors of different dimension");
            return nullptr;
        }
        return to_pyobject(dot(std::get<0>(vals),std::get<1>(vals)));

    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_cross(PyObject*,PyObject *arg) {
    try {
        auto vs = collect<n_vector>(arg);
        
        if(UNLIKELY(!vs.size())) {
            PyErr_SetString(PyExc_TypeError,"argument must be a sequence of vectors");
            return nullptr;
        }

        int dim = vs[0].dimension();
        for(size_t i=1; i<vs.size(); ++i) {
            if(UNLIKELY(vs[i].dimension() != dim)) {
                PyErr_SetString(PyExc_TypeError,"the vectors must all have the same dimension");
                return nullptr;
            }
        }
        if(UNLIKELY(size_t(dim) != vs.size()+1)) {
            PyErr_SetString(PyExc_ValueError,"the number of vectors must be exactly one less than their dimension");
            return nullptr;
        }

        return to_pyobject(cross<module_store>(vs.data()));
    } PY_EXCEPT_HANDLERS(nullptr)
}

std::tuple<n_aabb,kd_node<module_store>*> build_kdtree(const char *func,PyObject *args,PyObject *kwds) {
    auto vals = get_arg::get_args(func,args,kwds,
        param("primitives"),
        param<int>("extra_threads",-1));
    
    auto p_objs = collect<py::object>(std::get<0>(vals));
    if(UNLIKELY(p_objs.empty())) THROW_PYERR_STRING(ValueError,"cannot build tree from empty sequence");
    
    proto_array<module_store> primitives;
    primitives.reserve(p_objs.size());
    
    primitives.push_back(&checked_py_cast<obj_PrimitivePrototype>(p_objs[0].ref())->get_base());
    
    int dimension = primitives[0]->dimension();
    
    for(size_t i=1; i<p_objs.size(); ++i) {
        auto p = &checked_py_cast<obj_PrimitivePrototype>(p_objs[i].ref())->get_base();
        if(UNLIKELY(p->dimension() != dimension)) THROW_PYERR_STRING(TypeError,"the primitive prototypes must all have the same dimension");
        primitives.push_back(p);
    }
    
    {
        py::allow_threads _;
        return build_kdtree<module_store>(primitives,std::get<1>(vals));
    }
}

PyObject *obj_build_kdtree(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto vals = build_kdtree("build_kdtree",args,kwds);
        py::object root{py::new_ref(new_obj_node(nullptr,std::get<1>(vals),std::get<0>(vals).dimension()))};
        return py::make_tuple(std::get<0>(vals).start,std::get<0>(vals).end,root).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_build_composite_scene(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto vals = build_kdtree("build_composite_scene",args,kwds);
        return reinterpret_cast<PyObject*>(new obj_CompositeScene(std::get<0>(vals),std::get<1>(vals)));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef func_table[] = {
    {"dot",reinterpret_cast<PyCFunction>(&obj_dot),METH_VARARGS|METH_KEYWORDS,NULL},
    {"cross",&obj_cross,METH_O,NULL},
    {"build_kdtree",reinterpret_cast<PyCFunction>(&obj_build_kdtree),METH_VARARGS|METH_KEYWORDS,NULL},
    {"build_composite_scene",reinterpret_cast<PyCFunction>(&obj_build_composite_scene),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};


template<typename T> wrapped_array get_wrapped_array(int dimension) {
    auto obj = new wrapped_type<T>(dimension);
    return {py::new_ref(reinterpret_cast<PyObject*>(obj)),obj->cast_base().data()};
}

/* TODO: do something so that changing "real" to something other than float
   won't cause compile errors */
tracerx_constructors module_constructors = {
    &get_wrapped_array<n_vector>,
    &get_wrapped_array<n_matrix>,
    [](int dimension,material *mat) -> wrapped_arrays {
        wrapped_arrays r;
        r.data.reset(new float*[dimension+1]);
        
        auto tri = obj_Triangle::create(dimension,mat);
        r.obj = py::new_ref(reinterpret_cast<PyObject*>(tri));
        
        r.data[0] = tri->p1.data();
        r.data[1] = tri->face_normal.data();
        for(int i=0; i<dimension-1; ++i) r.data[i+2] = tri->items()[i].data();
        
        return r;
    },
    [](PyObject *tri) { reinterpret_cast<obj_Triangle*>(tri)->recalculate_d(); },
    [](int dimension,int type,material *mat) -> wrapped_solid {
        wrapped_solid r;
        
        auto s = new obj_Solid(dimension,static_cast<solid_type>(type),mat);
        r.obj = py::new_ref(reinterpret_cast<PyObject*>(s));
        r.orientation = s->orientation.data();
        r.position = s->position.data();
        
        return r;
    },
    [](PyObject *sobj) -> void {
        auto s = reinterpret_cast<obj_Solid*>(sobj);
        s->inv_orientation = s->orientation.inverse();
    }
};


PyTypeObject *classes[] = {
    obj_BoxScene::pytype(),
    obj_CompositeScene::pytype(),
    obj_Primitive::pytype(),
    obj_Solid::pytype(),
    obj_Triangle::pytype(),
    obj_FrozenVectorView::pytype(),
    obj_KDNode::pytype(),
    obj_KDLeaf::pytype(),
    obj_KDBranch::pytype(),
    wrapped_type<n_vector>::pytype(),
    obj_MatrixProxy::pytype(),
    wrapped_type<n_camera>::pytype(),
    obj_CameraAxes::pytype(),
    wrapped_type<n_matrix>::pytype(),
    wrapped_type<n_aabb>::pytype(),
    obj_PrimitivePrototype::pytype(),
    obj_TrianglePrototype::pytype(),
    wrapped_type<n_detached_triangle_point>::pytype(),
    obj_TrianglePointData::pytype(),
    wrapped_type<n_solid_prototype>::pytype(),
    wrapped_type<n_point_light>::pytype(),
    wrapped_type<n_global_light>::pytype(),
    cs_light_list<point_light_list_base>::pytype(),
    cs_light_list<global_light_list_base>::pytype()};


PyTypeObject *get_pytype(py::object mod,const char *name) {
    py::object obj = mod.attr(name);
    if(!PyType_CheckExact(obj.ref())) {
        PyErr_Format(PyExc_TypeError,"render.%s is supposed to be a class",name);
        throw py_error_set();
    }

    return reinterpret_cast<PyTypeObject*>(obj.ref());
}

#if PY_MAJOR_VERSION >= 3
#define INIT_ERR_VAL 0

PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    MODULE_STR,
    NULL,
    0,
    func_table,
    NULL,
    NULL,
    [](PyObject *self) -> int {
        (*package_common_data.invalidate_reference)(self);
        return 0;
    },
    NULL
};

extern "C" SHARED(PyObject) * APPEND_MODULE_NAME(PyInit_)() {
#else
#define INIT_ERR_VAL

extern "C" SHARED(void) APPEND_MODULE_NAME(init)() {
#endif
    using namespace py;

    if(!package_common_data.read_color) {
        try {
            object rmod = py::import_module(PACKAGE_STR ".render");

            auto stype = get_pytype(rmod,"Scene");
            obj_BoxScene::pytype()->tp_base = stype;
            obj_CompositeScene::pytype()->tp_base = stype;

            color_obj_base::_pytype = get_pytype(rmod,"Color");
            material::_pytype = get_pytype(rmod,"Material");

            auto pdata = reinterpret_cast<const package_common*>(
                PyCapsule_GetPointer(static_cast<object>(rmod.attr("_PACKAGE_COMMON")).ref(),"render._PACKAGE_COMMON"));
            if(!pdata) return INIT_ERR_VAL;
            package_common_data = *pdata;
        } PY_EXCEPT_HANDLERS(INIT_ERR_VAL)
    }

    for(auto cls : classes) {
        if(UNLIKELY(PyType_Ready(cls) < 0)) return INIT_ERR_VAL;
    }

#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3(MODULE_STR,func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;
        
    for(auto cls : classes) {
        add_class(m,cls->tp_name + sizeof(FULL_MODULE_STR),cls);
    }
    
    PyObject *c = PyCapsule_New(&module_constructors,"_CONSTRUCTORS",nullptr);
    if(UNLIKELY(!c)) return INIT_ERR_VAL;
    PyModule_AddObject(m,"_CONSTRUCTORS",c);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

