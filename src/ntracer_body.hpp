
#include <Python.h>
#include <assert.h>

#include "ntracer.hpp"
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


typedef repr::py_vector_t n_vector;
typedef repr::py_matrix_t n_matrix;
typedef repr::camera_t n_camera;


template<typename T> PyObject *to_pyobject(const ::impl::vector_expr<T> &e) {
    return to_pyobject(n_vector(e));
}

Py_ssize_t check_dimension(int d) {
    if(d < 3) {
        PyErr_Format(
            PyExc_ValueError,
            "dimension cannot be less than 3");
        throw py_error_set();
    }
    
    if(repr::required_d && d != repr::required_d) {
        assert(repr::required_d >= 3);
        PyErr_Format(
            PyExc_ValueError,
            "this class only supports a dimension of %d",
            repr::required_d);
        throw py_error_set();
    }
    
    return repr::required_d ? 0 : d;
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

template<typename T> void fill_vector(std::vector<T> &v,Py_ssize_t size,PyObject **items) {
    v.reserve(size);
    for(Py_ssize_t i=0; i<size; ++i) v.push_back(from_pyobject<T>(items[i]));
}

template<typename T> std::vector<T> collect(PyObject *src) {
    std::vector<T> items;
    
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



template<> struct wrapped_type<n_vector> {
    typedef repr::vector_obj type;
};
template<> struct wrapped_type<n_matrix> {
    typedef repr::matrix_obj type;
};
template<> struct wrapped_type<n_camera> {
    typedef repr::camera_obj type;
};


struct obj_BoxScene {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    BoxScene<repr> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    obj_BoxScene(int d) : base(d), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    obj_BoxScene(BoxScene<repr> const &b) : base(b), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    ~obj_BoxScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    BoxScene<repr> &cast_base() { return base; }
    BoxScene<repr> &get_base() { return base; }
};

template<> struct wrapped_type<BoxScene<repr> > {
    typedef obj_BoxScene type;
};


constexpr char matrix_proxy_name[] = MODULE_STR ".MatrixProxy";
typedef py::obj_array_adapter<real,matrix_proxy_name,false,true> obj_MatrixProxy;

constexpr char camera_axes_name[] = MODULE_STR ".CameraAxes";
typedef py::obj_array_adapter<n_vector,camera_axes_name,true> obj_CameraAxes;


struct obj_CompositeScene {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    composite_scene<repr> base;
    PyObject *idict;
    PyObject *weaklist;
    PY_MEM_GC_NEW_DELETE

    ~obj_CompositeScene() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    composite_scene<repr> &cast_base() { return base; }
    composite_scene<repr> &get_base() { return base; }
};

template<> struct wrapped_type<composite_scene<repr> > {
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


/* The following wrappers store their associated data in a special way. In large
   scenes, there will be many KD-tree nodes which need to be traversed as
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
    kd_node<repr> *_data;
    
    int dimension() const;
    
protected:
    obj_KDNode(py::nullable_object parent,kd_node<repr> *data) : parent(parent), _data(data) {}
    ~obj_KDNode() = default;
};

struct obj_KDBranch : obj_KDNode {
    static PyTypeObject pytype;
    
    int dimension;
    
    kd_branch<repr> *&data() {
        assert(_data);
        return reinterpret_cast<kd_branch<repr>*&>(_data);
    }
    
    kd_branch<repr> *data() const {
        assert(_data);
        return static_cast<kd_branch<repr>*>(_data);
    }
    
    obj_KDBranch(py::nullable_object parent,kd_branch<repr> *data,int dimension) : obj_KDNode(parent,data), dimension(dimension) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    ~obj_KDBranch() {
        if(!parent) delete data();
    }
};

struct obj_KDLeaf : obj_KDNode {
    static PyTypeObject pytype;
    
    kd_leaf<repr> *&data() {
        assert(_data);
        return reinterpret_cast<kd_leaf<repr>*&>(_data);
    }
    
    kd_leaf<repr> *data() const {
        assert(_data);
        return static_cast<kd_leaf<repr>*>(_data);
    }
    
    obj_KDLeaf(py::nullable_object parent,kd_leaf<repr> *data) : obj_KDNode(parent,data) {
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


struct obj_AABB {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    PY_MEM_NEW_DELETE

    obj_AABB(const n_vector &start,const n_vector &end) : start(start), end(end) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    
    int dimension() const { return start.dimension(); }

    n_vector start;
    n_vector end;
};


struct obj_PrimitivePrototype {
    static PyTypeObject pytype;
    PyObject_HEAD
};

struct triangle_point {
    n_vector point;
    n_vector edge_normal;
    
    triangle_point(const n_vector &point,const n_vector &edge_normal) : point(point), edge_normal(edge_normal) {}
};

struct obj_TrianglePrototype : repr::flexible_obj<obj_TrianglePrototype,triangle_point,repr::required_d> {
    static PyTypeObject pytype;
    
    n_vector face_normal;
    n_vector aabb_max;
    n_vector aabb_min;
    
    int dimension() const {
        return face_normal.dimension();
    }
};

struct obj_TrianglePointDatum {
    static PyTypeObject pytype;
    PyObject_HEAD
    PY_MEM_NEW_DELETE

    obj_TrianglePointDatum(const triangle_point &data) : data(data) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }

    triangle_point data;
};

PyObject *to_pyobject(const triangle_point &x) {
    return reinterpret_cast<PyObject*>(new obj_TrianglePointDatum(x));
}

constexpr char triangle_point_data_name[] = MODULE_STR ".TrianglePointData";
typedef py::obj_array_adapter<triangle_point,triangle_point_data_name,false,true> obj_TrianglePointData;


struct obj_SolidPrototype {
    static PyTypeObject pytype;
    PyObject_HEAD
        
    py::pyptr<solid<repr> > p;
    
    n_vector aabb_max;
    n_vector aabb_min;
    
    int dimension() const {
        return aabb_max.dimension();
    }
};


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
    /* we can get away with not traversing the BoxScene object's camera because
       it is never directly exposed to Python code */
    Py_VISIT(self->idict);
    return 0;
}


int obj_BoxScene_clear(obj_BoxScene *self) {
    /* we can get away with not clearing the BoxScene object's camera because
       it is never directly exposed to Python code */
    Py_CLEAR(self->idict);
    return 0;
}

void copy_camera(const n_camera &source,n_camera &dest) {
    assert(source.dimension() == dest.dimension());
    
    /* we only need a shallow copy since vectors are immutable in Python code
       and BoxScene doesn't modify its camera */
    dest.origin() = source.origin();
    for(int i=0; i<source.dimension(); ++i) dest.axes()[i] = source.axes()[i];
}

const char *set_camera_doc = "\
set_camera(camera)\n\
\n\
Attempt to set the scene's camera to a copy of the provided value.\n\
\n\
If the scene has been locked by a Renderer, this function will raise an\n\
exception instead.\n";
PyObject *obj_BoxScene_set_camera(obj_BoxScene *self,PyObject *arg) {
    try {
        if(self->base.locked) {
            PyErr_SetString(PyExc_RuntimeError,"the scene is locked for reading");
            return NULL;
        }
        
        repr::camera_obj::ref_type c = get_base<n_camera>(arg);
        if(!compatible(self->base,c)) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return NULL;
        }
        
        copy_camera(c,self->base.camera);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

const char *get_camera_doc = "\
get_camera()\n\
\n\
Return a copy of the scene's camera\n";
PyObject *obj_BoxScene_get_camera(obj_BoxScene *self,PyObject *) {
    try {
        const n_camera &c = self->base.camera;
        return to_pyobject(n_camera(c.dimension(),c.origin(),c.axes()));
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
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_set_camera),METH_O,set_camera_doc},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_get_camera),METH_NOARGS,get_camera_doc},
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
                
                new(&ptr->base) BoxScene<repr>(d);
            } catch(...) {
                type->tp_free(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(NULL)

        ptr->weaklist = NULL;
    }
    return reinterpret_cast<PyObject*>(ptr);
}

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
    tp_dictoffset = offsetof(obj_BoxScene,idict),
    tp_new = &obj_BoxScene_new);


int obj_CompositeScene_traverse(obj_CompositeScene *self,visitproc visit,void *arg) {
    /* we can get away with not traversing the camera because it is never
       directly exposed to Python code */
    Py_VISIT(self->idict);
    return 0;
}


int obj_CompositeScene_clear(obj_CompositeScene *self) {
    /* we can get away with not clearing the camera because it is never directly
       exposed to Python code */
    Py_CLEAR(self->idict);
    return 0;
}

PyObject *obj_CompositeScene_set_camera(obj_CompositeScene *self,PyObject *arg) {
    try {
        ensure_unlocked(self);
        
        repr::camera_obj::ref_type c = get_base<n_camera>(arg);
        if(!compatible(self->base,c)) {
            PyErr_SetString(PyExc_TypeError,"the scene and camera must have the same dimension");
            return NULL;
        }
        
        copy_camera(c,self->base.camera);
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_CompositeScene_get_camera(obj_CompositeScene *self,PyObject *) {
    try {
        const n_camera &c = self->base.camera;
        return to_pyobject(n_camera(c.dimension(),c.origin(),c.axes()));
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
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_set_camera),METH_O,set_camera_doc},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_CompositeScene_get_camera),METH_NOARGS,get_camera_doc},
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
                
                new(&ptr->base) composite_scene<repr>(d_node->dimension(),d_node->_data);
                d_node->parent = py::borrowed_ref(reinterpret_cast<PyObject*>(ptr));
            } catch(...) {
                type->tp_free(ptr);
                throw;
            }
        } PY_EXCEPT_HANDLERS(NULL)
    }
    return reinterpret_cast<PyObject*>(ptr);
}

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
    tp_dictoffset = offsetof(obj_CompositeScene,idict),
    tp_new = &obj_CompositeScene_new);


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
        repr::matrix_obj::ref_type orientation = get_base<n_matrix>(ga(true));
        auto position = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        if(!compatible(orientation,position))
            THROW_PYERR_STRING(TypeError,"the orientation and position must have the same dimension");
        
        return reinterpret_cast<PyObject*>(new solid<repr>(type,orientation,position));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Solid_getset[] = {
    {const_cast<char*>("type"),OBJ_GETTER(solid<repr>,int(self->type)),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(solid<repr>,self->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(solid<repr>,self->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(solid<repr>,self->position),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(solid<repr>,self->dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject solid_common::pytype = make_type_object(
    MODULE_STR ".Solid",
    sizeof(solid<repr>),
    tp_dealloc = destructor_dealloc<solid<repr> >::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_getset = obj_Solid_getset,
    tp_base = &obj_Primitive::pytype,
    tp_new = &obj_Solid_new);


void check_origin_dir_compat(const n_vector &o,const n_vector &d) {
    if(!compatible(o,d))
        THROW_PYERR_STRING(TypeError,"\"origin\" and \"direction\" must have the same dimension");
}

PyObject *obj_Triangle_intersects(triangle<repr> *self,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"origin","direction"};
        get_arg ga(args,kwds,names,"Triangle.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<repr> target(origin,direction);
        ray<repr> normal(origin.dimension());
        
        real dist = self->intersects(target,normal);
        if(!dist) Py_RETURN_NONE;
        
        return py::make_tuple(dist,normal.origin,normal.direction).new_ref();
    } PY_EXCEPT_HANDLERS(NULL)
}

std::vector<n_vector> points_for_triangle(PyObject *obj) {
    std::vector<n_vector> points = collect<n_vector>(obj);
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
        return reinterpret_cast<PyObject*>(triangle<repr>::from_points(points.data()));
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
        const char *names[] = {"p1,face_normal,edge_normals"};
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
        
        auto ptr = triangle<repr>::create(p1,face_normal,[&](int i) -> n_vector {
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

PyObject *obj_Triangle_getedges(triangle<repr> *self,void *) {
    try {
        return reinterpret_cast<PyObject*>(new obj_FrozenVectorView(
            reinterpret_cast<PyObject*>(self),
            self->dimension()-1,
            self->items()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Triangle_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(triangle<repr>,self->dimension()),NULL,NULL,NULL},
    {const_cast<char*>("p1"),OBJ_GETTER(triangle<repr>,self->p1),NULL,NULL,NULL},
    {const_cast<char*>("face_normal"),OBJ_GETTER(triangle<repr>,self->face_normal),NULL,NULL,NULL},
    {const_cast<char*>("edge_normals"),reinterpret_cast<getter>(&obj_Triangle_getedges),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_Triangle_members[] = {
    {const_cast<char*>("d"),member_macro<real>::value,offsetof(triangle<repr>,d),READONLY,NULL},
    {NULL}
};

PyTypeObject triangle_common::pytype = make_type_object(
    MODULE_STR ".Triangle",
    triangle<repr>::base_size,
    tp_itemsize = triangle<repr>::item_size,
    tp_dealloc = destructor_dealloc<triangle<repr> >::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_methods = obj_Triangle_methods,
    tp_members = obj_Triangle_members,
    tp_getset = obj_Triangle_getset,
    tp_base = &obj_Primitive::pytype,
    tp_new = &obj_Triangle_new);


PyObject *kdnode_intersects(obj_KDNode *self,PyObject *args,PyObject *kwds) {
    try {
        assert(self->_data->type == LEAF || self->_data->type == BRANCH);
        
        const char *names[] = {"origin","direction","t_near","t_far","cur_d"};
        get_arg ga(args,kwds,names,self->_data->type == LEAF ? "KDLeaf.intersects" : "KDBranch.intersects");
        auto origin = from_pyobject<n_vector>(ga(true));
        auto direction = from_pyobject<n_vector>(ga(true));
        
        real t_near = std::numeric_limits<real>::lowest();
        real t_far = std::numeric_limits<real>::max();
        int cur_d = -1;
        
        PyObject *tmp = ga(false);
        if(tmp) t_near = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) t_far = from_pyobject<real>(tmp);
        tmp = ga(false);
        if(tmp) cur_d = from_pyobject<int>(tmp);
        ga.finished();
        
        check_origin_dir_compat(origin,direction);
        
        ray<repr> target(origin,direction);
        ray<repr> normal(origin.dimension());
        
        if(!self->_data->intersects(target,normal,t_near,t_far,cur_d)) Py_RETURN_NONE;
        
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
    return py::incref(reinterpret_cast<PyObject*>(self->data()->items()[index]));
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
            
            ptr->data() = kd_leaf<repr>::create(size,[=](size_t i) -> primitive<repr>* {
                auto p = primitives[i];
                
                if(p.type() != &solid_common::pytype && p.type() != &triangle_common::pytype) {
                    PyErr_SetString(PyExc_TypeError,"object is not an instance of " MODULE_STR ".Primitive");
                    throw py_error_set();
                }

                return reinterpret_cast<primitive<repr>*>(p.new_ref());
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
    if(obj == Py_None) return nullptr;
    
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
                const char *names[] = {"split","left","right"};
                get_arg ga(args,kwds,names,"KDBranch.__new__");
                auto split = from_pyobject<real>(ga(true));
                auto left = ga(true);
                auto right = ga(true);
                ga.finished();
                
                auto lnode = acceptable_node(left);
                auto rnode = acceptable_node(right);
                
                if(!left && !right)
                    THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" can't both be None");
                
                if(lnode && rnode && !compatible(*lnode,*rnode))
                    THROW_PYERR_STRING(TypeError,"\"left\" and \"right\" must have the same dimension");

                ptr->data() = new kd_branch<repr>(split,lnode ? lnode->_data : nullptr,rnode ? rnode->_data : nullptr);
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

PyObject *new_obj_node(PyObject *parent,kd_node<repr> *node,int dimension) {
    if(node->type == LEAF) return reinterpret_cast<PyObject*>(new obj_KDLeaf(py::borrowed_ref(parent),static_cast<kd_leaf<repr>*>(node)));
    
    assert(node->type == BRANCH);
    return reinterpret_cast<PyObject*>(new obj_KDBranch(py::borrowed_ref(parent),static_cast<kd_branch<repr>*>(node),dimension));
}

PyObject *obj_KDBranch_get_child(obj_KDBranch *self,void *index) {
    assert(&self->data()->right == (&self->data()->left + 1));
    
    try {  
        return new_obj_node(reinterpret_cast<PyObject*>(self),(&self->data()->left)[reinterpret_cast<size_t>(index)].get(),self->dimension);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_KDBranch_getset[] = {
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


PyObject *obj_Vector_str(repr::vector_obj *self) {
    try {
        repr::vector_obj::ref_type base = self->get_base();
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

PyObject *obj_Vector_repr(repr::vector_obj *self) {
    try {
        repr::vector_obj::ref_type base = self->get_base();
        py::list lt(py::object(py::borrowed_ref(reinterpret_cast<PyObject*>(self))));
#if PY_MAJOR_VERSION >= 3
        return PyUnicode_FromFormat("Vector(%d,%R)",base.dimension(),lt.ref());
#else
        py::bytes lrepr = py::new_ref(PyObject_Repr(lt.ref()));
        return PyString_FromFormat("Vector(%d,%s)",base.dimension(),lrepr.data());
#endif
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_richcompare(repr::vector_obj *self,PyObject *arg,int op) {
    repr::vector_obj::ref_type base = self->get_base();
    
    if(PyObject_TypeCheck(arg,&repr::vector_obj::pytype)) {
        if(op == Py_EQ || op == Py_NE) {
            repr::vector_obj::ref_type b = reinterpret_cast<repr::vector_obj*>(arg)->get_base();
            return to_pyobject((compatible(base,b) && base == b) == (op == Py_EQ));
        }
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Vector___neg__(repr::vector_obj *self) {
    try {
        return to_pyobject(-self->get_base());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector___abs__(repr::vector_obj *self) {
    try {
        return to_pyobject(self->get_base().absolute());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector___add__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&repr::vector_obj::pytype)) {
            if(PyObject_TypeCheck(b,&repr::vector_obj::pytype)) {
                repr::vector_obj::ref_type va = reinterpret_cast<repr::vector_obj*>(a)->get_base();
                repr::vector_obj::ref_type vb = reinterpret_cast<repr::vector_obj*>(b)->get_base();
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
        if(PyObject_TypeCheck(a,&repr::vector_obj::pytype)) {
            if(PyObject_TypeCheck(b,&repr::vector_obj::pytype)) {
                repr::vector_obj::ref_type va = reinterpret_cast<repr::vector_obj*>(a)->get_base();
                repr::vector_obj::ref_type vb = reinterpret_cast<repr::vector_obj*>(b)->get_base();
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
        if(PyObject_TypeCheck(a,&repr::vector_obj::pytype)) {
            if(PyNumber_Check(b)) {
                return to_pyobject(reinterpret_cast<repr::vector_obj*>(a)->get_base() * from_pyobject<real>(b));
            }
        } else {
            if(PyNumber_Check(a)) {
                return to_pyobject(from_pyobject<real>(a) * reinterpret_cast<repr::vector_obj*>(b)->get_base());
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

Py_ssize_t obj_Vector___sequence_len__(repr::vector_obj *self) {
    return self->get_base().dimension();
}

PyObject *obj_Vector___sequence_getitem__(repr::vector_obj *self,Py_ssize_t index) {
    repr::vector_obj::ref_type v = self->get_base();
    if(index < 0 || index >= v.dimension()) {
        PyErr_SetString(PyExc_IndexError,"vector index out of range");
        return NULL;
    }
    return to_pyobject(v[index]);
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

PyObject *obj_Vector_square(repr::vector_obj *self,PyObject *) {
    return to_pyobject(self->get_base().square());
}

PyObject *obj_Vector_axis(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","axis","length"};
        get_arg ga(args,kwds,names,"Vector.axis");
        int dimension = from_pyobject<int>(ga(true));
        int axis = from_pyobject<int>(ga(true));
        PyObject *temp = ga(false);
        real length = temp ? from_pyobject<float>(temp) : real(1);
        ga.finished();

        check_dimension(dimension);
        if(axis < 0 || axis >= dimension) {
            PyErr_SetString(PyExc_ValueError,"axis must be between 0 and dimension-1");
            return NULL;
        }
        return to_pyobject(n_vector::axis(dimension,axis,length));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_absolute(repr::vector_obj *self,PyObject *) {
    return to_pyobject(self->get_base().absolute());
}

PyObject *obj_Vector_unit(repr::vector_obj *self,PyObject *) {
    try {
        return to_pyobject(self->get_base().unit());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_apply(repr::vector_obj *_self,PyObject *_func) {
    try {
        repr::vector_obj::ref_type self = _self->get_base();
        
        n_vector r(self.dimension());
        py::object func = py::borrowed_ref(_func);
        
        for(int i=0; i<self.dimension(); ++i)
            r[i] = from_pyobject<real>(func(self[i]));
        
        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Vector_methods[] = {
    {"square",reinterpret_cast<PyCFunction>(&obj_Vector_square),METH_NOARGS,NULL},
    {"axis",reinterpret_cast<PyCFunction>(&obj_Vector_axis),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"absolute",reinterpret_cast<PyCFunction>(&obj_Vector_absolute),METH_NOARGS,NULL},
    {"unit",reinterpret_cast<PyCFunction>(&obj_Vector_unit),METH_NOARGS,NULL},
    {"apply",reinterpret_cast<PyCFunction>(&obj_Vector_apply),METH_O,NULL},
    {NULL}
};

PyObject *obj_Vector_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"Vector.__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::nullable_object values(py::borrowed_ref(ga(false)));
        ga.finished();

        auto ptr = reinterpret_cast<repr::vector_obj*>(py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension))));
        
        if(values) {
            sized_iter itr(*values,dimension);
            for(int i=0; i<dimension; ++i) {
                ptr->cast_base()[i] = from_pyobject<real>(itr.next());
            }
            itr.finished();
        }
        
        return reinterpret_cast<PyObject*>(ptr);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Vector_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(repr::vector_obj,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject vector_obj_base::pytype = make_type_object(
    MODULE_STR ".Vector",
    sizeof(repr::vector_obj) - repr::vector_obj::item_size,
    tp_itemsize = repr::vector_obj::item_size,
    tp_repr = &obj_Vector_repr,
    tp_as_number = &obj_Vector_number_methods,
    tp_as_sequence = &obj_Vector_sequence_methods,
    tp_str = &obj_Vector_str,
    tp_richcompare = &obj_Vector_richcompare,
    tp_methods = obj_Vector_methods,
    tp_getset = obj_Vector_getset,
    tp_new = &obj_Vector_new);


void obj_Camera_dealloc(repr::camera_obj *self) {
    self->dealloc();
    
    Py_XDECREF(self->idict);
    if(self->weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(self));

    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

int obj_Camera_traverse(repr::camera_obj *self,visitproc visit,void *arg) {
    Py_VISIT(self->idict);

    return 0;
}

int obj_Camera_clear(repr::camera_obj *self) {
    Py_CLEAR(self->idict);

    return 0;
}

int obj_Camera_setorigin(repr::camera_obj *self,PyObject *arg,void *) {
    try {
        setter_no_delete(arg);
        self->get_base().origin() = from_pyobject<n_vector>(arg);
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
} 

PyObject *obj_Camera_getaxes(repr::camera_obj *self,void *) {
    try {
        repr::camera_obj::ref_type base = self->get_base();
        return reinterpret_cast<PyObject*>(new obj_CameraAxes(reinterpret_cast<PyObject*>(self),base.dimension(),base.axes()));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Camera_getset[] = {
    {const_cast<char*>("origin"),OBJ_GETTER(repr::camera_obj,self->get_base().origin()),reinterpret_cast<setter>(&obj_Camera_setorigin),NULL,NULL},
    {const_cast<char*>("axes"),reinterpret_cast<getter>(&obj_Camera_getaxes),NULL,NULL,NULL},
    {const_cast<char*>("dimension"),OBJ_GETTER(repr::camera_obj,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyObject *obj_Camera_normalize(repr::camera_obj *self,PyObject *) {
    self->get_base().normalize();
    Py_RETURN_NONE;
}

PyObject *obj_Camera_translate(repr::camera_obj *self,PyObject *arg) {
    try {
        self->get_base().translate(from_pyobject<n_vector>(arg));
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Camera_methods[] = {
    {"normalize",reinterpret_cast<PyCFunction>(&obj_Camera_normalize),METH_NOARGS,NULL},
    {"translate",reinterpret_cast<PyCFunction>(&obj_Camera_translate),METH_O,NULL},
    {NULL}
};

PyObject *obj_Camera_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension"};
        get_arg ga(args,kwds,names,"Camera.__new__");
        int dimension = from_pyobject<int>(ga(true));
        ga.finished();
        
        PyObject *ptr = py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension)));
        
        reinterpret_cast<repr::camera_obj*>(ptr)->alloc(dimension);
        return ptr;
    } PY_EXCEPT_HANDLERS(NULL)
}

template<int N> void zero_vector(fixed::vector<N,real> &v) {
    v.fill_with(real(0));
}

void zero_vector(var::repr::py_vector_t &v) {
    if(Py_REFCNT(v.store.data) == 1) {
        v.fill_with(real(0));
    } else {
        /* we don't need to assign any values to the elements since memory
           allocated by Python is automatically zeroed out */
        v = var::repr::py_vector_t(v.dimension());
    }
}

int obj_Camera_init(repr::camera_obj *self,PyObject *args,PyObject *kwds) {
    try {
        repr::camera_obj::ref_type base = self->get_base();
        
        zero_vector(base.origin());
        
        for(int i=0; i<base.dimension(); ++i) {
            zero_vector(base.axes()[i]);
            base.axes()[i][i] = 1;
        }
        
        return 0;
    } PY_EXCEPT_HANDLERS(-1);
}

PyTypeObject camera_obj_base::pytype = make_type_object(
    MODULE_STR ".Camera",
    sizeof(repr::camera_obj) - repr::camera_obj::item_size,
    tp_itemsize = repr::camera_obj::item_size,
    tp_dealloc = &obj_Camera_dealloc,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &obj_Camera_traverse,
    tp_clear = &obj_Camera_clear,
    tp_weaklistoffset = offsetof(repr::camera_obj,weaklist),
    tp_methods = obj_Camera_methods,
    tp_getset = obj_Camera_getset,
    tp_dictoffset = offsetof(repr::camera_obj,idict),
    tp_init = &obj_Camera_init,
    tp_new = &obj_Camera_new);


PyObject *obj_Matrix___mul__(PyObject *a,PyObject *b) {
    try {
        if(PyObject_TypeCheck(a,&repr::matrix_obj::pytype)) {
            repr::matrix_obj::ref_type base = reinterpret_cast<repr::matrix_obj*>(a)->get_base();
            if(PyObject_TypeCheck(b,&repr::matrix_obj::pytype)) {
                repr::matrix_obj::ref_type mb = reinterpret_cast<repr::matrix_obj*>(b)->get_base();
                if(!compatible(base,mb)) {
                    PyErr_SetString(PyExc_TypeError,"cannot multiply matrices of different dimension");
                    return NULL;
                }
                return to_pyobject(base * mb);
            }
            if(PyObject_TypeCheck(b,&repr::vector_obj::pytype)) {
                repr::vector_obj::ref_type vb = reinterpret_cast<repr::vector_obj*>(b)->get_base();
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

Py_ssize_t obj_Matrix___sequence_len__(repr::matrix_obj *self) {
    return self->get_base().dimension();
}

PyObject *obj_Matrix___sequence_getitem__(repr::matrix_obj *self,Py_ssize_t index) {
    try {
        repr::matrix_obj::ref_type base = self->get_base();

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
            if(PyObject_TypeCheck(PyTuple_GET_ITEM(args,0),&repr::vector_obj::pytype)) {
                return to_pyobject(n_matrix::scale(
                    reinterpret_cast<repr::vector_obj*>(PyTuple_GET_ITEM(args,0))->get_base()));
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

PyObject *obj_Matrix_values(repr::matrix_obj *self,PyObject *) {
    try {
        repr::matrix_obj::ref_type base = self->get_base();
        return reinterpret_cast<PyObject*>(
            new obj_MatrixProxy(reinterpret_cast<PyObject*>(self),base.dimension() * base.dimension(),base[0]));
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

PyObject *obj_Matrix_determinant(repr::matrix_obj *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().determinant());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_inverse(repr::matrix_obj *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().inverse());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Matrix_transpose(repr::matrix_obj *self,PyObject*) {
    try {
        return to_pyobject(self->get_base().transpose());
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Matrix_methods[] = {
    {"scale",reinterpret_cast<PyCFunction>(&obj_Matrix_scale),METH_VARARGS|METH_STATIC,NULL},
    {"values",reinterpret_cast<PyCFunction>(&obj_Matrix_values),METH_NOARGS,"All the matrix elements as a flat sequence"},
    {"rotation",reinterpret_cast<PyCFunction>(&obj_Matrix_rotation),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"identity",reinterpret_cast<PyCFunction>(&obj_Matrix_identity),METH_O|METH_STATIC,NULL},
    {"determinant",reinterpret_cast<PyCFunction>(&obj_Matrix_determinant),METH_NOARGS,NULL},
    {"inverse",reinterpret_cast<PyCFunction>(&obj_Matrix_inverse),METH_NOARGS,NULL},
    {"transpose",reinterpret_cast<PyCFunction>(&obj_Matrix_transpose),METH_NOARGS,NULL},
    {NULL}
};

void copy_row(repr::matrix_obj *m,py::object values,int row,int len) {
    sized_iter itr(values,len);
    for(int col=0; col<len; ++col) {
        m->cast_base()[row][col] = from_pyobject<real>(itr.next());
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

        auto ptr = reinterpret_cast<repr::matrix_obj*>(py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension) * dimension)));
        
        sized_iter itr(values,0);
        py::object item = itr.next();
        if(PyNumber_Check(item.ref())) {
            itr.expected_len = dimension * dimension;
            
            ptr->cast_base()[0][0] = from_pyobject<real>(item);
            for(int i=1; i<dimension*dimension; ++i) {
                ptr->cast_base().data()[i] = from_pyobject<real>(itr.next());
            }
        } else {
            itr.expected_len = dimension;
            copy_row(ptr,item,0,dimension);
            
            for(int row=1; row<dimension; ++row) copy_row(ptr,itr.next(),row,dimension);
        }
        
        itr.finished();

        return reinterpret_cast<PyObject*>(ptr);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_Matrix_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(repr::matrix_obj,self->get_base().dimension()),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject matrix_obj_base::pytype = make_type_object(
    MODULE_STR ".Matrix",
    sizeof(repr::matrix_obj) - repr::matrix_obj::item_size,
    tp_itemsize = repr::matrix_obj::item_size,
    tp_as_number = &obj_Matrix_number_methods,
    tp_as_sequence = &obj_Matrix_sequence_methods,
    tp_methods = obj_Matrix_methods,
    tp_getset = obj_Matrix_getset,
    tp_new = &obj_Matrix_new);


PyObject *aabb_split(obj_AABB *self,PyObject *args,PyObject *kwds,bool right) {
    obj_AABB *r;
    int axis;
    real split;
    
    try {
        const char *names[] = {"axis","split"};
        get_arg ga(args,kwds,names,right ? "AABB.right" : "AABB.left");
        axis = from_pyobject<int>(ga(true));
        split = from_pyobject<real>(ga(false));
        ga.finished();
        
        if(axis < 0 || axis >= self->start.dimension()) {
            PyErr_SetString(PyExc_ValueError,"invalid axis");
            return NULL;
        }
        if(split <= self->start[axis] || split >= self->end[axis]) {
            PyErr_SetString(PyExc_ValueError,"\"split\" must be inside the box within the given axis");
            return NULL;
        }
    
        r = new obj_AABB(self->start,self->end);
    } PY_EXCEPT_HANDLERS(NULL)
    
    if(right) r->start[axis] = split;
    else r->end[axis] = split;
    
    return reinterpret_cast<PyObject*>(r);
}

PyObject *obj_AABB_left(obj_AABB *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,false);
}

PyObject *obj_AABB_right(obj_AABB *self,PyObject *args,PyObject *kwds) {
    return aabb_split(self,args,kwds,true);
}

real clamp(real x) {
    if(x > 1) return 1;
    if(x < -1) return -1;
    return x;
}

real skip_dot(const n_vector &a,const n_vector &b,int skip) {
    assert(a.dimension() == b.dimension());
    
    real tot = 0;
    for(int i=0; i<skip; ++i) tot += a[i] * b[i];
    for(int i=skip+1; i<a.dimension(); ++i) tot += a[i] * b[i];
    return tot;
}

/* All Prototype intersection tests should only return true if the intersection
   between the AABB and the Primitive has a non-zero volume. E.g. two cubes that
   share a face do not count as intersecting. This is important because k-d tree
   split positions are always at Primitive boundaries and a Primitive should end
   up on only one side of the split (hyper)plane. */

bool aabb_triangle_intersects(const obj_AABB *aabb,const obj_TrianglePrototype *tp) {
    for(int i=0; i<aabb->dimension(); ++i) {
        if(tp->aabb_min[i] >= aabb->end[i] || tp->aabb_max[i] <= aabb->start[i]) return false;
    }
    
    real n_offset = dot(tp->face_normal,tp->items()[0].point);
    n_vector origin = (aabb->start + aabb->end) * 0.5;
    
    real po = dot(origin,tp->face_normal);
    
    real b_max = 0;
    for(int i=0; i<aabb->dimension(); ++i) b_max += std::abs((aabb->end[i] - aabb->start[i])/2 * tp->face_normal[i]);
    real b_min = po - b_max;
    b_max += po;
    
    if(b_max < n_offset || b_min > n_offset) return false;
    
    for(int i=0; i<aabb->dimension(); ++i) {
        const n_vector &axis = tp->items()[i].edge_normal;
        
        for(int j=0; j<aabb->dimension(); ++j) {
            real t_max = skip_dot(tp->items()[0].point,axis,j);
            real t_min = skip_dot(tp->items()[i ? i : 1].point,axis,j);
            if(t_min > t_max) std::swap(t_max,t_min);
            
            po = skip_dot(origin,axis,j);
            
            b_max = 0;
            for(int k=0; k<aabb->dimension(); ++k) {
                if(k != j) b_max += std::abs((aabb->end[k] - aabb->start[k])/2 * axis[k]);
            }
            b_min = po - b_max;
            b_max += po;
            
            if(b_max <= t_min || b_min >= t_max) return false;
        }
    }
    
    return true;
}

bool box_box_axis_test(const obj_AABB *aabb,const solid<repr> *c,const n_vector &axis) {
    real a_po = dot(c->position,axis);
    real b_po = dot((aabb->start + aabb->end) * 0.5,axis);
    
    real a_max = 0;
    for(int i=0; i<aabb->dimension(); ++i) a_max += std::abs(dot(cube_component(c,i),axis));
    
    real b_max = 0;
    for(int i=0; i<aabb->dimension(); ++i) b_max += std::abs((aabb->end[i] - aabb->start[i])/2 * axis[i]);
    
    return b_po+b_max < a_po-a_max || b_po-b_max > a_po+a_max;
}

bool aabb_cube_intersects(const obj_AABB *aabb,const obj_SolidPrototype *c) {
    for(int i=0; i<aabb->dimension(); ++i) {
        if(aabb->end[i] <= c->aabb_min[i] || aabb->start[i] >= c->aabb_max[i]) return false;
    }
    
    for(int i=0; i<aabb->dimension(); ++i) {
        n_vector normal = cube_normal(c->p.get(),i);
        
        if(box_box_axis_test(aabb,c->p.get(),normal)) return false;
        
        // try projecting the normal onto each orthogonal hyperplane
        for(int j=0; j<aabb->dimension(); ++j) {
            n_vector axis = normal * -normal[j];
            axis[j] += normal.square();
            
            if(box_box_axis_test(aabb,c->p.get(),axis)) return false;
        }
    }
    
    return true;
}

bool aabb_sphere_intersects(const obj_AABB *aabb,const solid<repr> *s) {
    n_vector box_p = s->position - s->inv_orientation * ((aabb->start + aabb->end) * 0.5);
            
    n_vector closest(aabb->dimension(),0);
    
    for(int i=0; i<aabb->dimension(); ++i) {
        // equivalent to: s->orientation.transpose() * n_vector::axis(aabb->dimension(),i,(aabb->end[i] - aabb->start[i])/2)
        n_vector component = s->orientation[i] * ((aabb->end[i] - aabb->start[i])/2);
        closest += clamp(dot(box_p,component)/component.square()) * component;
    }
    
    return (s->position - closest).square() < 1;
}

PyObject *intersect_type_error(PyObject *o) {
    if(Py_TYPE(o) == &triangle_common::pytype || Py_TYPE(o) == &solid_common::pytype) {
        PyErr_SetString(PyExc_TypeError,
            "Instances of Triangle and Solid cannot be used directly. Use TrianglePrototype and SolidPrototype instead.");
    } else {
        PyErr_SetString(PyExc_TypeError,
            "object must be an instance of " MODULE_STR ".PrimitivePrototype");
    }
    return NULL;
}

PyObject *obj_AABB_intersects(obj_AABB *self,PyObject *obj) {
    static const char dim_err[] = "cannot perform intersection test on object with different dimension";
    
    try {
        if(PyObject_TypeCheck(obj,&obj_TrianglePrototype::pytype)) {
            auto tp = reinterpret_cast<obj_TrianglePrototype*>(obj);
            
            if(self->dimension() != tp->dimension()) {
                PyErr_SetString(PyExc_TypeError,dim_err);
                return NULL;
            }
            
            return to_pyobject(aabb_triangle_intersects(self,tp));
        }
        
        if(PyObject_TypeCheck(obj,&obj_SolidPrototype::pytype)) {
            auto sp = reinterpret_cast<obj_SolidPrototype*>(obj);
            
            if(self->dimension() != sp->dimension()) {
                PyErr_SetString(PyExc_TypeError,dim_err);
                return NULL;
            }
        
            if(sp->p->type == CUBE)
                return to_pyobject(aabb_cube_intersects(self,sp));
            
            assert(sp->p->type == SPHERE);
            return to_pyobject(aabb_sphere_intersects(self,sp->p.get()));
        }

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
    auto ptr = reinterpret_cast<obj_AABB*>(type->tp_alloc(type,0));
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
            
            if(start_obj) {
                auto start = from_pyobject<n_vector>(start_obj);
                if(dimension != start.dimension())
                    THROW_PYERR_STRING(TypeError,"\"start\" has a dimension different from \"dimension\"");
                new(&ptr->start) n_vector(start);
            } else new(&ptr->start) n_vector(dimension,std::numeric_limits<real>::lowest());
        

            if(end_obj) {
                auto end = from_pyobject<n_vector>(end_obj);
                if(dimension != end.dimension())
                    THROW_PYERR_STRING(TypeError,"\"end\" has a dimension different from \"dimension\"");
                new(&ptr->end) n_vector(end);
            } else new(&ptr->end) n_vector(dimension,std::numeric_limits<real>::max());
 
        } catch(...) {
            Py_DECREF(ptr);
        }
    } PY_EXCEPT_HANDLERS(NULL)
    
    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_AABB_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_AABB,self->start.dimension()),NULL,NULL,NULL},
    {const_cast<char*>("start"),OBJ_GETTER(obj_AABB,self->start),NULL,NULL,NULL},
    {const_cast<char*>("end"),OBJ_GETTER(obj_AABB,self->end),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_AABB::pytype = make_type_object(
    MODULE_STR ".AABB",
    sizeof(obj_AABB),
    tp_dealloc = destructor_dealloc<obj_AABB>::value,
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
        return reinterpret_cast<PyObject*>(triangle<repr>::create(
            self->items()[0].point,
            self->face_normal,
            [=](int i){ return self->items()[i+1].edge_normal; }));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_TrianglePrototype_methods[] = {
    {"realize",reinterpret_cast<PyCFunction>(&obj_TrianglePrototype_realize),METH_NOARGS,NULL},
    {NULL}
};

PyObject *obj_TrianglePrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    obj_TrianglePrototype *ptr;
    
    try {
        const char *names[] = {"points"};
        get_arg ga(args,kwds,names,"TrianglePrototype.__new__");
        PyObject *points_obj = ga(true);
        ga.finished();
        
        auto points = points_for_triangle(points_obj);
        int dim = points[0].dimension();
        
        ptr = reinterpret_cast<obj_TrianglePrototype*>(py::check_obj(type->tp_alloc(type,repr::required_d ? 0 : dim)));
        
        try {
            new(&ptr->aabb_max) n_vector(dim,std::numeric_limits<real>::lowest());
            new(&ptr->aabb_min) n_vector(dim,std::numeric_limits<real>::max());
            
            for(int i=0; i<dim; ++i) {
                for(int j=0; j<dim; ++j) {
                    if(points[i][j] > ptr->aabb_max[j]) ptr->aabb_max[j] = points[i][j];
                    if(points[i][j] < ptr->aabb_min[j]) ptr->aabb_min[j] = points[i][j];
                }
            }

            smaller<n_matrix> tmp(dim-1);
            
            repr::smaller_init_array<n_vector> vsides(dim-1,[&](int i) -> n_vector { return points[i+1] - points[0]; });
            new(&ptr->face_normal) n_vector(dim);
            ::impl::cross(ptr->face_normal,tmp,static_cast<n_vector*>(vsides));
            auto square = ptr->face_normal.square();
            
            new(&ptr->items()[0]) triangle_point(points[0],n_vector(dim,0));
            
            for(int i=1; i<dim; ++i) {
                auto old = vsides[i-1];
                vsides[i-1] = ptr->face_normal;
                
                new(&ptr->items()[i]) triangle_point(points[i],n_vector(dim));
                
                ::impl::cross(ptr->items()[i].edge_normal,tmp,static_cast<n_vector*>(vsides));
                vsides[i-1] = old;
                ptr->items()[i].edge_normal /= square;
                
                ptr->items()[0].edge_normal -= ptr->items()[i].edge_normal;
            }
        } catch(...) {
            Py_DECREF(ptr);
        }
    } PY_EXCEPT_HANDLERS(NULL)
    
    return reinterpret_cast<PyObject*>(ptr);
}

PyGetSetDef obj_TrianglePrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_TrianglePrototype,self->face_normal.dimension()),NULL,NULL,NULL},
    {const_cast<char*>("face_normal"),OBJ_GETTER(obj_TrianglePrototype,self->face_normal),NULL,NULL,NULL},
    {const_cast<char*>("point_data"),OBJ_GETTER(
        obj_TrianglePrototype,
        reinterpret_cast<PyObject*>(new obj_TrianglePointData(obj_self,self->dimension(),self->items()))),NULL,NULL,NULL},
    {const_cast<char*>("aabb_max"),OBJ_GETTER(obj_TrianglePrototype,self->aabb_max),NULL,NULL,NULL},
    {const_cast<char*>("aabb_min"),OBJ_GETTER(obj_TrianglePrototype,self->aabb_min),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_TrianglePrototype::pytype = make_type_object(
    MODULE_STR ".TrianglePrototype",
    obj_TrianglePrototype::base_size,
    tp_itemsize = obj_TrianglePrototype::item_size,
    tp_dealloc = destructor_dealloc<obj_TrianglePrototype>::value,
    tp_methods = obj_TrianglePrototype_methods,
    tp_getset = obj_TrianglePrototype_getset,
    tp_base = &obj_PrimitivePrototype::pytype,
    tp_new = &obj_TrianglePrototype_new);


PyGetSetDef obj_TrianglePointDatum_getset[] = {
    {const_cast<char*>("point"),OBJ_GETTER(obj_TrianglePointDatum,self->data.point),NULL,NULL,NULL},
    {const_cast<char*>("edge_normal"),OBJ_GETTER(obj_TrianglePointDatum,self->data.edge_normal),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_TrianglePointDatum::pytype = make_type_object(
    MODULE_STR ".TrianglePointDatum",
    sizeof(obj_TrianglePointDatum),
    tp_dealloc = destructor_dealloc<obj_TrianglePointDatum>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_getset = obj_TrianglePointDatum_getset,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"The TrianglePointDatum type cannot be instantiated directly");
        return nullptr;
    });


PyObject *obj_SolidPrototype_realize(obj_SolidPrototype *self,PyObject*) {
    return py::incref(self->p.ref());
}

PyMethodDef obj_SolidPrototype_methods[] = {
    {"realize",reinterpret_cast<PyCFunction>(&obj_SolidPrototype_realize),METH_NOARGS,NULL},
    {NULL}
};

PyObject *obj_SolidPrototype_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    auto ptr = reinterpret_cast<obj_SolidPrototype*>(type->tp_alloc(type,0));
    if(!ptr) return NULL;
    
    try {
        try {
            const char *names[] = {"type","orientation","position"};
            get_arg ga(args,kwds,names,"SolidPrototype.__new__");
            auto type = from_pyobject<solid_type>(ga(true));
            repr::matrix_obj::ref_type orientation = get_base<n_matrix>(ga(true));
            auto position = from_pyobject<n_vector>(ga(true));
            ga.finished();
            
            if(!compatible(orientation,position))
                THROW_PYERR_STRING(TypeError,"the orientation and position must have the same dimension"); 
            
            new(&ptr->p) py::pyptr<solid<repr> >(py::new_ref(reinterpret_cast<PyObject*>(new solid<repr>(type,orientation,position))));

            if(type == CUBE) {
                new(&ptr->aabb_max) n_vector(position.dimension(),0);
                for(int i=0; i<position.dimension(); ++i) ptr->aabb_max += cube_component(ptr->p.get(),i).apply(static_cast<real (*)(real)>(&std::abs));
                new(&ptr->aabb_min) n_vector(ptr->p->position - ptr->aabb_max);
                ptr->aabb_max += ptr->p->position;
            } else {
                assert(type == SPHERE);

                new(&ptr->aabb_max) n_vector(position.dimension());
                new(&ptr->aabb_min) n_vector(position.dimension());

                for(int i=0; i<position.dimension(); ++i) {
                    // equivalent to: (orientation.transpose() * n_vector::axis(position->dimension(),i)).unit()
                    n_vector normal = orientation[i].unit();
                    
                    real max = dot(n_vector::axis(position.dimension(),i) - position,normal);
                    real min = dot(n_vector::axis(position.dimension(),i,-1) - position,normal);
                    if(min > max) std::swap(max,min);

                    ptr->aabb_max[i] = max;
                    ptr->aabb_min[i] = min;
                }
            }
            
            return reinterpret_cast<PyObject*>(ptr);
        } catch(...) {
            Py_DECREF(ptr);
            throw;
        }
    } PY_EXCEPT_HANDLERS(NULL)
}

PyGetSetDef obj_SolidPrototype_getset[] = {
    {const_cast<char*>("dimension"),OBJ_GETTER(obj_SolidPrototype,self->dimension()),NULL,NULL,NULL},
    {const_cast<char*>("type"),OBJ_GETTER(obj_SolidPrototype,self->p->type),NULL,NULL,NULL},
    {const_cast<char*>("orientation"),OBJ_GETTER(obj_SolidPrototype,self->p->orientation),NULL,NULL,NULL},
    {const_cast<char*>("inv_orientation"),OBJ_GETTER(obj_SolidPrototype,self->p->inv_orientation),NULL,NULL,NULL},
    {const_cast<char*>("position"),OBJ_GETTER(obj_SolidPrototype,self->p->position),NULL,NULL,NULL},
    {const_cast<char*>("aabb_max"),OBJ_GETTER(obj_SolidPrototype,self->aabb_max),NULL,NULL,NULL},
    {const_cast<char*>("aabb_min"),OBJ_GETTER(obj_SolidPrototype,self->aabb_min),NULL,NULL,NULL},
    {NULL}
};

PyTypeObject obj_SolidPrototype::pytype = make_type_object(
    MODULE_STR ".SolidPrototype",
    sizeof(obj_SolidPrototype),
    tp_dealloc = destructor_dealloc<obj_SolidPrototype>::value,
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
        std::vector<n_vector> vs = collect<n_vector>(arg);
        
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

        return to_pyobject(cross<repr>(dim,&vs[0]));
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
    &solid<repr>::pytype,
    &triangle<repr>::pytype,
    &obj_FrozenVectorView::pytype,
    &obj_KDNode::pytype,
    &obj_KDLeaf::pytype,
    &obj_KDBranch::pytype,
    &repr::vector_obj::pytype,
    &obj_MatrixProxy::pytype,
    &repr::camera_obj::pytype,
    &obj_CameraAxes::pytype,
    &repr::matrix_obj::pytype,
    &obj_AABB::pytype,
    &obj_PrimitivePrototype::pytype,
    &obj_TrianglePrototype::pytype,
    &obj_TrianglePointDatum::pytype,
    &obj_TrianglePointData::pytype,
    &obj_SolidPrototype::pytype};


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

