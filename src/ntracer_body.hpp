
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


typedef vector<module_store> n_vector;
typedef matrix<module_store> n_matrix;
typedef camera<module_store> n_camera;
typedef solid_prototype<module_store> n_solid_prototype;
typedef triangle_prototype<module_store> n_triangle_prototype;
typedef triangle_batch_prototype<module_store> n_triangle_batch_prototype;
typedef aabb<module_store> n_aabb;
typedef point_light<module_store> n_point_light;
typedef global_light<module_store> n_global_light;
typedef vector<module_store,v_real> n_vector_batch;


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

template<typename T> void fill_vector(std::vector<T,simd::allocator<T>> &v,Py_ssize_t size,PyObject **items) {
    v.reserve(size);
    for(Py_ssize_t i=0; i<size; ++i) v.push_back(from_pyobject<T>(items[i]));
}

template<typename T> std::vector<T,simd::allocator<T>> collect(PyObject *src) {
    std::vector<T,simd::allocator<T>> items;

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
struct ROOT ## _obj_base : py::pyobj_subclass { \
    CONTAINED_PYTYPE_DEF \
    PyObject_HEAD \
}; \
template<> struct _wrapped_type<n_ ## ROOT> { \
    typedef simple_py_wrapper<n_ ## ROOT,ROOT ## _obj_base> type; \
}


SIMPLE_WRAPPER(vector);
SIMPLE_WRAPPER(vector_batch);
SIMPLE_WRAPPER(matrix);
SIMPLE_WRAPPER(camera);


template<typename T,typename Base> PyObject *to_pyobject(const ::impl::vector_expr<T,Base> &e) {
    return to_pyobject(n_vector(e));
}


struct obj_BoxScene : obj_Scene {
    CONTAINED_PYTYPE_DEF

    box_scene<module_store> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    obj_BoxScene(int d) : base(d), idict(nullptr), weaklist(nullptr) {
        _get_base = &obj_BoxScene::scene_get_base;
        PyObject_Init(py::ref(this),pytype());
    }
    obj_BoxScene(box_scene<module_store> const &b) : base(b), idict(nullptr), weaklist(nullptr) {
        _get_base = &obj_BoxScene::scene_get_base;
        PyObject_Init(py::ref(this),pytype());
    }
    ~obj_BoxScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(py::ref(this));
    }

    box_scene<module_store> &cast_base() { return base; }
    box_scene<module_store> &get_base() { return base; }
    static scene &scene_get_base(obj_Scene *self) { return static_cast<obj_BoxScene*>(self)->base; }
};

template<> struct _wrapped_type<box_scene<module_store>> {
    typedef obj_BoxScene type;
};


constexpr char matrix_proxy_name[] = FULL_MODULE_STR ".MatrixProxy";
typedef py::obj_array_adapter<real,matrix_proxy_name,false,true> obj_MatrixProxy;


struct obj_CameraAxes : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF

    PyObject_HEAD
    py::pyptr<wrapped_type<n_camera>> base;
    PY_MEM_NEW_DELETE

    obj_CameraAxes(wrapped_type<n_camera> *base) : base(py::borrowed_ref(base)) {
        PyObject_Init(py::ref(this),pytype());
    }
};


struct obj_CompositeScene;
struct composite_scene_obj_base : obj_Scene{
    CONTAINED_PYTYPE_DEF
};
struct obj_CompositeScene : private simple_py_wrapper<composite_scene<module_store>,composite_scene_obj_base>, virtual py::pyobj_subclass {
    friend PyObject *obj_CompositeScene_new(PyTypeObject*,PyObject*,PyObject*);

    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    template<typename... T> obj_CompositeScene(T&&... x) : simple_py_wrapper(std::forward<T>(x)...), idict(nullptr), weaklist(nullptr) {
        _get_base = &obj_CompositeScene::scene_get_base;
    }

    ~obj_CompositeScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(py::ref(this));
    }

    using simple_py_wrapper::get_base;
    using simple_py_wrapper::cast_base;
    using composite_scene_obj_base::pytype;

    static scene &scene_get_base(obj_Scene *self) { return static_cast<obj_CompositeScene*>(self)->get_base(); }
};

template<> struct _wrapped_type<composite_scene<module_store>> {
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


struct obj_Primitive : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

struct obj_PrimitiveBatch : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

typedef solid<module_store> obj_Solid;
typedef triangle<module_store> obj_Triangle;

typedef triangle_batch<module_store> obj_TriangleBatch;

template<> primitive<module_store> *checked_py_cast<primitive<module_store>>(PyObject *o) {
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

struct obj_KDNode : py::pyobj_subclass {
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

    kd_branch<module_store> *data() const {
        assert(_data);
        return static_cast<kd_branch<module_store>*>(_data);
    }

    obj_KDBranch(py::nullable<py::object> parent,kd_branch<module_store> *data,int dimension) : obj_KDNode(parent,data), dimension(dimension) {
        PyObject_Init(py::ref(this),pytype());
    }
    ~obj_KDBranch() {
        if(!parent) delete data();
    }
};

struct obj_KDLeaf : obj_KDNode {
    CONTAINED_PYTYPE_DEF

    kd_leaf<module_store> *data() const {
        assert(_data);
        return static_cast<kd_leaf<module_store>*>(_data);
    }

    obj_KDLeaf(py::nullable<py::object> parent,kd_leaf<module_store> *data) : obj_KDNode(parent,data) {
        PyObject_Init(py::ref(this),pytype());
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


int intersection_index(const intersection_target<module_store,false>&) { return -1; }
int intersection_index(const intersection_target<module_store,true> &t) { return t.index; }

struct py_ray_intersection {
    real dist;
    n_vector origin;
    n_vector normal;
    py::object p;
    int index;

    py_ray_intersection(real dist,const n_vector &origin,const n_vector &normal,PyObject *p,int index)
        : dist(dist), origin(origin), normal(normal), p(py::borrowed_ref(p)), index(index) {}

    py_ray_intersection(const ray_intersection<module_store> &ri) :
        dist(ri.dist),
        origin(ri.normal.origin),
        normal(ri.normal.direction),
        p(py::borrowed_ref(ri.target.p)),
        index(intersection_index(ri.target)) {}
};
struct py_ray_intersection_obj_base : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};
template<> struct _wrapped_type<py_ray_intersection> {
    typedef simple_py_wrapper<py_ray_intersection,py_ray_intersection_obj_base> type;
};



struct aabb_obj_base : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

template<> struct _wrapped_type<n_aabb> {
    typedef simple_py_wrapper<n_aabb,aabb_obj_base,true> type;
};


struct obj_PrimitivePrototype : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD

    primitive_prototype<module_store> &get_base();
};


struct triangle_prototype_obj_base : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PY_MEM_NEW_DELETE
    typedef n_triangle_prototype actual;
    typedef triangle_point<module_store,real> point_t;
};

struct triangle_batch_prototype_obj_base : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PY_MEM_NEW_DELETE
    typedef n_triangle_batch_prototype actual;
    typedef triangle_point<module_store,v_real> point_t;
};

/* Whether this object is variable-sized or not depends on whether T can vary in
   size and whether T can be stored in-place (which it can't if it requires
   extra alignment). If it is stored in-place, care must be taken to ensure it
   has enough room  */
template<typename T,bool Var,bool InPlace=(alignof(typename T::actual) <= PYOBJECT_ALIGNMENT)> struct tproto_base : T {
    typedef typename T::actual actual;

    PyObject_HEAD
    actual base;

    static void *operator new(size_t,int dimension) {
        void *ptr = PyObject_Malloc(base_size);
        if(!ptr) throw std::bad_alloc();
        return ptr;
    }

    template<typename... Args> tproto_base(Args&&... args) : base(std::forward<Args>(args)...) {
        PyObject_Init(py::ref(this),T::pytype());
    }

    actual &alloc_base(int dimension) {
        return base;
    }
    actual &get_base() {
        return base;
    }

    static const size_t base_size = actual::item_offset
        + sizeof(typename T::point_t)*module_store::required_d
        + offsetof(tproto_base,base);
    static const size_t item_size = 0;
};

template<typename T,bool Var> struct tproto_base<T,Var,false> : T {
    typedef typename T::actual actual;

    PyObject_HEAD
    std::unique_ptr<actual> base;

    static void *operator new(size_t,int) {
        void *ptr = PyObject_Malloc(base_size);
        if(!ptr) throw std::bad_alloc();
        return ptr;
    }

    template<typename... Args> tproto_base(int dimension,Args&&... args) : base(new(dimension) actual(dimension,std::forward<Args>(args)...)) {
        PyObject_Init(py::ref(this),T::pytype());
    }

    actual &alloc_base(int dimension) {
        new(&base) std::unique_ptr<actual>(
            reinterpret_cast<actual*>(actual::operator new(actual::item_offset,dimension)));
        return *base;
    }
    actual &get_base() {
        return *base;
    }

    static const size_t base_size;
    static const size_t item_size = 0;
};
template<typename T,bool Var> const size_t tproto_base<T,Var,false>::base_size = sizeof(tproto_base<T,Var,false>);

template<typename T> struct tproto_base<T,true,true> : T {
    typedef typename T::actual actual;

    PyObject_VAR_HEAD
    actual base;

    static void *operator new(size_t,int dimension) {
        void *ptr = PyObject_Malloc(base_size + item_size * dimension);
        if(!ptr) throw std::bad_alloc();
        return ptr;
    }

    template<typename... Args> tproto_base(Args&&... args) : base(std::forward<Args>(args)...) {
        PyObject_Init(py::ref(this),T::pytype());
    }

    actual &alloc_base(int dimension) {
        return base;
    }
    actual &get_base() {
        return base;
    }

    static const size_t base_size;
    static const size_t item_size = actual::item_size;
};
#define COMMA_WORKAROUND tproto_base<T,true,true>
template<typename T> const size_t tproto_base<T,true,true>::base_size = T::actual::item_offset + offsetof(COMMA_WORKAROUND,base);
#undef COMMA_WORKAROUND

typedef tproto_base<triangle_prototype_obj_base,!module_store::required_d> obj_TrianglePrototype;
template<> struct _wrapped_type<n_triangle_prototype> {
    typedef obj_TrianglePrototype type;
};

typedef tproto_base<triangle_batch_prototype_obj_base,!module_store::required_d> obj_TriangleBatchPrototype;
template<> struct _wrapped_type<n_triangle_batch_prototype> {
    typedef obj_TriangleBatchPrototype type;
};


template<typename T> struct n_detatched_triangle_point {
    vector<module_store,T> point;
    vector<module_store,T> edge_normal;

    n_detatched_triangle_point(const triangle_point<module_store,T> &tp) : point(tp.point), edge_normal(tp.edge_normal) {}
};

template<typename T> struct detatched_triangle_point_obj_base : py::pyobj_subclass {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD

    static PyGetSetDef getset[];
};
template<typename T> struct _wrapped_type<n_detatched_triangle_point<T> > {
    typedef simple_py_wrapper<n_detatched_triangle_point<T>,detatched_triangle_point_obj_base<T> > type;
};

template<typename T> PyObject *to_pyobject(const triangle_point<module_store,T> &tp) {
    return py::ref(new wrapped_type<n_detatched_triangle_point<T> >(tp));
}

constexpr char triangle_point_data_name[] = FULL_MODULE_STR ".TrianglePointData";
typedef py::obj_array_adapter<triangle_point<module_store,real>,triangle_point_data_name,false,true> obj_TrianglePointData;

constexpr char triangle_batch_point_data_name[] = FULL_MODULE_STR ".TriangleBatchPointData";
typedef py::obj_array_adapter<triangle_point<module_store,v_real>,triangle_batch_point_data_name,false,true> obj_TriangleBatchPointData;


SIMPLE_WRAPPER(solid_prototype);

primitive_prototype<module_store> &obj_PrimitivePrototype::get_base() {
    if(PyObject_TypeCheck(py::ref(this),obj_TrianglePrototype::pytype()))
        return reinterpret_cast<obj_TrianglePrototype*>(this)->get_base();

    if(PyObject_TypeCheck(py::ref(this),obj_TriangleBatchPrototype::pytype()));
    return reinterpret_cast<obj_TriangleBatchPrototype*>(this)->get_base();

    assert(PyObject_TypeCheck(py::ref(this),wrapped_type<n_solid_prototype>::pytype()));
    return reinterpret_cast<wrapped_type<n_solid_prototype>*>(this)->get_base();
}

SIMPLE_WRAPPER(point_light);
SIMPLE_WRAPPER(global_light);

template<typename T> struct cs_light_list : T, py::pyobj_subclass {
    static PySequenceMethods sequence_methods;
    static PyMethodDef methods[];

    CONTAINED_PYTYPE_DEF
    PY_MEM_NEW_DELETE
    PyObject_HEAD
    py::pyptr<obj_CompositeScene> parent;

    cs_light_list(obj_CompositeScene *parent) : parent(py::borrowed_ref(parent)) {
        PyObject_Init(py::ref(this),pytype());
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



FIX_STACK_ALIGN PyObject *obj_BoxScene_set_camera(obj_BoxScene *self,PyObject *arg) {
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

FIX_STACK_ALIGN PyObject *obj_BoxScene_get_camera(obj_BoxScene *self,PyObject *) {
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

FIX_STACK_ALIGN PyObject *obj_BoxScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_BoxScene*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                auto [d] = get_arg::get_args("BoxScene.__new__",args,kwds,param<real>("dimension"));

                check_dimension(d);

                new(&ptr->base) box_scene<module_store>(d);
                ptr->_get_base = &obj_BoxScene::scene_get_base;
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(nullptr)

        ptr->weaklist = nullptr;
    }
    return py::ref(ptr);
}

PyGetSetDef obj_BoxScene_getset[] = {
    {"locked",OBJ_GETTER(obj_BoxScene,self->base.locked),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_BoxScene_members[] = {
    {"fov",member_macro<real>::value,offsetof(obj_BoxScene,base.fov),READONLY,NULL},
    {NULL}
};

PyTypeObject obj_BoxScene::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".BoxScene",
    .tp_basicsize = sizeof(obj_BoxScene),
    .tp_dealloc = destructor_dealloc<obj_BoxScene>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &traverse_idict<obj_BoxScene>,
    .tp_clear = &clear_idict<obj_BoxScene>,
    .tp_weaklistoffset = offsetof(obj_BoxScene,weaklist),
    .tp_methods = obj_BoxScene_methods,
    .tp_members = obj_BoxScene_members,
    .tp_getset = obj_BoxScene_getset,
    .tp_dictoffset = offsetof(obj_BoxScene,idict),
    .tp_new = &obj_BoxScene_new};


FIX_STACK_ALIGN PyObject *obj_CompositeScene_set_camera(obj_CompositeScene *self,PyObject *arg) {
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

FIX_STACK_ALIGN PyObject *obj_CompositeScene_get_camera(obj_CompositeScene *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().cam);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_CompositeScene_set_ambient(obj_CompositeScene *self,PyObject *arg) {
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

FIX_STACK_ALIGN PyObject *obj_CompositeScene_add_light(obj_CompositeScene *self,PyObject *arg) {
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

FIX_STACK_ALIGN PyObject *obj_CompositeScene_set_background(obj_CompositeScene *self,PyObject *args,PyObject *kwds) {
    try {
        ensure_unlocked(self);
        auto &base = self->get_base();

        const char *names[] = {"c1","c2","c3","axis",nullptr};
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

FIX_STACK_ALIGN PyObject *obj_CompositeScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(ptr) {
        try {
            try {
                const char *names[] = {"boundary","data",nullptr};
                get_arg ga(args,kwds,names,"CompositeScene.__new__");

                auto &boundary = get_base<n_aabb>(ga(true));
                auto d_node = checked_py_cast<obj_KDNode>(ga(true));

                ga.finished();

                if(d_node->parent) THROW_PYERR_STRING(ValueError,"\"data\" must not be already attached to another node");

                if(boundary.dimension() != d_node->dimension())
                    THROW_PYERR_STRING(TypeError,"\"boundary\" and \"data\" must have the same dimesion");

                auto &base = reinterpret_cast<obj_CompositeScene*>(ptr)->alloc_base();

                new(&base) composite_scene<module_store>(boundary,d_node->_data);
                reinterpret_cast<obj_CompositeScene*>(ptr)->_get_base = &obj_CompositeScene::scene_get_base;
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

    if(node->type == LEAF) return py::ref(new obj_KDLeaf(py::borrowed_ref(parent),static_cast<kd_leaf<module_store>*>(node)));

    assert(node->type == BRANCH);
    return py::ref(new obj_KDBranch(py::borrowed_ref(parent),static_cast<kd_branch<module_store>*>(node),dimension));
}

FIX_STACK_ALIGN PyObject *obj_CompositeScene_get_ambient_color(obj_CompositeScene *self,void*) {
    try {
        return to_pyobject(self->get_base().ambient);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_CompositeScene_get_bg1(obj_CompositeScene *self,void*) {
    try {
        return to_pyobject(self->get_base().bg1);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_CompositeScene_get_bg2(obj_CompositeScene *self,void*) {
    try {
        return to_pyobject(self->get_base().bg2);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_CompositeScene_get_bg3(obj_CompositeScene *self,void*) {
    try {
        return to_pyobject(self->get_base().bg3);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_CompositeScene_getset[] = {
    {"locked",OBJ_GETTER(obj_CompositeScene,self->get_base().locked),NULL,NULL,NULL},
    {"fov",OBJ_GETTER(obj_CompositeScene,self->get_base().fov),NULL,NULL,NULL},
    {"max_reflect_depth",OBJ_GETTER(obj_CompositeScene,self->get_base().max_reflect_depth),NULL,NULL,NULL},
    {"shadows",OBJ_GETTER(obj_CompositeScene,self->get_base().shadows),NULL,NULL,NULL},
    {"camera_light",OBJ_GETTER(obj_CompositeScene,self->get_base().camera_light),NULL,NULL,NULL},
    {"ambient_color",reinterpret_cast<getter>(&obj_CompositeScene_get_ambient_color),NULL,NULL,NULL},
    {"bg1",reinterpret_cast<getter>(&obj_CompositeScene_get_bg1),NULL,NULL,NULL},
    {"bg2",reinterpret_cast<getter>(&obj_CompositeScene_get_bg2),NULL,NULL,NULL},
    {"bg3",reinterpret_cast<getter>(&obj_CompositeScene_get_bg3),NULL,NULL,NULL},
    {"bg_gradient_axis",OBJ_GETTER(obj_CompositeScene,self->get_base().bg_gradient_axis),NULL,NULL,NULL},
    {"boundary",OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary))),NULL,NULL,NULL},
    {"root",OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(new_obj_node(obj_self,self->get_base().root.get(),self->get_base().dimension()))),NULL,NULL,NULL},
    {"point_lights",OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(new cs_light_list<point_light_list_base>(self))),NULL,NULL,NULL},
    {"global_lights",OBJ_GETTER(
        obj_CompositeScene,
        py::new_ref(new cs_light_list<global_light_list_base>(self))),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject composite_scene_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".CompositeScene",
    .tp_basicsize = sizeof(obj_CompositeScene),
    .tp_dealloc = destructor_dealloc<obj_CompositeScene>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &traverse_idict<obj_CompositeScene>,
    .tp_clear = &clear_idict<obj_CompositeScene>,
    .tp_weaklistoffset = offsetof(obj_CompositeScene,weaklist),
    .tp_methods = obj_CompositeScene_methods,
    .tp_getset = obj_CompositeScene_getset,
    .tp_dictoffset = offsetof(obj_CompositeScene,idict),
    .tp_new = &obj_CompositeScene_new};


void check_index(const n_camera &c,Py_ssize_t index) {
    if(index < 0 || index >= c.dimension()) THROW_PYERR_STRING(IndexError,"index out of range");
}

PySequenceMethods obj_CameraAxes_sequence_methods = {
    .sq_length = [](PyObject *self) -> Py_ssize_t {
        return reinterpret_cast<obj_CameraAxes*>(self)->base->get_base().dimension();
    },
    .sq_item = [](PyObject *self,Py_ssize_t index) FIX_STACK_ALIGN -> PyObject* {
        try {
            auto &base = reinterpret_cast<obj_CameraAxes*>(self)->base->get_base();
            check_index(base,index);
            return to_pyobject(n_vector(base.t_orientation[index]));
        } PY_EXCEPT_HANDLERS(nullptr)
    },
    .sq_ass_item = [](PyObject *self,Py_ssize_t index,PyObject *value) FIX_STACK_ALIGN -> int {
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
    }
};

PyTypeObject obj_CameraAxes::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".CameraAxes",
    .tp_basicsize = sizeof(obj_BoxScene),
    .tp_dealloc = destructor_dealloc<obj_CameraAxes>::value,
    .tp_as_sequence = &obj_CameraAxes_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the CameraAxes type cannot be instantiated directly");
        return nullptr;
    }};


void check_origin_dir_compat(const n_vector &o,const n_vector &d) {
    if(!compatible(o,d))
        THROW_PYERR_STRING(TypeError,"\"origin\" and \"direction\" must have the same dimension");
}

FIX_STACK_ALIGN PyObject *obj_Primitive_intersects(primitive<module_store> *self,PyObject *args,PyObject *kwds) {
    try {
        auto [origin,direction] = get_arg::get_args("Primitive.intersects",args,kwds,
            param<n_vector>("origin"),
            param<n_vector>("direction"));

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

PyTypeObject obj_Primitive::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Primitive",
    .tp_basicsize = sizeof(obj_Primitive),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_Primitive_methods,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the Primitive type cannot be instantiated directly");
        return nullptr;
    }};


FIX_STACK_ALIGN PyObject *obj_PrimitiveBatch_intersects(primitive_batch<module_store> *self,PyObject *args,PyObject *kwds) {
    try {
        auto vals = get_arg::get_args("PrimitiveBatch.intersects",args,kwds,
            param<n_vector>("origin"),
            param<n_vector>("direction"),
            param<int>("index"));

        check_origin_dir_compat(std::get<0>(vals),std::get<1>(vals));

        ray<module_store> target{std::get<0>(vals),std::get<1>(vals)};
        ray<module_store> normal{std::get<0>(vals).dimension()};
        int index = std::get<2>(vals);

        real dist = self->intersects(target,normal,index);
        if(!dist) Py_RETURN_NONE;

        return py::make_tuple(dist,normal.origin,normal.direction,index).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_PrimitiveBatch_methods[] = {
    {"intersects",reinterpret_cast<PyCFunction>(&obj_PrimitiveBatch_intersects),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyTypeObject obj_PrimitiveBatch::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".PrimitiveBatch",
    .tp_basicsize = sizeof(obj_PrimitiveBatch),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_PrimitiveBatch_methods,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the PrimitiveBatch type cannot be instantiated directly");
        return nullptr;
    }};


template<typename T> int kd_tree_item_traverse(PyObject *self,visitproc visit,void *arg) {
    return reinterpret_cast<T*>(self)->parent.gc_traverse(visit,arg);
}

template<typename T> int kd_tree_item_clear(PyObject *self) {
    T *obj = reinterpret_cast<T*>(self);
    if(obj->parent) {
        obj->_data = nullptr;
        obj->parent.gc_clear();
    }
    return 0;
}

template<> solid_type from_pyobject<solid_type>(PyObject *o) {
    int t = from_pyobject<int>(o);
    if(t != CUBE && t != SPHERE) THROW_PYERR_STRING(ValueError,"invalid shape type");
    return static_cast<solid_type>(t);
}

FIX_STACK_ALIGN PyObject *obj_Solid_reduce(obj_Solid *self,PyObject*) {
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

FIX_STACK_ALIGN PyObject *obj_Solid_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"type","position","orientation","material",nullptr};
        get_arg ga(args,kwds,names,"Solid.__new__");
        auto type = from_pyobject<solid_type>(ga(true));
        auto position = from_pyobject<n_vector>(ga(true));
        auto &orientation = get_base<n_matrix>(ga(true));
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();

        if(!compatible(orientation,position))
            THROW_PYERR_STRING(TypeError,"the position and orientation must have the same dimension");

        return py::ref(new obj_Solid(type,orientation,position,m));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Solid_get_orientation(obj_Solid *self,void*) {
    try {
        return to_pyobject(self->orientation);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Solid_get_inv_orientation(obj_Solid *self,void*) {
    try {
        return to_pyobject(self->inv_orientation);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Solid_get_position(obj_Solid *self,void*) {
    try {
        return to_pyobject(self->position);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Solid_getset[] = {
    {"type",OBJ_GETTER(obj_Solid,int(self->type)),NULL,NULL,NULL},
    {"orientation",reinterpret_cast<getter>(&obj_Solid_get_orientation),NULL,NULL,NULL},
    {"inv_orientation",reinterpret_cast<getter>(&obj_Solid_get_inv_orientation),NULL,NULL,NULL},
    {"position",reinterpret_cast<getter>(&obj_Solid_get_position),NULL,NULL,NULL},
    {"dimension",OBJ_GETTER(obj_Solid,self->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_Solid_members[] = {
    {"material",T_OBJECT_EX,offsetof(obj_Triangle,m),READONLY,NULL},
    {NULL}
};

PyTypeObject solid_obj_common::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Solid",
    .tp_basicsize = sizeof(obj_Solid),
    .tp_dealloc = destructor_dealloc<obj_Solid>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = obj_Solid_methods,
    .tp_members = obj_Solid_members,
    .tp_getset = obj_Solid_getset,
    .tp_base = obj_Primitive::pytype(),
    .tp_new = &obj_Solid_new,
    .tp_free = reinterpret_cast<freefunc>(&dealloc_uninitialized<obj_Solid>)};


std::vector<n_vector,simd::allocator<n_vector>> points_for_triangle(PyObject *obj) {
    auto points = collect<n_vector>(obj);
    if(points.empty()) THROW_PYERR_STRING(TypeError,"a sequence of points (vectors) is required");

    int dim = points[0].dimension();
    for(size_t i=1; i<points.size(); ++i) {
        if(points[i].dimension() != dim) THROW_PYERR_STRING(TypeError,"all points must have the same dimension");
    }

    if(size_t(dim) != points.size()) THROW_PYERR_STRING(ValueError,"the number of points must equal their dimension");

    return points;
}

FIX_STACK_ALIGN PyObject *obj_Triangle_from_points(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"points","material",nullptr};
        get_arg ga(args,kwds,names,"Triangle.from_points");
        auto obj_points = ga(true);
        auto m = checked_py_cast<material>(ga(true));
        ga.finished();

        return py::ref(obj_Triangle::from_points(points_for_triangle(obj_points).data(),m));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Triangle_reduce(obj_Triangle *self,PyObject*) {
    try {
        struct item_size {
            static constexpr int get(int d) { return d+1; }
        };

        module_store::type<item_size,const real*> values(self->dimension());
        values.items.raw[0] = self->p1.data();
        values.items.raw[1] = self->face_normal.data();
        for(int i=0; i<self->dimension()-1; ++i) values.items.raw[i+2] = self->items()[i].data();

        return (*package_common_data.triangle_reduce)(self->dimension(),values.items.raw,self->m.get());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Triangle_methods[] = {
    {"from_points",reinterpret_cast<PyCFunction>(&obj_Triangle_from_points),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Triangle_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

FIX_STACK_ALIGN PyObject *obj_Triangle_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    const char dim_err[] = "all supplied vectors must have the same dimension";

    try {
        const char *names[] = {"p1","face_normal","edge_normals","material",nullptr};
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

        /* Triangle objects have special alignment requirements and don't use
           Python's memory allocator */
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

        return py::ref(ptr);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Triangle_get_edges(obj_Triangle *self,void*) {
    try {
        return py::ref(new obj_FrozenVectorView(
            py::ref(self),
            self->items().size(),
            self->items()));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Triangle_get_face_normal(obj_Triangle *self,void*) {
    try {
        return to_pyobject(self->face_normal);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Triangle_getset[] = {
    {"dimension",OBJ_GETTER(obj_Triangle,self->dimension()),NULL,NULL,NULL},
    {"p1",OBJ_GETTER(obj_Triangle,self->p1),NULL,NULL,NULL},
    {"face_normal",reinterpret_cast<getter>(&obj_Triangle_get_face_normal),NULL,NULL,NULL},
    {"edge_normals",reinterpret_cast<getter>(&obj_Triangle_get_edges),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_Triangle_members[] = {
    {"d",member_macro<real>::value,offsetof(obj_Triangle,d),READONLY,NULL},
    {"material",T_OBJECT_EX,offsetof(obj_Triangle,m),READONLY,NULL},
    {NULL}
};

PyTypeObject triangle_obj_common::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Triangle",
    .tp_basicsize = obj_Triangle::base_size,
    .tp_itemsize = obj_Triangle::item_size,
    .tp_dealloc = destructor_dealloc<obj_Triangle>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = obj_Triangle_methods,
    .tp_members = obj_Triangle_members,
    .tp_getset = obj_Triangle_getset,
    .tp_base = obj_Primitive::pytype(),
    .tp_new = &obj_Triangle_new,
    .tp_free = reinterpret_cast<freefunc>(&dealloc_uninitialized<obj_Triangle>)};


FIX_STACK_ALIGN PyObject *obj_TriangleBatch_sequence_getitem(obj_TriangleBatch *self,Py_ssize_t index) {
    try {
        if(UNLIKELY(index < 0 || index >= static_cast<Py_ssize_t>(v_real::size))) {
            PyErr_SetString(PyExc_IndexError,"index out of range");
            return nullptr;
        }
        return py::ref(obj_Triangle::create(
            interleave1<module_store,v_real::size>(self->p1,index),
            interleave1<module_store,v_real::size>(self->face_normal,index),
            [=](int i) { return interleave1<module_store,v_real::size>(self->items()[i],index); },
            self->m[index].get()));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PySequenceMethods obj_TriangleBatch_sequence_methods = {
    .sq_length = [](PyObject*){ return static_cast<Py_ssize_t>(v_real::size); },
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_TriangleBatch_sequence_getitem)};

FIX_STACK_ALIGN PyObject *obj_TriangleBatch_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        auto arg = std::get<0>(get_arg::get_args("TriangleBatch.__new__",args,kwds,
            param("triangles")));

        auto tris = sized_iter(py::borrowed_ref(arg),v_real::size);

        py::pyptr<obj_Triangle> tmp[v_real::size];

        for(size_t i=0; i<v_real::size; ++i) {
            tmp[i] = py::borrowed_ref(checked_py_cast<obj_Triangle>(tris.next().ref()));
        }
        tris.finished();

        /* TriangleBatch objects have special alignment requirements and don't
           use Python's memory allocator */
        return py::ref(obj_TriangleBatch::from_triangles([&](int i){ return tmp[i].get(); }));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_TriangleBatch_methods[] = {
    //{"__reduce__",reinterpret_cast<PyCFunction>(&obj_TriangleBatch_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

PyTypeObject triangle_batch_obj_common::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".TriangleBatch",
    .tp_basicsize = obj_TriangleBatch::base_size,
    .tp_itemsize = obj_TriangleBatch::item_size,
    .tp_dealloc = destructor_dealloc<obj_TriangleBatch>::value,
    .tp_as_sequence = &obj_TriangleBatch_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = obj_TriangleBatch_methods,
    .tp_base = obj_PrimitiveBatch::pytype(),
    .tp_new = &obj_TriangleBatch_new,
    .tp_free = reinterpret_cast<freefunc>(&dealloc_uninitialized<obj_TriangleBatch>)};


size_t fill_ray_intersections(const ray_intersections<module_store> &hits,PyObject *list) {
    auto data = hits.data();
    size_t i = 0;
    for(; i<hits.size(); ++i) PyList_SET_ITEM(list,i,py::ref(new wrapped_type<py_ray_intersection>(data[i])));
    return i;
}

void bad_target() {
    THROW_PYERR_STRING(TypeError,"\"source\" must be an instance of Primitive or PrimitiveBatch");
}

void set_target(intersection_target<module_store,false> &t,PyObject *p,int index) {
    if(!p || PyObject_TypeCheck(p,obj_Primitive::pytype())) t.p = reinterpret_cast<primitive<module_store>*>(p);

    // if kd_leaf objects can't have any batch primtives, this is equivalent
    else if(PyObject_TypeCheck(p,obj_PrimitiveBatch::pytype())) t.p = nullptr;

    else bad_target();
}
void set_target(intersection_target<module_store,true> &t,PyObject *p,int index) {
    t.p = p;

    if(!p || PyObject_TypeCheck(p,obj_Primitive::pytype())) t.index = -1;
    else if(PyObject_TypeCheck(p,obj_PrimitiveBatch::pytype())) t.index = index;
    else bad_target();
}

FIX_STACK_ALIGN PyObject *kdnode_intersects(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);

        auto [origin,direction,t_near,t_far,source,batch_index]
            = get_arg::get_args("KDNode.intersects",args,kwds,
            param<n_vector>("origin"),
            param<n_vector>("direction"),
            param<real>("t_near",std::numeric_limits<real>::lowest()),
            param<real>("t_far",std::numeric_limits<real>::max()),
            param<PyObject*>("source",nullptr),
            param<int>("batch_index",-1));

        check_origin_dir_compat(origin,direction);

        intersection_target<module_store> skip;
        auto _none = Py_None; // doing this avoids a "will never be NULL" warning on GCC
        set_target(skip,source == _none ? nullptr : source,batch_index);

        ray_intersection<module_store> o_hit(origin.dimension());
        ray_intersections<module_store> t_hits;
        bool did_hit;
        {
            py::allow_threads _;
            ray<module_store> target{origin,direction};

            did_hit = intersects(self->_data,target,skip,o_hit,t_hits,t_near,t_far);
        }

        size_t r_size = t_hits.size();
        if(did_hit) ++r_size;
        PyObject *r = PyList_New(r_size);
        if(UNLIKELY(!r)) return nullptr;

        try {
            auto i = fill_ray_intersections(t_hits,r);
            if(did_hit) PyList_SET_ITEM(r,i,py::ref(new wrapped_type<py_ray_intersection>(o_hit)));
        } catch(...) {
            Py_DECREF(r);
            throw;
        }

        return r;
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *kdnode_occludes(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);

        auto [origin,direction,distance,t_near,t_far,source,batch_index]
            = get_arg::get_args("KDNode.occludes",args,kwds,
            param<n_vector>("origin"),
            param<n_vector>("direction"),
            param<real>("distance",std::numeric_limits<real>::max()),
            param<real>("t_near",std::numeric_limits<real>::lowest()),
            param<real>("t_far",std::numeric_limits<real>::max()),
            param<PyObject*>("source",nullptr),
            param<int>("batch_index",-1));

        check_origin_dir_compat(origin,direction);

        intersection_target<module_store> skip;
        set_target(skip,source,batch_index);
        ray_intersections<module_store> hits;
        bool occ;
        {
            py::allow_threads _;
            ray<module_store> target{origin,direction};

            occ = occludes(self->_data,target,distance,skip,hits,t_near,t_far);
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

PyTypeObject obj_KDNode::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".KDNode",
    .tp_basicsize = sizeof(obj_KDNode),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_KDNode_methods,
    .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the KDNode type cannot be instantiated directly");
        return nullptr;
    }};


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
    .sq_length = reinterpret_cast<lenfunc>(&obj_KDLeaf___sequence_len__),
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_KDLeaf___sequence_getitem__)
};

inline int dim_at(const kd_leaf<module_store,false> *n,size_t i) {
    return n->items()[i]->dimension();
}
inline int dim_at(const kd_leaf<module_store,true> *n,size_t i) {
    return i < n->batches ?
        reinterpret_cast<primitive_batch<module_store>*>(n->items()[i].ref())->dimension() :
        reinterpret_cast<primitive<module_store>*>(n->items()[i].ref())->dimension();
}

inline kd_leaf<module_store,false> *create_kd_leaf(Py_ssize_t size,py::tuple primitives,const kd_leaf<module_store,false>*) {
    return kd_leaf<module_store,false>::create(size,[=](size_t i) -> primitive<module_store>* {
            checked_py_cast<primitive<module_store> >(primitives[i].ref());
            return reinterpret_cast<primitive<module_store>*>(primitives[i].new_ref());
        });
}
inline kd_leaf<module_store,true> *create_kd_leaf(Py_ssize_t size,py::tuple primitives,const kd_leaf<module_store,true>*) {
    return kd_leaf<module_store,true>::create(size,[=](size_t i) -> py::object {
            if(!PyObject_TypeCheck(primitives[i].ref(),obj_Primitive::pytype()) && !PyObject_TypeCheck(primitives[i].ref(),obj_PrimitiveBatch::pytype()))
                THROW_PYERR_STRING(TypeError,"each item must be an instance of Primitive or PrimitiveBatch");
            return primitives[i];
        });
}

FIX_STACK_ALIGN PyObject *obj_KDLeaf_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_KDLeaf*>(type->tp_alloc(type,0));
    if(!ptr) return nullptr;

    try {
        try {
            const char *names[] = {"primitives",nullptr};
            get_arg ga(args,kwds,names,"KDLeaf.__new__");
            py::tuple primitives(py::object(py::borrowed_ref(ga(true))));
            ga.finished();

            Py_ssize_t size = primitives.size();

            if(!size) THROW_PYERR_STRING(ValueError,"KDLeaf requires at least one item");

            ptr->_data = create_kd_leaf(size,primitives,static_cast<kd_leaf<module_store>*>(nullptr));

            int d = dim_at(ptr->data(),0);
            for(Py_ssize_t i=1; i<size; ++i) {
                if(dim_at(ptr->data(),i) != d) {
                    THROW_PYERR_STRING(TypeError,"every member of KDLeaf must have the same dimension");
                }
            }
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)

    return py::ref(ptr);
}

PyGetSetDef obj_KDLeaf_getset[] = {
    {"dimension",OBJ_GETTER(obj_KDLeaf,self->data()->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDLeaf::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".KDLeaf",
    .tp_basicsize = sizeof(obj_KDLeaf),
    .tp_dealloc = destructor_dealloc<obj_KDLeaf>::value,
    .tp_as_sequence = &obj_KDLeaf_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &kd_tree_item_traverse<obj_KDLeaf>,
    .tp_clear = &kd_tree_item_clear<obj_KDLeaf>,
    .tp_getset = obj_KDLeaf_getset,
    .tp_base = obj_KDNode::pytype(),
    .tp_new = &obj_KDLeaf_new};


obj_KDNode* acceptable_node(PyObject *obj) {
    if(!obj || obj == Py_None) return nullptr;

    if(Py_TYPE(obj) != obj_KDBranch::pytype() && Py_TYPE(obj) != obj_KDLeaf::pytype())
        THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" must be instances of " MODULE_STR ".KDNode");

    auto node = reinterpret_cast<obj_KDNode*>(obj);

    if(node->parent)
        THROW_PYERR_STRING(ValueError,"\"left\" and \"right\" must not already be attached to another node/scene");

    return node;
}

FIX_STACK_ALIGN PyObject *obj_KDBranch_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_KDBranch*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                const char *names[] = {"axis","split","left","right",nullptr};
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

                ptr->_data = new kd_branch<module_store>(axis,split,lnode ? lnode->_data : nullptr,rnode ? rnode->_data : nullptr);
                ptr->dimension = (lnode ? lnode : rnode)->dimension();
                if(lnode) lnode->parent = py::borrowed_ref(ptr);
                if(rnode) rnode->parent = py::borrowed_ref(ptr);
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(nullptr)
    }
    return py::ref(ptr);
}

PyObject *obj_KDBranch_get_child(obj_KDBranch *self,void *index) {
    assert(&self->data()->right == (&self->data()->left + 1));

    try {
        return new_obj_node(py::ref(self),(&self->data()->left)[reinterpret_cast<size_t>(index)].get(),self->dimension);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_KDBranch_getset[] = {
    {"axis",OBJ_GETTER(obj_KDBranch,self->data()->axis),NULL,NULL,NULL},
    {"split",OBJ_GETTER(obj_KDBranch,self->data()->split),NULL,NULL,NULL},
    {"left",reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(0)},
    {"right",reinterpret_cast<getter>(&obj_KDBranch_get_child),NULL,NULL,reinterpret_cast<void*>(1)},
    {"dimension",OBJ_GETTER(obj_KDBranch,self->dimension),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_KDBranch::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".KDBranch",
    .tp_basicsize = sizeof(obj_KDBranch),
    .tp_dealloc = destructor_dealloc<obj_KDBranch>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &kd_tree_item_traverse<obj_KDBranch>,
    .tp_clear = &kd_tree_item_clear<obj_KDBranch>,
    .tp_getset = obj_KDBranch_getset,
    .tp_base = obj_KDNode::pytype(),
    .tp_new = &obj_KDBranch_new};


FIX_STACK_ALIGN PyObject *obj_RayIntersection_get_origin(wrapped_type<py_ray_intersection> *self,void*) {
    try {
        return to_pyobject(self->get_base().origin);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_RayIntersection_get_normal(wrapped_type<py_ray_intersection> *self,void*) {
    try {
        return to_pyobject(self->get_base().normal);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_RayIntersection_getset[] = {
    {const_cast<char*>("dist"),OBJ_GETTER(wrapped_type<py_ray_intersection>,self->get_base().dist),NULL,NULL,NULL},
    {const_cast<char*>("origin"),reinterpret_cast<getter>(&obj_RayIntersection_get_origin),NULL,NULL,NULL},
    {const_cast<char*>("normal"),reinterpret_cast<getter>(&obj_RayIntersection_get_normal),NULL,NULL,NULL},
    {const_cast<char*>("primitive"),OBJ_GETTER(wrapped_type<py_ray_intersection>,self->get_base().p),NULL,NULL,NULL},
    {const_cast<char*>("batch_index"),OBJ_GETTER(wrapped_type<py_ray_intersection>,self->get_base().index),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject py_ray_intersection_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".RayIntersection",
    .tp_basicsize = sizeof(wrapped_type<py_ray_intersection>),
    .tp_dealloc = destructor_dealloc<wrapped_type<py_ray_intersection> >::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_getset = obj_RayIntersection_getset,
    .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        auto ptr = reinterpret_cast<wrapped_type<py_ray_intersection>*>(type->tp_alloc(type,0));
        if(ptr) {
            try {
                try {
                    auto [dist,origin,normal,primitive,batch_index] = get_arg::get_args("RayIntersection.__new__",args,kwds,
                        param<real>("dist"),
                        param<n_vector>("origin"),
                        param<n_vector>("normal"),
                        param("primitive"),
                        param<int>("batch_index",-1));

                    if(PyObject_TypeCheck(primitive,obj_Primitive::pytype())) {
                        if(batch_index != -1) THROW_PYERR_STRING(
                            ValueError,
                            "\"batch_index\" cannot be anything other than -1 unless \"primitive\" is an instance of PrimitiveBatch");
                    } else if(PyObject_TypeCheck(primitive,obj_PrimitiveBatch::pytype())) {
                        if(batch_index < 0) THROW_PYERR_STRING(
                            ValueError,
                            "\"batch_index\" cannot be less than zero if \"primitive\" is an instance of PrimitiveBatch");
                    } else {
                        THROW_PYERR_STRING(
                            TypeError,
                            "\"primitive\" must either be an instance of Primitive or PrimitiveBatch");
                    }

                    if(!compatible(origin,normal)) THROW_PYERR_STRING(TypeError,"\"origin\" and \"normal\" must have the same dimension");

                    new(&ptr->alloc_base()) py_ray_intersection(
                        dist,
                        origin,
                        normal,
                        primitive,
                        batch_index);
                } catch(...) {
                    Py_DECREF(ptr);
                    throw;
                }
            } PY_EXCEPT_HANDLERS(nullptr)
        }
        return py::ref(ptr);
    }};


PyObject *obj_Vector_str(wrapped_type<n_vector> *self) {
    try {
        auto &base = self->get_base();
        py::list converted{py::object(py::borrowed_ref(self))};
        for(int i=0; i<base.dimension(); ++i) converted[i] = py::str(converted[i]);
        py::object inner = py::check_new_ref(PyUnicode_Join(py::object(py::check_new_ref(PyUnicode_FromString(","))).ref(),converted.ref()));
        return PyUnicode_FromFormat("<%U>",inner.ref());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Vector_repr(wrapped_type<n_vector> *self) {
    try {
        auto &base = self->get_base();
        py::list lt{py::object(py::borrowed_ref(self))};
        return PyUnicode_FromFormat("Vector(%d,%R)",base.dimension(),lt.ref());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector_richcompare(wrapped_type<n_vector> *self,PyObject *arg,int op) {
    if(op == Py_EQ || op == Py_NE) {
        auto &base = self->get_base();
        auto b = get_base_if_is_type<n_vector>(arg);

        if(b) return to_pyobject((compatible(base,*b) && base == *b) == (op == Py_EQ));
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

FIX_STACK_ALIGN PyObject *obj_Vector___neg__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(-self->get_base());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector___abs__(wrapped_type<n_vector> *self) {
    try {
        return to_pyobject(self->get_base().absolute());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector___add__(PyObject *a,PyObject *b) {
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

FIX_STACK_ALIGN PyObject *obj_Vector___sub__(PyObject *a,PyObject *b) {
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

FIX_STACK_ALIGN PyObject *obj_Vector___mul__(PyObject *a,PyObject *b) {
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

FIX_STACK_ALIGN PyObject *obj_Vector___div__(PyObject *a,PyObject *b) {
    try {
        auto va = get_base_if_is_type<n_vector>(a);

        if(va && PyNumber_Check(b)) return to_pyobject(*va / from_pyobject<real>(b));
    } PY_EXCEPT_HANDLERS(nullptr)
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyNumberMethods obj_Vector_number_methods = {
    .nb_add = &obj_Vector___add__,
    .nb_subtract = &obj_Vector___sub__,
    .nb_multiply = &obj_Vector___mul__,
    .nb_negative = reinterpret_cast<unaryfunc>(&obj_Vector___neg__),
    .nb_absolute = reinterpret_cast<unaryfunc>(&obj_Vector___abs__),
    .nb_true_divide = &obj_Vector___div__
};

Py_ssize_t obj_Vector___sequence_len__(wrapped_type<n_vector> *self) {
    return self->get_base().dimension();
}

void v_index_check(const n_vector &v,Py_ssize_t index) {
    if(index < 0 || index >= v.dimension()) THROW_PYERR_STRING(IndexError,"vector index out of range");
}

FIX_STACK_ALIGN PyObject *obj_Vector___sequence_getitem__(wrapped_type<n_vector> *self,Py_ssize_t index) {
    try {
        auto &v = self->get_base();
        v_index_check(v,index);
        return to_pyobject(v[index]);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PySequenceMethods obj_Vector_sequence_methods = {
    .sq_length = reinterpret_cast<lenfunc>(&obj_Vector___sequence_len__),
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_Vector___sequence_getitem__)
};

FIX_STACK_ALIGN PyObject *obj_Vector_square(wrapped_type<n_vector> *self,PyObject *) {
    return to_pyobject(self->get_base().square());
}

FIX_STACK_ALIGN PyObject *obj_Vector_axis(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto [dim,axis,length] = get_arg::get_args("Vector.axis",args,kwds,
            param<int>("dimension"),
            param<int>("axis"),
            param<real>("length",1));

        check_dimension(dim);
        if(UNLIKELY(axis < 0 || axis >= dim)) {
            PyErr_SetString(PyExc_ValueError,"axis must be between 0 and dimension-1");
            return nullptr;
        }
        return to_pyobject(n_vector::axis(dim,axis,length));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector_absolute(wrapped_type<n_vector> *self,PyObject *) {
    return to_pyobject(self->get_base().absolute());
}

FIX_STACK_ALIGN PyObject *obj_Vector_unit(wrapped_type<n_vector> *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().unit());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector_apply(wrapped_type<n_vector> *_self,PyObject *_func) {
    try {
        auto &self = _self->get_base();

        n_vector r(self.dimension());
        py::object func = py::borrowed_ref(_func);

        for(int i=0; i<self.dimension(); ++i)
            r[i] = from_pyobject<real>(func(self[i]));

        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector_set_c(wrapped_type<n_vector> *self,PyObject *args,PyObject *kwds) {
    try {
        auto &v = self->get_base();

        auto [index,value] = get_arg::get_args("Vector.set_c",args,kwds,
            param<Py_ssize_t>("index"),
            param<real>("value"));

        v_index_check(v,index);

        n_vector r = v;
        r[index] = value;

        return to_pyobject(std::move(r));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Vector_reduce(wrapped_type<n_vector> *self,PyObject*) {
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

FIX_STACK_ALIGN PyObject *obj_Vector_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values",nullptr};
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
    {"dimension",OBJ_GETTER(wrapped_type<n_vector>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject vector_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Vector",
    .tp_basicsize = sizeof(wrapped_type<n_vector>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_vector>>::value,
    .tp_repr = reinterpret_cast<reprfunc>(&obj_Vector_repr),
    .tp_as_number = &obj_Vector_number_methods,
    .tp_as_sequence = &obj_Vector_sequence_methods,
    .tp_str = reinterpret_cast<reprfunc>(&obj_Vector_str),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_richcompare = reinterpret_cast<richcmpfunc>(&obj_Vector_richcompare),
    .tp_methods = obj_Vector_methods,
    .tp_getset = obj_Vector_getset,
    .tp_new = &obj_Vector_new};


FIX_STACK_ALIGN PyObject *obj_VectorBatch_getitem(wrapped_type<n_vector_batch> *self,Py_ssize_t index) {
    try {
        if(index < 0 || index >= static_cast<Py_ssize_t>(v_real::size)) {
            PyErr_SetString(PyExc_IndexError,"batch index out of range");
            return nullptr;
        }

        return to_pyobject(interleave1<module_store,v_real::size>(self->get_base(),index));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PySequenceMethods obj_VectorBatch_sequence_methods = {
    .sq_length = [](PyObject*){ return static_cast<Py_ssize_t>(v_real::size); },
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_VectorBatch_getitem)};

PyTypeObject vector_batch_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".VectorBatch",
    .tp_basicsize = sizeof(wrapped_type<n_vector_batch>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_vector_batch>>::value,
    .tp_as_sequence = &obj_VectorBatch_sequence_methods,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the VectorBatch type cannot be instantiated directly");
        return nullptr;
    }};


FIX_STACK_ALIGN PyObject *obj_Camera_get_origin(wrapped_type<n_camera> *self,void*) {
    try {
        return to_pyobject(self->get_base().origin);
    } PY_EXCEPT_HANDLERS(nullptr)
}
FIX_STACK_ALIGN int obj_Camera_set_origin(wrapped_type<n_camera> *self,PyObject *arg,void*) {
    try {
        setter_no_delete(arg);
        self->get_base().origin = from_pyobject<n_vector>(arg);
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
}

PyGetSetDef obj_Camera_getset[] = {
    {"origin",reinterpret_cast<getter>(&obj_Camera_get_origin),reinterpret_cast<setter>(obj_Camera_set_origin),NULL,NULL},
    {"axes",OBJ_GETTER(wrapped_type<n_camera>,py::ref(new obj_CameraAxes(self))),NULL,NULL,NULL},
    {"dimension",OBJ_GETTER(wrapped_type<n_camera>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

FIX_STACK_ALIGN PyObject *obj_Camera_normalize(wrapped_type<n_camera> *self,PyObject *) {
    try {
        self->get_base().normalize();
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Camera_translate(wrapped_type<n_camera> *self,PyObject *arg) {
    try {
        self->get_base().translate(from_pyobject<n_vector>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Camera_transform(wrapped_type<n_camera> *self,PyObject *arg) {
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

FIX_STACK_ALIGN PyObject *obj_Camera_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        auto [dimension] = get_arg::get_args("Camera.__new__",args,kwds,param<int>("dimension"));

        check_dimension(dimension);

        PyObject *ptr = py::check_obj(type->tp_alloc(type,0));

        new(&reinterpret_cast<wrapped_type<n_camera>*>(ptr)->alloc_base()) n_camera(dimension);
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN int obj_Camera_init(wrapped_type<n_camera> *self,PyObject *args,PyObject *kwds) {
    auto &base = self->get_base();

    for(int i=0; i<base.dimension()*base.dimension(); ++i) base.t_orientation.data()[i] = 0;
    for(int i=0; i<base.dimension(); ++i) base.t_orientation[i][i] = 1;

    return 0;
}

PyTypeObject camera_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Camera",
    .tp_basicsize = sizeof(wrapped_type<n_camera>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_camera>>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_Camera_methods,
    .tp_getset = obj_Camera_getset,
    .tp_init = reinterpret_cast<initproc>(&obj_Camera_init),
    .tp_new = &obj_Camera_new};


FIX_STACK_ALIGN PyObject *obj_Matrix_richcompare(wrapped_type<n_matrix> *self,PyObject *arg,int op) {
    if(op == Py_EQ || op == Py_NE) {
        auto &base = self->get_base();
        auto b = get_base_if_is_type<n_matrix>(arg);

        if(b) return to_pyobject((compatible(base,*b) && base == *b) == (op == Py_EQ));
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

FIX_STACK_ALIGN PyObject *obj_Matrix___mul__(PyObject *a,PyObject *b) {
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
    .nb_multiply = &obj_Matrix___mul__
};

Py_ssize_t obj_Matrix___sequence_len__(wrapped_type<n_matrix> *self) {
    return self->get_base().dimension();
}

FIX_STACK_ALIGN PyObject *obj_Matrix___sequence_getitem__(wrapped_type<n_matrix> *self,Py_ssize_t index) {
    try {
        auto &base = self->get_base();

        if(UNLIKELY(index < 0 || index >= base.dimension())) {
            PyErr_SetString(PyExc_IndexError,"matrix row index out of range");
            return nullptr;
        }
        return py::ref(new obj_MatrixProxy(py::ref(self),base.dimension(),base[index]));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PySequenceMethods obj_Matrix_sequence_methods = {
    .sq_length = reinterpret_cast<lenfunc>(&obj_Matrix___sequence_len__),
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_Matrix___sequence_getitem__)
};

FIX_STACK_ALIGN PyObject *obj_Matrix_reflection(PyObject*,PyObject *arg) {
    try {
        return to_pyobject(n_matrix::reflection(from_pyobject<n_vector>(arg)));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_scale(PyObject*,PyObject *args) {
    try {
        if(PyTuple_GET_SIZE(args) == 1) {
            if(PyObject_TypeCheck(PyTuple_GET_ITEM(args,0),wrapped_type<n_vector>::pytype())) {
                return to_pyobject(n_matrix::scale(
                    reinterpret_cast<wrapped_type<n_vector>*>(PyTuple_GET_ITEM(args,0))->get_base()));
            }
        } else if(PyTuple_GET_SIZE(args) == 2
                && PyLong_Check(PyTuple_GET_ITEM(args,0))
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

FIX_STACK_ALIGN PyObject *obj_Matrix_rotation(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto [a,b,angle] = get_arg::get_args("Matrix.rotation",args,kwds,
            param<n_vector>("a"),
            param<n_vector>("b"),
            param<real>("angle"));

        if(UNLIKELY(!compatible(a,b))) {
            PyErr_SetString(PyExc_TypeError,"cannot produce rotation matrix using vectors of different dimension");
            return nullptr;
        }

        return to_pyobject(n_matrix::rotation(a,b,angle));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_identity(PyObject*,PyObject *arg) {
    try {
        int dimension = from_pyobject<int>(arg);
        check_dimension(dimension);
        return to_pyobject(n_matrix::identity(dimension));
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_determinant(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().determinant());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_inverse(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().inverse());
    } catch(std::domain_error &e) {
        PyErr_SetString(PyExc_ValueError,e.what());
        return nullptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_transpose(wrapped_type<n_matrix> *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().transpose());
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_Matrix_reduce(wrapped_type<n_matrix> *self,PyObject*) {
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

FIX_STACK_ALIGN PyObject *obj_Matrix_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values",nullptr};
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
        return py::ref(
            new obj_MatrixProxy(py::ref(self),base.dimension() * base.dimension(),base.data()));
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Matrix_getset[] = {
    {"values",reinterpret_cast<getter>(&obj_Matrix_values),NULL,NULL,NULL},
    {"dimension",OBJ_GETTER(wrapped_type<n_matrix>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject matrix_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Matrix",
    .tp_basicsize = sizeof(wrapped_type<n_matrix>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_matrix>>::value,
    .tp_as_number = &obj_Matrix_number_methods,
    .tp_as_sequence = &obj_Matrix_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_richcompare = reinterpret_cast<richcmpfunc>(&obj_Matrix_richcompare),
    .tp_methods = obj_Matrix_methods,
    .tp_getset = obj_Matrix_getset,
    .tp_new = &obj_Matrix_new};


FIX_STACK_ALIGN PyObject *aabb_split(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds,bool right) {
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

    return py::ref(r);
}

PyObject *obj_AABB_left(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,false);
}

PyObject *obj_AABB_right(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,true);
}

template<typename T,typename F> PyObject *try_intersects(const n_aabb &base,PyObject *obj,F f) {
    if(PyObject_TypeCheck(obj,wrapped_type<T>::pytype())) {
        auto p = reinterpret_cast<wrapped_type<T>*>(obj);

        if(base.dimension() != p->get_base().dimension())
            THROW_PYERR_STRING(TypeError,"cannot perform intersection test on object with different dimension");

        return to_pyobject(f(base,p->get_base()));
    }
    return nullptr;
}

struct intersects_callback_t {
    template<typename T> bool operator()(const n_aabb &base,const T &p) {
        return base.intersects(p);
    }
};

void set_primitive_instead_of_proto_error() {
    PyErr_SetString(PyExc_TypeError,
        "Instances of Primitive cannot be used directly. Use PrimitivePrototype instead.");
}

FIX_STACK_ALIGN PyObject *obj_AABB_intersects(wrapped_type<n_aabb> *self,PyObject *obj) {
    try {
        auto &base = self->get_base();
        intersects_callback_t callback;

        auto r = try_intersects<n_triangle_prototype>(base,obj,callback);
        if(r) return r;
        r = try_intersects<n_triangle_batch_prototype>(base,obj,callback);
        if(r) return r;
        r = try_intersects<n_solid_prototype>(base,obj,callback);
        if(r) return r;

        if(PyObject_TypeCheck(obj,obj_Primitive::pytype()))
            set_primitive_instead_of_proto_error();
        else
            PyErr_SetString(PyExc_TypeError,
                "object must be an instance of " MODULE_STR ".PrimitivePrototype");

        return nullptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

struct intersects_flat_callback_t {
    int skip;

    template<typename T> bool operator()(const n_aabb &base,const T &p) {
        return base.intersects_flat(p,skip);
    }
};

FIX_STACK_ALIGN PyObject *obj_AABB_intersects_flat(wrapped_type<n_aabb> *self,PyObject *args,PyObject *kwds) {
    try {
        auto vals = get_arg::get_args("AABB.intersects_flat",args,kwds,
            param("primitive"),
            param<int>("skip"));

        auto &base = self->get_base();
        intersects_flat_callback_t callback{std::get<1>(vals)};

        auto r = try_intersects<n_triangle_prototype>(base,std::get<0>(vals),callback);
        if(r) return r;
        r = try_intersects<n_triangle_batch_prototype>(base,std::get<0>(vals),callback);
        if(r) return r;

        if(PyObject_TypeCheck(std::get<0>(vals),obj_Primitive::pytype()))
            set_primitive_instead_of_proto_error();
        else
            PyErr_SetString(PyExc_TypeError,
                "object must be an instance of " MODULE_STR ".TrianglePrototype or " MODULE_STR ".TriangleBatchPrototype");

        return nullptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_AABB_methods[] = {
    {"left",reinterpret_cast<PyCFunction>(&obj_AABB_left),METH_VARARGS|METH_KEYWORDS,NULL},
    {"right",reinterpret_cast<PyCFunction>(&obj_AABB_right),METH_VARARGS|METH_KEYWORDS,NULL},
    {"intersects",reinterpret_cast<PyCFunction>(&obj_AABB_intersects),METH_O,NULL},
    {"intersects_flat",reinterpret_cast<PyCFunction>(&obj_AABB_intersects_flat),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

FIX_STACK_ALIGN PyObject *obj_AABB_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;

    try {
        try {
            auto vals = get_arg::get_args("AABB.__new__",args,kwds,
                param<int>("dimension"),
                param<PyObject*>("start",nullptr),
                param<PyObject*>("end",nullptr));

            check_dimension(std::get<0>(vals));

            auto &base = reinterpret_cast<wrapped_type<n_aabb>*>(ptr)->alloc_base();

            if(std::get<1>(vals)) {
                auto start = from_pyobject<n_vector>(std::get<1>(vals));
                if(std::get<0>(vals) != start.dimension())
                    THROW_PYERR_STRING(TypeError,"\"start\" has a dimension different from \"dimension\"");
                new(&base.start) n_vector(start);
            } else new(&base.start) n_vector(std::get<0>(vals),std::numeric_limits<real>::lowest());


            if(std::get<2>(vals)) {
                auto end = from_pyobject<n_vector>(std::get<2>(vals));
                if(std::get<0>(vals) != end.dimension())
                    THROW_PYERR_STRING(TypeError,"\"end\" has a dimension different from \"dimension\"");
                new(&base.end) n_vector(end);
            } else new(&base.end) n_vector(std::get<0>(vals),std::numeric_limits<real>::max());

        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)

    return ptr;
}

FIX_STACK_ALIGN PyObject *obj_AABB_get_start(wrapped_type<n_aabb> *self,void*) {
    try {
        return to_pyobject(self->get_base().start);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_AABB_get_end(wrapped_type<n_aabb> *self,void*) {
    try {
        return to_pyobject(self->get_base().end);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_AABB_getset[] = {
    {"dimension",OBJ_GETTER(wrapped_type<n_aabb>,self->get_base().dimension()),NULL,NULL,NULL},
    {"start",reinterpret_cast<getter>(&obj_AABB_get_start),NULL,NULL,NULL},
    {"end",reinterpret_cast<getter>(&obj_AABB_get_end),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject aabb_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".AABB",
    .tp_basicsize = sizeof(wrapped_type<n_aabb>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_aabb>>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_AABB_methods,
    .tp_getset = obj_AABB_getset,
    .tp_new = &obj_AABB_new};


PyTypeObject obj_PrimitivePrototype::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".PrimitivePrototype",
    .tp_basicsize = sizeof(obj_PrimitivePrototype),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the PrimitivePrototype type cannot be instantiated directly");
        return nullptr;
    }};


FIX_STACK_ALIGN PyObject *obj_TrianglePrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"points","material",nullptr};
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

            new(&base.p) py::pyptr<obj_Primitive>(py::new_ref(obj_Triangle::from_points(points.data(),m)));
            new(&base.first_edge_normal) n_vector(dim,0);
            new(&base.items()[0]) triangle_point<module_store,real>(points[0],base.first_edge_normal);

            for(int i=1; i<dim; ++i) {
                new(&base.items()[i]) triangle_point<module_store,real>(points[i],base.pt()->items()[i-1]);
                base.first_edge_normal -= base.pt()->items()[i-1];
            }

            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_TrianglePrototype_get_face_normal(obj_TrianglePrototype *self,void*) {
    try {
        return to_pyobject(self->get_base().pt()->face_normal);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_TrianglePrototype_getset[] = {
    {"dimension",OBJ_GETTER(obj_TrianglePrototype,self->get_base().dimension()),NULL,NULL,NULL},
    {"face_normal",reinterpret_cast<getter>(&obj_TrianglePrototype_get_face_normal),NULL,NULL,NULL},
    {"point_data",OBJ_GETTER(
        obj_TrianglePrototype,
        py::ref(new obj_TrianglePointData(obj_self,self->get_base().dimension(),self->get_base().items()))),NULL,NULL,NULL},
    {"boundary",OBJ_GETTER(
        obj_TrianglePrototype,
        py::new_ref(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary))),NULL,NULL,NULL},
    {"material",OBJ_GETTER(obj_TrianglePrototype,self->get_base().pt()->m),NULL,NULL,NULL},
    {"primitive",OBJ_GETTER(obj_TrianglePrototype,self->get_base().p),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject triangle_prototype_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".TrianglePrototype",
    .tp_basicsize = obj_TrianglePrototype::base_size,
    .tp_itemsize = obj_TrianglePrototype::item_size,
    .tp_dealloc = destructor_dealloc<obj_TrianglePrototype>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_getset = obj_TrianglePrototype_getset,
    .tp_base = obj_PrimitivePrototype::pytype(),
    .tp_new = &obj_TrianglePrototype_new};


FIX_STACK_ALIGN PyObject *obj_TriangleBatchPrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        auto arg = std::get<0>(get_arg::get_args("TriangleBatchPrototype.__new__",args,kwds,
            param("t_prototypes")));

        py::pyptr<obj_TrianglePrototype> vals[v_real::size];
        sized_iter itr{py::borrowed_ref(arg),v_real::size};
        for(size_t i=0; i<v_real::size; ++i) {
            auto val = itr.next();
            if(!PyObject_TypeCheck(val.ref(),obj_TrianglePrototype::pytype())) {
                PyErr_SetString(PyExc_TypeError,"items must be instances of TrianglePrototype");
                return nullptr;
            }
            vals[i] = py::pyptr<obj_TrianglePrototype>(val);
        }
        itr.finished();

        int dimension = vals[0]->get_base().dimension();
        for(size_t i=1; i<v_real::size; ++i) {
            if(vals[i]->get_base().dimension() != dimension) {
                PyErr_SetString(PyExc_TypeError,"the items must all have the same dimension");
                return nullptr;
            }
        }

        auto ptr = py::check_obj(type->tp_alloc(type,obj_TriangleBatchPrototype::item_size ? dimension : 0));
        try {
            new(&reinterpret_cast<obj_TriangleBatchPrototype*>(ptr)->alloc_base(dimension))
                n_triangle_batch_prototype(dimension,[&](int i){ return &vals[i]->get_base(); });
            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_TriangleBatchPrototype_get_face_normal(obj_TriangleBatchPrototype *self,void*) {
    try {
        return to_pyobject(self->get_base().pt()->face_normal);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_TriangleBatchPrototype_getset[] = {
    {"dimension",OBJ_GETTER(obj_TriangleBatchPrototype,self->get_base().dimension()),NULL,NULL,NULL},
    {"face_normal",reinterpret_cast<getter>(&obj_TriangleBatchPrototype_get_face_normal),NULL,NULL,NULL},
    {"point_data",OBJ_GETTER(
        obj_TriangleBatchPrototype,
        py::ref(new obj_TriangleBatchPointData(obj_self,self->get_base().dimension(),self->get_base().items()))),NULL,NULL,NULL},
    {"boundary",OBJ_GETTER(
        obj_TriangleBatchPrototype,
        py::new_ref(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary))),NULL,NULL,NULL},
    {"material",OBJ_GETTER(
        obj_TriangleBatchPrototype,
        py::tuple(self->get_base().pt()->m,self->get_base().pt()->m+v_real::size).new_ref()),NULL,NULL,NULL},
    {"primitive",OBJ_GETTER(obj_TriangleBatchPrototype,self->get_base().p),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject triangle_batch_prototype_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".TriangleBatchPrototype",
    .tp_basicsize = obj_TriangleBatchPrototype::base_size,
    .tp_itemsize = obj_TriangleBatchPrototype::item_size,
    .tp_dealloc = destructor_dealloc<obj_TriangleBatchPrototype>::value,
    .tp_getset = obj_TriangleBatchPrototype_getset,
    .tp_base = obj_PrimitivePrototype::pytype(),
    .tp_new = &obj_TriangleBatchPrototype_new};


template<typename T> struct obj_TrianglePointDatum_strings {};

template<> struct obj_TrianglePointDatum_strings<real> {
    static constexpr const char *mod_name = FULL_MODULE_STR ".TrianglePointDatum";
    static PyObject *tp_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
        PyErr_SetString(PyExc_TypeError,"The TrianglePointDatum type cannot be instantiated directly");
        return nullptr;
    }
};

template<> struct obj_TrianglePointDatum_strings<v_real> {
    static constexpr const char *mod_name = FULL_MODULE_STR ".TriangleBatchPointDatum";
    static PyObject *tp_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
        PyErr_SetString(PyExc_TypeError,"The TriangleBatchPointDatum type cannot be instantiated directly");
        return nullptr;
    }
};

template<typename T> FIX_STACK_ALIGN PyObject *obj_TrianglePointDatum_get_point(wrapped_type<n_detatched_triangle_point<T> > *self,void*) {
    try {
        return to_pyobject(self->get_base().point);
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> FIX_STACK_ALIGN PyObject *obj_TrianglePointDatum_get_edge_normal(wrapped_type<n_detatched_triangle_point<T> > *self,void*) {
    try {
        return to_pyobject(self->get_base().edge_normal);
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> PyGetSetDef detatched_triangle_point_obj_base<T>::getset[] = {
    {const_cast<char*>("point"),reinterpret_cast<getter>(&obj_TrianglePointDatum_get_point<T>),NULL,NULL,NULL},
    {const_cast<char*>("edge_normal"),reinterpret_cast<getter>(&obj_TrianglePointDatum_get_edge_normal<T>),NULL,NULL,NULL},
    {NULL}
};

template<typename T> PyTypeObject detatched_triangle_point_obj_base<T>::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = obj_TrianglePointDatum_strings<real>::mod_name,
    .tp_basicsize = sizeof(wrapped_type<n_detatched_triangle_point<T> >),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_detatched_triangle_point<T> > >::value,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_getset = detatched_triangle_point_obj_base<T>::getset,
    .tp_new = &obj_TrianglePointDatum_strings<real>::tp_new};


FIX_STACK_ALIGN PyObject *obj_SolidPrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;

    try {
        try {
            const char *names[] = {"type","position","orientation","material",nullptr};
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

FIX_STACK_ALIGN PyObject *obj_SolidPrototype_get_orientation(wrapped_type<n_solid_prototype> *self,void*) {
    try {
        return to_pyobject(self->get_base().ps()->orientation);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_SolidPrototype_get_inv_orientation(wrapped_type<n_solid_prototype> *self,void*) {
    try {
        return to_pyobject(self->get_base().ps()->inv_orientation);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_SolidPrototype_get_position(wrapped_type<n_solid_prototype> *self,void*) {
    try {
        return to_pyobject(self->get_base().ps()->position);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_SolidPrototype_getset[] = {
    {"dimension",OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().dimension()),NULL,NULL,NULL},
    {"type",OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->type),NULL,NULL,NULL},
    {"orientation",reinterpret_cast<getter>(&obj_SolidPrototype_get_orientation),NULL,NULL,NULL},
    {"inv_orientation",reinterpret_cast<getter>(&obj_SolidPrototype_get_inv_orientation),NULL,NULL,NULL},
    {"position",reinterpret_cast<getter>(&obj_SolidPrototype_get_position),NULL,NULL,NULL},
    {"material",OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()->m),NULL,NULL,NULL},
    {"boundary",OBJ_GETTER(
        wrapped_type<n_solid_prototype>,
        py::new_ref(new wrapped_type<n_aabb>(obj_self,self->get_base().boundary))),NULL,NULL,NULL},
    {"primitive",OBJ_GETTER(wrapped_type<n_solid_prototype>,self->get_base().ps()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject solid_prototype_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".SolidPrototype",
    .tp_basicsize = sizeof(wrapped_type<n_solid_prototype>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_solid_prototype>>::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_getset = obj_SolidPrototype_getset,
    .tp_base = obj_PrimitivePrototype::pytype(),
    .tp_new = &obj_SolidPrototype_new};


FIX_STACK_ALIGN PyObject *obj_PointLight_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;

    try {
        try {
            const char *names[] = {"position","color",nullptr};
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

FIX_STACK_ALIGN PyObject *obj_PointLight_get_position(wrapped_type<n_point_light> *self,void*) {
    try {
        return to_pyobject(self->get_base().position);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_PointLight_get_color(wrapped_type<n_point_light> *self,void*) {
    try {
        return to_pyobject(self->get_base().c);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_PointLight_getset[] = {
    {"position",reinterpret_cast<getter>(&obj_PointLight_get_position),NULL,NULL,NULL},
    {"color",reinterpret_cast<getter>(&obj_PointLight_get_color),NULL,NULL,NULL},
    {"dimension",OBJ_GETTER(wrapped_type<n_point_light>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject point_light_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".PointLight",
    .tp_basicsize = sizeof(wrapped_type<n_point_light>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_point_light>>::value,
//    .tp_repr = &obj_PointLight_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_getset = obj_PointLight_getset,
    .tp_new = &obj_PointLight_new};


FIX_STACK_ALIGN PyObject *obj_GlobalLight_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = type->tp_alloc(type,0);
    if(!ptr) return nullptr;

    try {
        try {
            auto [direction,color] = get_arg::get_args("GlobalLight.__new__",args,kwds,
                param<n_vector>("direction"),
                param("color"));

            auto &base = reinterpret_cast<wrapped_type<n_global_light>*>(ptr)->alloc_base();

            new(&base.direction) n_vector(direction);
            read_color(base.c,color,"color");

            return ptr;
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_GlobalLight_get_direction(wrapped_type<n_global_light> *self,void*) {
    try {
        return to_pyobject(self->get_base().direction);
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_GlobalLight_get_color(wrapped_type<n_global_light> *self,void*) {
    try {
        return to_pyobject(self->get_base().c);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_GlobalLight_getset[] = {
    {"direction",reinterpret_cast<getter>(&obj_GlobalLight_get_direction),NULL,NULL,NULL},
    {"color",reinterpret_cast<getter>(&obj_GlobalLight_get_color),NULL,NULL,NULL},
    {"dimension",OBJ_GETTER(wrapped_type<n_global_light>,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject global_light_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".GlobalLight",
    .tp_basicsize = sizeof(wrapped_type<n_global_light>),
    .tp_dealloc = destructor_dealloc<wrapped_type<n_global_light>>::value,
//    .tp_repr = &obj_GlobalLight_repr,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_getset = obj_GlobalLight_getset,
    .tp_new = &obj_GlobalLight_new};


template<typename T> void check_index(const cs_light_list<T> *ll,Py_ssize_t index) {
    if(index < 0 || size_t(index) >= T::value(ll->parent.get()).size()) THROW_PYERR_STRING(IndexError,"index out of range");
}

template<typename T> Py_ssize_t cs_light_list_len(cs_light_list<T> *self) {
    return static_cast<Py_ssize_t>(T::value(self->parent.get()).size());
}

template<typename T> FIX_STACK_ALIGN PyObject *cs_light_list_getitem(cs_light_list<T> *self,Py_ssize_t index) {
    try {
        check_index(self,index);
        return to_pyobject(T::value(self->parent.get())[index]);
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> FIX_STACK_ALIGN int cs_light_list_setitem(cs_light_list<T> *self,Py_ssize_t index,PyObject *value) {
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

template<typename T> PySequenceMethods cs_light_list<T>::sequence_methods = {
    .sq_length = reinterpret_cast<lenfunc>(&cs_light_list_len<T>),
    .sq_item = reinterpret_cast<ssizeargfunc>(&cs_light_list_getitem<T>),
    .sq_ass_item = reinterpret_cast<ssizeobjargproc>(&cs_light_list_setitem<T>)
};

template<typename T> FIX_STACK_ALIGN PyObject *cs_light_list_append(cs_light_list<T> *self,PyObject *arg) {
    try {
        ensure_unlocked(self->parent);
        T::value(self->parent.get()).push_back(light_compat_check(self->parent->cast_base(),get_base<typename T::item_t>(arg)));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

template<typename T> FIX_STACK_ALIGN PyObject *cs_light_list_extend(cs_light_list<T> *self,PyObject *arg) {
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

template<typename T> PyMethodDef cs_light_list<T>::methods[] = {
    {"append",reinterpret_cast<PyCFunction>(&cs_light_list_append<T>),METH_O,NULL},
    {"extend",reinterpret_cast<PyCFunction>(&cs_light_list_extend<T>),METH_O,NULL},
    {NULL}
};

template<typename T> PyTypeObject cs_light_list<T>::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = T::name,
    .tp_basicsize = sizeof(cs_light_list<T>),
    .tp_dealloc = destructor_dealloc<cs_light_list<T>>::value,
    .tp_as_sequence = &cs_light_list<T>::sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_methods = cs_light_list<T>::methods,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_Format(PyExc_TypeError,"the %s type cannot be instantiated directly",T::name + sizeof(FULL_MODULE_STR));
        return nullptr;
    }};


FIX_STACK_ALIGN PyObject *obj_dot(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto [a,b] = get_arg::get_args("dot",args,kwds,
            param<n_vector>("a"),
            param<n_vector>("b"));

        if(UNLIKELY(!compatible(a,b))) {
            PyErr_SetString(PyExc_TypeError,"cannot perform dot product on vectors of different dimension");
            return nullptr;
        }
        return to_pyobject(dot(a,b));

    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_cross(PyObject*,PyObject *arg) {
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

obj_PrimitivePrototype *p_proto_cast(PyObject *obj) {
    auto p = checked_py_cast<obj_PrimitivePrototype>(obj);

    if(v_real::size == 1 && Py_TYPE(obj) == obj_TriangleBatchPrototype::pytype())
        THROW_PYERR_STRING(TypeError,"instances of TriangleBatchPrototype cannot be used to construct a k-d tree when BATCH_SIZE is 1");
    return p;
}

std::tuple<n_aabb,kd_node<module_store>*> build_kdtree(const char *func,PyObject *args,PyObject *kwds) {
    const char *names[] = {"primitives","extra_threads","max_depth","split_threshold","traversal_cost","intersection_cost",nullptr};

    get_arg ga{args,kwds,names,func};

    auto p_iterable = ga(true);

    auto tmp = ga(false);
    int extra_threads = -1;
    if(tmp) extra_threads = from_pyobject<int>(tmp);

    auto max_depth = ga(get_arg::KEYWORD_ONLY);
    auto split_threshold = ga(get_arg::KEYWORD_ONLY);
    auto traversal = ga(get_arg::KEYWORD_ONLY);
    auto intersection = ga(get_arg::KEYWORD_ONLY);

    ga.finished();


    auto p_objs = collect<py::object>(p_iterable);
    if(UNLIKELY(p_objs.empty())) THROW_PYERR_STRING(ValueError,"cannot build tree from empty sequence");

    proto_array<module_store> primitives;
    primitives.reserve(p_objs.size());

    primitives.push_back(&p_proto_cast(p_objs[0].ref())->get_base());

    int dimension = primitives[0]->dimension();

    kd_tree_params kd_params(dimension);

    if(max_depth) {
        kd_params.max_depth = from_pyobject<int>(max_depth);
        if(kd_params.max_depth < 0) THROW_PYERR_STRING(ValueError,"max_depth cannot be less than 0");
    }

    if(split_threshold) {
        kd_params.split_threshold = from_pyobject<int>(split_threshold);
        if(kd_params.split_threshold < 1) THROW_PYERR_STRING(ValueError,"split_threshold cannot be less than 1");
    }

    if(traversal) kd_params.traversal = from_pyobject<real>(traversal);
    if(intersection) kd_params.intersection = from_pyobject<real>(intersection);

    for(size_t i=1; i<p_objs.size(); ++i) {
        auto p = &p_proto_cast(p_objs[i].ref())->get_base();
        if(UNLIKELY(p->dimension() != dimension)) THROW_PYERR_STRING(TypeError,"the primitive prototypes must all have the same dimension");
        primitives.push_back(p);
    }

    return build_kdtree<module_store>(primitives,extra_threads,kd_params);
}

FIX_STACK_ALIGN PyObject *obj_build_kdtree(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto [boundary,root] = build_kdtree("build_kdtree",args,kwds);
        py::object pyroot{py::new_ref(new_obj_node(nullptr,root,boundary.dimension()))};
        return py::make_tuple(boundary.start,boundary.end,pyroot).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

FIX_STACK_ALIGN PyObject *obj_build_composite_scene(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        auto [boundary,root] = build_kdtree("build_composite_scene",args,kwds);
        return py::ref(new obj_CompositeScene(boundary,root));
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
    return {py::new_ref(obj),obj->cast_base().data()};
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
        r.obj = py::new_ref(tri);

        r.data[0] = tri->p1.data();
        r.data[1] = tri->face_normal.data();
        for(int i=0; i<dimension-1; ++i) r.data[i+2] = tri->items()[i].data();

        return r;
    },
    [](PyObject *tri) { reinterpret_cast<obj_Triangle*>(tri)->recalculate_d(); },
    [](int dimension,int type,material *mat) -> wrapped_solid {
        wrapped_solid r;

        auto s = new obj_Solid(dimension,static_cast<solid_type>(type),mat);
        r.obj = py::new_ref(s);
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
    obj_PrimitiveBatch::pytype(),
    obj_Solid::pytype(),
    obj_Triangle::pytype(),
    obj_TriangleBatch::pytype(),
    obj_FrozenVectorView::pytype(),
    obj_KDNode::pytype(),
    obj_KDLeaf::pytype(),
    obj_KDBranch::pytype(),
    wrapped_type<py_ray_intersection>::pytype(),
    wrapped_type<n_vector>::pytype(),
    wrapped_type<n_vector_batch>::pytype(),
    obj_MatrixProxy::pytype(),
    wrapped_type<n_camera>::pytype(),
    obj_CameraAxes::pytype(),
    wrapped_type<n_matrix>::pytype(),
    wrapped_type<n_aabb>::pytype(),
    obj_PrimitivePrototype::pytype(),
    obj_TrianglePrototype::pytype(),
    wrapped_type<n_detatched_triangle_point<real> >::pytype(),
    obj_TrianglePointData::pytype(),
    obj_TriangleBatchPrototype::pytype(),
    wrapped_type<n_detatched_triangle_point<v_real> >::pytype(),
    obj_TriangleBatchPointData::pytype(),
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

extern "C" FIX_STACK_ALIGN SHARED(PyObject) * APPEND_MODULE_NAME(PyInit_)() {
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
            if(!pdata) return nullptr;
            package_common_data = *pdata;
        } PY_EXCEPT_HANDLERS(nullptr)
    }

    for(auto cls : classes) {
        if(UNLIKELY(PyType_Ready(cls) < 0)) return nullptr;
    }

    PyObject *m = PyModule_Create(&module_def);

    if(UNLIKELY(!m)) return nullptr;

    for(auto cls : classes) {
        add_class(m,cls->tp_name + sizeof(FULL_MODULE_STR),cls);
    }

    PyObject *c = PyCapsule_New(&module_constructors,"_CONSTRUCTORS",nullptr);
    if(UNLIKELY(!c)) return nullptr;
    PyModule_AddObject(m,"_CONSTRUCTORS",c);

    PyModule_AddIntConstant(m,"BATCH_SIZE",v_real::size);

    return m;
}
