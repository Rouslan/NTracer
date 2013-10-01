
#ifndef py_common_hpp
#define py_common_hpp

#include <Python.h>
#include <new>
#include <limits>


#ifdef __GNUC__
    #define LIKELY(X) __builtin_expect(static_cast<bool>(X),1)
    #define UNLIKELY(X) __builtin_expect(static_cast<bool>(X),0)
#else
    #define LIKELY(X) X
    #define UNLIKELY(X) X
#endif

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__BEOS__)
    #define SHARED(RET) __declspec(dllexport) RET
#elif defined(__GNUC__) && __GNUC__ >= 4
    #define SHARED(RET) RET __attribute__((visibility("default")))
#else
    #define SHARED(RET) RET
#endif


#define PY_MEM_NEW_DELETE void *operator new(size_t s) {            \
        void *ptr = PyObject_Malloc(s);                             \
        if(!ptr) throw std::bad_alloc();                            \
        return ptr;                                                 \
    }                                                               \
    void operator delete(void *ptr) {                               \
        PyObject_Free(ptr);                                         \
    }

#define PY_MEM_GC_NEW_DELETE void *operator new(size_t s) {         \
        void *ptr = _PyObject_GC_Malloc(s);                         \
        if(!ptr) throw std::bad_alloc();                            \
        return ptr;                                                 \
    }                                                               \
    void operator delete(void *ptr) {                               \
        PyObject_GC_Del(ptr);                                       \
    }

#ifndef PyVarObject_HEAD_INIT
    #define PyVarObject_HEAD_INIT(type, size) \
        PyObject_HEAD_INIT(type) size,
#endif

#define PY_EXCEPT_HANDLERS(RET) catch(py_error_set&) {              \
        return RET;                                                 \
    } catch(std::bad_alloc&) {                                      \
        PyErr_NoMemory();                                           \
        return RET;                                                 \
    } catch(std::exception &e) {                                    \
        PyErr_SetString(PyExc_RuntimeError,e.what());               \
        return RET;                                                 \
    }


extern const char *no_delete_msg;
extern const char *not_init_msg;
extern const char *unspecified_err_msg;
extern const char *no_keywords_msg;
extern const char *init_on_derived_msg;
extern const char *not_implemented_msg;


/* when thrown, indicates that a PyErr_X function was already called with the
   details of the exception. As such, it carries no information of its own. */
struct py_error_set {
    py_error_set() { assert(PyErr_Occurred()); }
    void clear() { PyErr_Clear(); }
};

enum storage_mode {UNINITIALIZED = 0,CONTAINS,MANAGEDREF,MANAGEDPTR,UNMANAGEDREF};


// checks that min <= x <= max and raises an exception otherwise
long narrow(long x,long max,long min);

template<typename T> inline T py_to_xint(PyObject *po);


#if PY_MAJOR_VERSION >= 3
    #define _compat_Int_FromLong PyLong_FromLong
    #define Py_TPFLAGS_CHECKTYPES 0

    #define PYBYTES(X) PyBytes_ ## X
    #define PYSTR(X) PyUnicode_ ## X
#else
    #define _compat_Int_FromLong PyInt_FromLong

    #define PYBYTES(X) PyString_ ## X
    #define PYSTR(X) PyString_ ## X
#endif

inline PyObject *to_pyobject(short x) {
    return _compat_Int_FromLong(x);
}

inline PyObject *to_pyobject(unsigned short x) {
    return _compat_Int_FromLong(x);
}

inline PyObject *to_pyobject(long x) {
    return _compat_Int_FromLong(x);
}

inline PyObject *to_pyobject(unsigned long x) {
    return PyLong_FromUnsignedLong(x);
}

#ifdef HAVE_LONG_LONG
inline PyObject *to_pyobject(long long x) {
    return PyLong_FromLongLong(x);
}

inline PyObject *to_pyobject(unsigned long long x) {
    return PyLong_FromUnsignedLongLong(x);
}
#endif

#if INT_MAX != LONG_MAX && INT_MAX != SHORT_MAX
inline PyObject *to_pyobject(int x) {
    return _compat_Int_FromLong(x);
}

inline PyObject *to_pyobject(unsigned int x) {
    return _compat_Int_FromLong(x);
}
#endif

inline PyObject *to_pyobject(bool x) {
    PyObject *r = x ? Py_True : Py_False;
    Py_INCREF(r);
    return r;
}

inline PyObject *to_pyobject(float x) {
    return PyFloat_FromDouble(x);
}

inline PyObject *to_pyobject(double x) {
    return PyFloat_FromDouble(x);
}

/*inline PyObject *to_pyobject(const char *x) {
    return PyString_FromString(x);
}*/

inline PyObject *to_pyobject(PyObject *x) {
    Py_INCREF(x);
    return x;
}


template<typename T> inline T from_pyobject(PyObject *o);


template<> inline short from_pyobject<short>(PyObject *o) {
    return py_to_xint<short>(o);
}

template<> inline unsigned short from_pyobject<unsigned short>(PyObject *o) {
    return py_to_xint<unsigned short>(o);
}

template<> inline long from_pyobject<long>(PyObject *o) {
#if PY_MAJOR_VERSION >= 3
    long r = PyLong_AsLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
#else
    long r = PyInt_AsLong(o);
    if(UNLIKELY(r == -1 && PyErr_Occurred())) throw py_error_set();
    return r;
#endif
}

template<> inline unsigned long from_pyobject<unsigned long>(PyObject *o) {
    unsigned long r = PyLong_AsUnsignedLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}

#ifdef HAVE_LONG_LONG
template<> inline long long from_pyobject<long long>(PyObject *o) {
    long long r = PyLong_AsLongLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}

template<> inline unsigned long long from_pyobject<unsigned long long>(PyObject *o) {
    unsigned long long r = PyLong_AsUnsignedLongLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}
#endif

#if INT_MAX != LONG_MAX && INT_MAX != SHORT_MAX
template<> inline int from_pyobject<int>(PyObject *o) {
    return static_cast<int>(from_pyobject<long>(o));
}

template<> inline unsigned int from_pyobject<unsigned int>(PyObject *o) {
    return static_cast<unsigned int>(from_pyobject<long>(o));
}
#endif

template<> inline bool from_pyobject<bool>(PyObject *o) {
    return static_cast<bool>(PyObject_IsTrue(o));
}

template<> inline double from_pyobject<double>(PyObject *o) {
    double r = PyFloat_AsDouble(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}

template<> inline float from_pyobject<float>(PyObject *o) {
    return static_cast<float>(from_pyobject<double>(o));
}

/*template<> inline const char *from_pyobject<const char*>(PyObject *o) {
    return PyString_AsString(o);
}*/

template<typename T> inline T py_to_xint(PyObject *o) {
    static_assert(std::numeric_limits<long>::max() >= std::numeric_limits<T>::max(),"py_to_xint will truncate T");
    return static_cast<T>(narrow(from_pyobject<long>(o),std::numeric_limits<T>::max(),std::numeric_limits<T>::min()));
}



template<typename T> struct wrapped_type;

/* When invariable_storage<T>::value is true, the location of T inside of its
   wrapped type will always be the same, thus an instance of T can be accessed
   from a PyObject pointer using
   reinterpret_cast<wrapped_type<T>::type*>(pointer)->base. However, the
   instance will not necessarily be initialized. Call get_base at least once to
   ensure that it is (once initialized, it can never be uninitialized). */
template<typename T> struct invariable_storage {
    static constexpr bool value = false;
};

template<typename T> auto get_base(PyObject *o) -> decltype(reinterpret_cast<typename wrapped_type<T>::type*>(o)->get_base()) {
    if(UNLIKELY(!PyObject_TypeCheck(o,&wrapped_type<T>::type::pytype))) {
        PyErr_Format(PyExc_TypeError,"object is not an instance of %s",wrapped_type<T>::type::pytype.tp_name);
        throw py_error_set();
    }
    return reinterpret_cast<typename wrapped_type<T>::type*>(o)->get_base();
}



struct get_arg {
    PyObject *args, *kwds;
    const char **names;
    const char *fname;
    Py_ssize_t arg_index, tcount, kcount;
    
    get_arg(PyObject *args,PyObject *kwds,Py_ssize_t arg_len,const char **names=NULL,const char *fname=NULL);
    template<int N> get_arg(PyObject *args,PyObject *kwds,const char *(&names)[N],const char *fname=NULL) : get_arg(args,kwds,N,names,fname) {}
    
    PyObject *operator()(bool required);
    void finished();
};

void NoSuchOverload(PyObject *args);


inline void add_class(PyObject *module,const char *name,PyTypeObject *type) {
    Py_INCREF(type);
    PyModule_AddObject(module,name,reinterpret_cast<PyObject*>(type));
}

#endif
