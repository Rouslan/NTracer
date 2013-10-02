
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


typedef repr::py_vector_t n_vector;
typedef repr::py_matrix_t n_matrix;
typedef repr::camera_t n_camera;


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

class sized_iter {
public:
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

template<typename T> void destructor_dealloc(PyObject *self) {
    reinterpret_cast<T*>(self)->~T();
    Py_TYPE(self)->tp_free(self);
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


struct obj_MatrixProxy {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    py::array_adapter<REAL> base;
    PY_MEM_NEW_DELETE
    obj_MatrixProxy(py::array_adapter<REAL> const &_0) : base(_0) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    obj_MatrixProxy(PyObject *_0,long unsigned int _1,REAL *_2) : base(_0,_1,_2) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    
    py::array_adapter<REAL> &cast_base() { return base; }
    py::array_adapter<REAL> &get_base() { return base; }
};

template<> struct wrapped_type<py::array_adapter<REAL> > {
    typedef obj_MatrixProxy type;
};


struct obj_CameraAxes {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    py::array_adapter<n_vector> base;
    PY_MEM_GC_NEW_DELETE
    obj_CameraAxes(py::array_adapter<n_vector> const &b) : base(b) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    obj_CameraAxes(PyObject * _0,size_t _1,n_vector *_2) : base(_0,_1,_2) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    
    py::array_adapter<n_vector> &cast_base() { return base; }
    py::array_adapter<n_vector> &get_base() { return base; }
};

template<> struct wrapped_type<py::array_adapter<n_vector> > {
    typedef obj_CameraAxes type;
};



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
    
    /* we only need a shallow copy since vectors are immutable in Python
       code and BoxScene doesn't modify its camera */
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

PyMethodDef obj_BoxScene_methods[] = {
    {"set_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_set_camera),METH_O,set_camera_doc},
    {"get_camera",reinterpret_cast<PyCFunction>(&obj_BoxScene_get_camera),METH_NOARGS,get_camera_doc},
    {NULL}
};

PyObject *obj_BoxScene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    obj_BoxScene *ptr = reinterpret_cast<obj_BoxScene*>(type->tp_alloc(type,0));
    if(ptr) {
        try {
            try {
                const char *names[] = {"dimension"};
                get_arg ga(args,kwds,names,"__new__");
                REAL d = from_pyobject<REAL>(ga(true));
                ga.finished();
                
                new(&ptr->base) BoxScene<repr>(d);
            } catch(...) {
                Py_DECREF(ptr);
            }
        } PY_EXCEPT_HANDLERS(NULL)

        ptr->weaklist = NULL;
    }
    return reinterpret_cast<PyObject*>(ptr);
}

PyMemberDef obj_BoxScene_members[] = {
    {const_cast<char*>("fov"),member_macro<REAL>::value,offsetof(obj_BoxScene,base.fov),0,NULL},
    {NULL}
};

PyTypeObject obj_BoxScene::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".BoxScene", /* tp_name */
    sizeof(obj_BoxScene), /* tp_basicsize */
    0,                         /* tp_itemsize */
    &destructor_dealloc<obj_BoxScene>, /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL, /* tp_compare */
    NULL, /* tp_repr */
    NULL, /* tp_as_number */
    NULL, /* tp_as_sequence */
    NULL, /* tp_as_mapping */
    NULL, /* tp_hash */
    NULL, /* tp_call */
    NULL, /* tp_str */
    NULL, /* tp_getattro */
    NULL, /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC, /* tp_flags */
    NULL, /* tp_doc */
    reinterpret_cast<traverseproc>(&obj_BoxScene_traverse), /* tp_traverse */
    reinterpret_cast<inquiry>(&obj_BoxScene_clear), /* tp_clear */
    NULL, /* tp_richcompare */
    offsetof(obj_BoxScene,weaklist), /* tp_weaklistoffset */
    NULL, /* tp_iter */
    NULL, /* tp_iternext */
    obj_BoxScene_methods, /* tp_methods */
    obj_BoxScene_members, /* tp_members */
    NULL, /* tp_getset */
    NULL, /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    offsetof(obj_BoxScene,idict), /* tp_dictoffset */
    NULL, /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_BoxScene_new /* tp_new */
};


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
                return to_pyobject(reinterpret_cast<repr::vector_obj*>(a)->get_base() * from_pyobject<REAL>(b));
            }
        } else {
            if(PyNumber_Check(a)) {
                return to_pyobject(from_pyobject<REAL>(a) * reinterpret_cast<repr::vector_obj*>(b)->get_base());
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

PyObject *obj_Vector_dot(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"a","b"};
        get_arg ga(args,kwds,names,"dot");
        repr::vector_obj::ref_type a = get_base<n_vector>(ga(true));
        repr::vector_obj::ref_type b = get_base<n_vector>(ga(true));
        ga.finished();
        
        if(!compatible(a,b)) {
            PyErr_SetString(PyExc_TypeError,"cannot perform dot product on vectors of different dimension");
            return NULL;
        }
        return to_pyobject(n_vector::dot(a,b));

    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Vector_axis(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","axis","length"};
        get_arg ga(args,kwds,names,"axis");
        int dimension = from_pyobject<int>(ga(true));
        int axis = from_pyobject<int>(ga(true));
        PyObject *temp = ga(false);
        REAL length = temp ? from_pyobject<float>(temp) : REAL(1);
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

PyMethodDef obj_Vector_methods[] = {
    {"square",reinterpret_cast<PyCFunction>(&obj_Vector_square),METH_NOARGS,NULL},
    {"dot",reinterpret_cast<PyCFunction>(&obj_Vector_dot),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"axis",reinterpret_cast<PyCFunction>(&obj_Vector_axis),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"absolute",reinterpret_cast<PyCFunction>(&obj_Vector_absolute),METH_NOARGS,NULL},
    {"unit",reinterpret_cast<PyCFunction>(&obj_Vector_unit),METH_NOARGS,NULL},
    {NULL}
};

PyObject *obj_Vector_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::nullable_object values(py::borrowed_ref(ga(false)));
        ga.finished();

        repr::vector_obj *ptr = reinterpret_cast<repr::vector_obj*>(py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension))));
        
        if(values) {
            sized_iter itr(*values,dimension);
            for(int i=0; i<dimension; ++i) {
                ptr->cast_base()[i] = from_pyobject<REAL>(itr.next());
            }
            itr.finished();
        }
        
        return reinterpret_cast<PyObject*>(ptr);
    } PY_EXCEPT_HANDLERS(NULL)
}

PyTypeObject vector_obj_base::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".Vector",             /* tp_name */
    sizeof(repr::vector_obj) - repr::vector_obj::item_size, /* tp_basicsize */
    repr::vector_obj::item_size,        /* tp_itemsize */
    NULL,                         /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL,                         /* tp_compare */
    reinterpret_cast<reprfunc>(&obj_Vector_repr), /* tp_repr */
    &obj_Vector_number_methods,   /* tp_as_number */
    &obj_Vector_sequence_methods, /* tp_as_sequence */
    NULL,                         /* tp_as_mapping */
    NULL,                         /* tp_hash */
    NULL,                         /* tp_call */
    reinterpret_cast<reprfunc>(&obj_Vector_str), /* tp_str */
    NULL,                         /* tp_getattro */
    NULL,                         /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES, /* tp_flags */
    NULL,                         /* tp_doc */
    NULL,                         /* tp_traverse */
    NULL,                         /* tp_clear */
    reinterpret_cast<richcmpfunc>(&obj_Vector_richcompare), /* tp_richcompare */
    0,                            /* tp_weaklistoffset */
    NULL,                         /* tp_iter */
    NULL,                         /* tp_iternext */
    obj_Vector_methods,           /* tp_methods */
    NULL,                         /* tp_members */
    NULL,                         /* tp_getset */
    NULL,                         /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    0,                            /* tp_dictoffset */
    NULL,                         /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_Vector_new               /* tp_new */
};

Py_ssize_t obj_MatrixProxy___sequence_len__(obj_MatrixProxy *self) {
    return self->base.length();
}

PyObject *obj_MatrixProxy___sequence_getitem__(obj_MatrixProxy *self,Py_ssize_t index) {
    try {
        return to_pyobject(self->base.sequence_getitem(index));
    } PY_EXCEPT_HANDLERS(NULL)
}

PySequenceMethods obj_MatrixProxy_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_MatrixProxy___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_MatrixProxy___sequence_getitem__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PyObject *obj_MatrixProxy_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    PyErr_SetString(PyExc_TypeError,"The MatrixProxy type cannot be instantiated directly");
    return NULL;
}

PyTypeObject obj_MatrixProxy::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".MatrixProxy", /* tp_name */
    sizeof(obj_MatrixProxy), /* tp_basicsize */
    0,                         /* tp_itemsize */
    &destructor_dealloc<obj_MatrixProxy>, /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL, /* tp_compare */
    NULL, /* tp_repr */
    NULL, /* tp_as_number */
    &obj_MatrixProxy_sequence_methods, /* tp_as_sequence */
    NULL, /* tp_as_mapping */
    NULL, /* tp_hash */
    NULL, /* tp_call */
    NULL, /* tp_str */
    NULL, /* tp_getattro */
    NULL, /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES, /* tp_flags */
    NULL, /* tp_doc */
    NULL, /* tp_traverse */
    NULL, /* tp_clear */
    NULL, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    NULL, /* tp_iter */
    NULL, /* tp_iternext */
    NULL, /* tp_methods */
    NULL, /* tp_members */
    NULL, /* tp_getset */
    NULL, /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    0, /* tp_dictoffset */
    NULL, /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_MatrixProxy_new /* tp_new */
};

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


PyObject *obj_Camera_getorigin(repr::camera_obj *self,void *) {
    try {
        return to_pyobject(self->get_base().origin());
    } PY_EXCEPT_HANDLERS(NULL)
}

int obj_Camera_setorigin(repr::camera_obj *self,PyObject *arg,void *) {
    if(!arg) {
        PyErr_SetString(PyExc_TypeError,no_delete_msg);
        return -1;
    }

    try {
        self->get_base().origin() = get_base<n_vector>(arg);
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
    {const_cast<char*>("origin"),reinterpret_cast<getter>(&obj_Camera_getorigin),reinterpret_cast<setter>(&obj_Camera_setorigin),NULL,NULL},
    {const_cast<char*>("axes"),reinterpret_cast<getter>(&obj_Camera_getaxes),NULL,NULL,NULL},
    {NULL}
};

PyObject *obj_Camera_normalize(repr::camera_obj *self,PyObject *) {
    self->get_base().normalize();
    Py_RETURN_NONE;
}

PyObject *obj_Camera_translate(repr::camera_obj *self,PyObject *arg) {
    try {
        self->get_base().translate(get_base<n_vector>(arg));
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
        get_arg ga(args,kwds,names,"__new__");
        int dimension = from_pyobject<int>(ga(true));
        ga.finished();
        
        PyObject *ptr = py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension)));
        
        reinterpret_cast<repr::camera_obj*>(ptr)->alloc(dimension);
        return ptr;
    } PY_EXCEPT_HANDLERS(NULL)
}

template<int N> void zero_vector(fixed::vector<N,REAL> &v) {
    v.fill_with(REAL(0));
}

void zero_vector(var::repr::py_vector_t &v) {
    if(Py_REFCNT(v.store.data) == 1) {
        v.fill_with(REAL(0));
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
            base.axes()[i][i] = REAL(1);
        }
        
        return 0;
    } PY_EXCEPT_HANDLERS(-1);
}

PyTypeObject camera_obj_base::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".Camera",         /* tp_name */
    sizeof(repr::camera_obj) - repr::camera_obj::item_size, /* tp_basicsize */
    repr::camera_obj::item_size,        /* tp_itemsize */
    reinterpret_cast<destructor>(&obj_Camera_dealloc), /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL,                         /* tp_compare */
    NULL,                         /* tp_repr */
    NULL,                         /* tp_as_number */
    NULL,                         /* tp_as_sequence */
    NULL,                         /* tp_as_mapping */
    NULL,                         /* tp_hash */
    NULL,                         /* tp_call */
    NULL,                         /* tp_str */
    NULL,                         /* tp_getattro */
    NULL,                         /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC, /* tp_flags */
    NULL,                         /* tp_doc */
    reinterpret_cast<traverseproc>(&obj_Camera_traverse), /* tp_traverse */
    reinterpret_cast<inquiry>(&obj_Camera_clear), /* tp_clear */
    NULL,                         /* tp_richcompare */
    offsetof(repr::camera_obj,weaklist), /* tp_weaklistoffset */
    NULL,                         /* tp_iter */
    NULL,                         /* tp_iternext */
    obj_Camera_methods,           /* tp_methods */
    NULL,                         /* tp_members */
    obj_Camera_getset,            /* tp_getset */
    NULL,                         /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    offsetof(repr::camera_obj,idict),   /* tp_dictoffset */
    reinterpret_cast<initproc>(&obj_Camera_init), /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_Camera_new               /* tp_new */
};

Py_ssize_t obj_CameraAxes___sequence_len__(obj_CameraAxes *self) {
    return self->base.length();
}


PyObject *obj_CameraAxes___sequence_getitem__(obj_CameraAxes *self,Py_ssize_t index) {
    try {
        return to_pyobject(self->base.sequence_getitem(index));
    } PY_EXCEPT_HANDLERS(NULL)
}


int obj_CameraAxes___sequence_setitem__(obj_CameraAxes *self,Py_ssize_t index,PyObject *arg) {
    try {
        self->base.sequence_setitem(index,get_base<n_vector>(arg));
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
}


PySequenceMethods obj_CameraAxes_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_CameraAxes___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_CameraAxes___sequence_getitem__),
    NULL,
    reinterpret_cast<ssizeobjargproc>(&obj_CameraAxes___sequence_setitem__),
    NULL,
    NULL,
    NULL,
    NULL
};

int obj_CameraAxes_traverse(obj_CameraAxes *self,visitproc visit,void *arg) {
    int ret = self->base.gc_traverse(visit,arg);
    if(ret) return ret;

    return 0;
}

PyObject *obj_CameraAxes_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    PyErr_SetString(PyExc_TypeError,"The CameraAxes type cannot be instantiated directly");
    return NULL;
}

PyTypeObject obj_CameraAxes::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".CameraAxes", /* tp_name */
    sizeof(obj_CameraAxes), /* tp_basicsize */
    0,                         /* tp_itemsize */
    &destructor_dealloc<obj_CameraAxes>, /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL, /* tp_compare */
    NULL, /* tp_repr */
    NULL, /* tp_as_number */
    &obj_CameraAxes_sequence_methods, /* tp_as_sequence */
    NULL, /* tp_as_mapping */
    NULL, /* tp_hash */
    NULL, /* tp_call */
    NULL, /* tp_str */
    NULL, /* tp_getattro */
    NULL, /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC, /* tp_flags */
    NULL, /* tp_doc */
    reinterpret_cast<traverseproc>(&obj_CameraAxes_traverse), /* tp_traverse */
    NULL, /* tp_clear */
    NULL, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    NULL, /* tp_iter */
    NULL, /* tp_iternext */
    NULL, /* tp_methods */
    NULL, /* tp_members */
    NULL, /* tp_getset */
    NULL, /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    0, /* tp_dictoffset */
    NULL, /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_CameraAxes_new /* tp_new */
};

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
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
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
                && PyLong_Check(PyTuple_GET_ITEM(args,0))
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
        get_arg ga(args,kwds,names,"rotation");
        repr::vector_obj::ref_type a = get_base<n_vector>(ga(true));
        repr::vector_obj::ref_type b = get_base<n_vector>(ga(true));
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

PyMethodDef obj_Matrix_methods[] = {
    {"scale",reinterpret_cast<PyCFunction>(&obj_Matrix_scale),METH_VARARGS|METH_STATIC,NULL},
    {"values",reinterpret_cast<PyCFunction>(&obj_Matrix_values),METH_NOARGS,"All the matrix elements as a flat sequence"},
    {"rotation",reinterpret_cast<PyCFunction>(&obj_Matrix_rotation),METH_VARARGS|METH_KEYWORDS|METH_STATIC,NULL},
    {"identity",reinterpret_cast<PyCFunction>(&obj_Matrix_identity),METH_O|METH_STATIC,NULL},
    {NULL}
};

void copy_row(repr::matrix_obj *m,py::object values,int row,int len) {
    sized_iter itr(values,len);
    for(int col=0; col<len; ++col) {
        m->cast_base()[row][col] = from_pyobject<REAL>(itr.next());
    }
    itr.finished();
}

PyObject *obj_Matrix_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dimension","values"};
        get_arg ga(args,kwds,names,"__new__");
        int dimension = from_pyobject<int>(ga(true));
        py::object values(py::borrowed_ref(ga(true)));
        ga.finished();

        repr::matrix_obj *ptr = reinterpret_cast<repr::matrix_obj*>(py::check_obj(type->tp_alloc(
            type,
            check_dimension(dimension) * dimension)));
        
        sized_iter itr(values,0);
        py::object item = itr.next();
        if(PyNumber_Check(item.ref())) {
            itr.expected_len = dimension * dimension;
            
            ptr->cast_base()[0][0] = from_pyobject<REAL>(item);
            for(int i=1; i<dimension*dimension; ++i) {
                /* there are no gaps between the rows, so we can treat
                   _matrix<T>::operator[](0) as a pointer to a flattened array
                   of all the elements */
                ptr->cast_base()[0][i] = from_pyobject<REAL>(itr.next());
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

PyTypeObject matrix_obj_base::pytype = {
    PyVarObject_HEAD_INIT(NULL,0)
    MODULE_STR ".Matrix", /* tp_name */
    sizeof(repr::matrix_obj) - repr::matrix_obj::item_size, /* tp_basicsize */
    repr::matrix_obj::item_size,                         /* tp_itemsize */
    NULL, /* tp_dealloc */
    NULL,                         /* tp_print */
    NULL,                         /* tp_getattr */
    NULL,                         /* tp_setattr */
    NULL, /* tp_compare */
    NULL, /* tp_repr */
    &obj_Matrix_number_methods, /* tp_as_number */
    &obj_Matrix_sequence_methods, /* tp_as_sequence */
    NULL, /* tp_as_mapping */
    NULL, /* tp_hash */
    NULL, /* tp_call */
    NULL, /* tp_str */
    NULL, /* tp_getattro */
    NULL, /* tp_setattro */
    NULL,                         /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES, /* tp_flags */
    NULL, /* tp_doc */
    NULL, /* tp_traverse */
    NULL, /* tp_clear */
    NULL, /* tp_richcompare */
    0, /* tp_weaklistoffset */
    NULL, /* tp_iter */
    NULL, /* tp_iternext */
    obj_Matrix_methods, /* tp_methods */
    NULL, /* tp_members */
    NULL, /* tp_getset */
    NULL, /* tp_base */
    NULL,                         /* tp_dict */
    NULL,                         /* tp_descr_get */
    NULL,                         /* tp_descr_set */
    0, /* tp_dictoffset */
    NULL, /* tp_init */
    NULL,                         /* tp_alloc */
    &obj_Matrix_new /* tp_new */
};


PyMethodDef func_table[] = {
    {NULL}
};


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
        if(!PyType_CheckExact(scene.ref())) THROW_PYERR_STRING(TypeError,"render.Scene is supposed to be a class")
        obj_BoxScene::pytype.tp_base = reinterpret_cast<PyTypeObject*>(scene.ref());
    } PY_EXCEPT_HANDLERS(INIT_ERR_VAL)
#endif

    if(UNLIKELY(PyType_Ready(&obj_BoxScene::pytype) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(&repr::vector_obj::pytype) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(&obj_MatrixProxy::pytype) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(&repr::camera_obj::pytype) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(&obj_CameraAxes::pytype) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(&repr::matrix_obj::pytype) < 0)) return INIT_ERR_VAL;
#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3(MODULE_STR,func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;

    add_class(m,"BoxScene",&obj_BoxScene::pytype);
    add_class(m,"Vector",&repr::vector_obj::pytype);
    add_class(m,"MatrixProxy",&obj_MatrixProxy::pytype);
    add_class(m,"Camera",&repr::camera_obj::pytype);
    add_class(m,"CameraAxes",&obj_CameraAxes::pytype);
    add_class(m,"Matrix",&repr::matrix_obj::pytype);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}

