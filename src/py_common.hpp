
#ifndef py_common_hpp
#define py_common_hpp

/* needs to be included before Python.h to prevent an erroneous redefinition
   under MinGW */
#include <cmath>

#include <Python.h>
#include <structmember.h>
#include <new>
#include <limits>
#include <utility>
#include <tuple>

#include "compatibility.hpp"
#include "simd.hpp"


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

#define PY_EXCEPT_HANDLERS(RET) catch(py_error_set&) {              \
        return RET;                                                 \
    } catch(std::bad_alloc&) {                                      \
        PyErr_NoMemory();                                           \
        return RET;                                                 \
    } catch(std::exception &e) {                                    \
        PyErr_SetString(PyExc_RuntimeError,e.what());               \
        return RET;                                                 \
    }


#define OBJ_GETTER(T,EXPR) [](PyObject *obj_self,void*) -> PyObject* {  \
    try {                                                               \
        auto self = reinterpret_cast<T*>(obj_self);                     \
        return to_pyobject(EXPR);                                       \
    } PY_EXCEPT_HANDLERS(NULL)                                          \
}

#define _OBJ_SETTER(CONVERT,T,EXPR) [](PyObject *obj_self,PyObject *arg,void*) -> int { \
    try {                                                                      \
        setter_no_delete(arg);                                                 \
        auto self = reinterpret_cast<T*>(obj_self);                            \
        EXPR = CONVERT<std::decay_t<decltype(EXPR)>>(arg);        \
        return 0;                                                              \
    } PY_EXCEPT_HANDLERS(-1)                                                   \
}

#define OBJ_SETTER(T,EXPR) _OBJ_SETTER(from_pyobject,T,EXPR)
#define OBJ_BASE_SETTER(T,EXPR) _OBJ_SETTER(get_base,T,EXPR)


// this value is taken from Python/pyarena.c of the CPython source
const size_t PYOBJECT_ALIGNMENT = 8;


extern const char *no_delete_msg;
extern const char *not_init_msg;
extern const char *unspecified_err_msg;
extern const char *no_keywords_msg;
extern const char *init_on_derived_msg;
extern const char *not_implemented_msg;


constexpr size_t aligned(size_t size,size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}

template<typename T> struct range {
    T begin() const { return _begin; }
    T end() const { return _end; }

    range(T begin,T end) : _begin(begin), _end(end) {}

private:
    const T _begin;
    const T _end;
};

template<typename T> range<T> make_range(T begin,T end) {
    return range<T>(begin,end);
}


/* when thrown, indicates that a PyErr_X function was already called with the
   details of the exception. As such, it carries no information of its own. */
struct py_error_set {
    py_error_set() { assert(PyErr_Occurred()); }
    void clear() { PyErr_Clear(); }
};

enum storage_mode {UNINITIALIZED = 0,CONTAINS,INDIRECT};


// checks that min <= x <= max and raises an exception otherwise
long narrow(long x,long max,long min);

template<typename T> inline T py_to_xint(PyObject *po);


inline PyObject *to_pyobject(short x) {
    return PyLong_FromLong(x);
}

inline PyObject *to_pyobject(unsigned short x) {
    return PyLong_FromLong(x);
}

inline PyObject *to_pyobject(long x) {
    return PyLong_FromLong(x);
}

inline PyObject *to_pyobject(unsigned long x) {
    return PyLong_FromUnsignedLong(x);
}

inline PyObject *to_pyobject(long long x) {
    return PyLong_FromLongLong(x);
}

inline PyObject *to_pyobject(unsigned long long x) {
    return PyLong_FromUnsignedLongLong(x);
}

inline PyObject *to_pyobject(int x) {
    return PyLong_FromLong(x);
}

inline PyObject *to_pyobject(unsigned int x) {
#if INT_MAX < LONG_MAX
    return PyLong_FromLong(x);
#else
    return PyLong_FromUnsignedLong(x);
#endif
}

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

inline PyObject *to_pyobject(const char *x) {
    return PyUnicode_FromString(x);
}

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
    long r = PyLong_AsLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}

template<> inline unsigned long from_pyobject<unsigned long>(PyObject *o) {
    unsigned long r = PyLong_AsUnsignedLong(o);
    if(UNLIKELY(PyErr_Occurred())) throw py_error_set();
    return r;
}

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


template<> inline int from_pyobject<int>(PyObject *o) {
#if INT_MAX < LONG_MAX
    return py_to_xint<int>(o);
#else
    return from_pyobject<long>(o);
#endif
}

template<> inline unsigned int from_pyobject<unsigned int>(PyObject *o) {
#if INT_MAX < LONG_MAX
    return py_to_xint<unsigned int>(o);
#else
    return from_pyobject<unsigned long>(o);
#endif
}

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

template<> inline PyObject *from_pyobject<PyObject*>(PyObject *o) {
    return o;
}

template<typename T> inline T py_to_xint(PyObject *o) {
    static_assert(std::numeric_limits<long>::max() >= std::numeric_limits<T>::max(),"py_to_xint will truncate T");
    return static_cast<T>(narrow(from_pyobject<long>(o),std::numeric_limits<T>::max(),std::numeric_limits<T>::min()));
}


template<typename T> struct member_macro {};
#define MEMBER_MACRO(T,M) template<> struct member_macro<T> { static const int value=M; }
MEMBER_MACRO(short,T_SHORT);
MEMBER_MACRO(unsigned short,T_USHORT);
MEMBER_MACRO(int,T_INT);
MEMBER_MACRO(unsigned int,T_UINT);
MEMBER_MACRO(long,T_LONG);
MEMBER_MACRO(unsigned long,T_ULONG);
MEMBER_MACRO(float,T_FLOAT);
MEMBER_MACRO(double,T_DOUBLE);
MEMBER_MACRO(long long,T_LONGLONG);
MEMBER_MACRO(unsigned long long,T_ULONGLONG);
#undef MEMBER_MACRO



template<typename T> struct _wrapped_type {};
template<typename T> using wrapped_type = typename _wrapped_type<T>::type;


/* When invariable_storage<T>::value is true, the location of T inside of its
   wrapped type will always be the same, thus an instance of T can be accessed
   from a PyObject pointer using
   reinterpret_cast<wrapped_type<T>::type*>(pointer)->base. However, the
   instance will not necessarily be initialized. Call get_base at least once to
   verify that it is (once initialized, it can never be uninitialized). */
template<typename T> struct invariable_storage {
    static constexpr bool value = false;
};

template<typename T,typename=wrapped_type<std::decay_t<T>>> PyObject *to_pyobject(T &&x) {
    return reinterpret_cast<PyObject*>(new wrapped_type<std::decay_t<T>>(std::forward<T>(x)));
}

template<typename T> T *checked_py_cast(PyObject *o) {
    if(UNLIKELY(!PyObject_TypeCheck(o,T::pytype()))) {
        PyErr_Format(PyExc_TypeError,"object is not an instance of %s",T::pytype()->tp_name);
        throw py_error_set();
    }
    return reinterpret_cast<T*>(o);
}

template<typename T> T &get_base(PyObject *o) {
    return checked_py_cast<wrapped_type<T>>(o)->get_base();
}

template<typename T> T *get_base_if_is_type(PyObject *o) {
    return PyObject_TypeCheck(o,wrapped_type<T>::pytype()) ? &reinterpret_cast<wrapped_type<T>*>(o)->get_base() : nullptr;
}


template<typename T,bool trivial=std::is_trivially_destructible_v<T>> struct destructor_dealloc {
    static void _function(PyObject *self) {
        reinterpret_cast<T*>(self)->~T();
        Py_TYPE(self)->tp_free(self);
    }

    static constexpr void (*value)(PyObject*) = &_function;
};

template<typename T> struct destructor_dealloc<T,true> {
    static constexpr void (*value)(PyObject*) = nullptr;
};


template<typename T> int traverse_idict(PyObject *self,visitproc visit,void *arg) {
    Py_VISIT(reinterpret_cast<T*>(self)->idict);

    return 0;
}

template<typename T> int clear_idict(PyObject *self) {
    Py_CLEAR(reinterpret_cast<T*>(self)->idict);

    return 0;
}


template<typename T,typename Base,bool AllowIndirect=false,bool InPlace=(alignof(T) <= PYOBJECT_ALIGNMENT)> struct simple_py_wrapper : Base {
    T base;
    PY_MEM_NEW_DELETE
    template<typename... Args> simple_py_wrapper(Args&&... args) : base(std::forward<Args>(args)...) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
    }

    T &alloc_base() {
        return base;
    }

    T &cast_base() { return base; }
    T &get_base() { return base; }
};

template<typename T,typename Base> struct simple_py_wrapper<T,Base,false,false> : Base {
    T *base;
    PY_MEM_NEW_DELETE
    template<typename... Args> simple_py_wrapper(Args&&... args) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
        new(&alloc_base()) T(std::forward<Args>(args)...);
    }
    ~simple_py_wrapper() {
        if(base) {
            base->~T();
            simd::aligned_free(base);
        }
    }

    T &alloc_base() {
        assert(!base);
        base = reinterpret_cast<T*>(simd::aligned_alloc(alignof(T),sizeof(T)));
        return *base;
    }

    T &cast_base() { return *base; }
    T &get_base() { return *base; }
};

template<typename T,typename Base> struct simple_py_wrapper<T,Base,true,true> : Base {
    storage_mode mode;
    union direct_indirect_data {
        T direct;
        struct {
            T *ptr;
            PyObject *owner;
        } indirect;

        direct_indirect_data() {}
        ~direct_indirect_data() {}
    } base;

    PY_MEM_NEW_DELETE

    template<typename... Args> simple_py_wrapper(Args&&... args) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
        new(&base.direct) T(std::forward<Args>(args)...);
        mode = CONTAINS;
    }

    simple_py_wrapper(PyObject *owner,T &base) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
        this->base.indirect.ptr = &base;
        this->base.indirect.owner = owner;
        Py_INCREF(owner);
        mode = INDIRECT;
    }

    ~simple_py_wrapper() {
        if(mode == CONTAINS) base.direct.~T();
        else if(mode == INDIRECT) {
            Py_DECREF(base.indirect.owner);
        }
    }

    T &alloc_base() {
        assert(mode == UNINITIALIZED);
        mode = CONTAINS;
        return base.direct;
    }

    T &cast_base() {
        if(mode == CONTAINS) return base.direct;

        assert(mode == INDIRECT);
        return *base.indirect.ptr;
    }
    T &get_base() { return cast_base(); }
};

template<typename T,typename Base> struct simple_py_wrapper<T,Base,true,false> : Base {
    storage_mode mode;
    T *base;
    PyObject *owner;

    PY_MEM_NEW_DELETE

    template<typename... Args> simple_py_wrapper(Args&&... args) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
        new(&alloc_base()) T(std::forward<Args>(args)...);
        mode = CONTAINS;
    }

    simple_py_wrapper(PyObject *owner,T &base) : mode(INDIRECT), base(&base), owner(owner) {
        Py_INCREF(owner);
        PyObject_Init(reinterpret_cast<PyObject*>(this),Base::pytype());
    }

    ~simple_py_wrapper() {
        if(mode == CONTAINS) {
            base->~T();
            simd::aligned_free(base);
        } else if(mode == INDIRECT) {
            Py_DECREF(owner);
        }
    }

    T &alloc_base() {
        assert(!base);
        base = reinterpret_cast<T*>(simd::aligned_alloc(alignof(T),sizeof(T)));
        mode = CONTAINS;
        return *base;
    }

    T &cast_base() { return *base; }
    T &get_base() { return *base; }
};

#define CONTAINED_PYTYPE_DEF \
    static PyTypeObject _pytype; \
    static constexpr PyTypeObject *pytype() { return &_pytype; }


template<typename T> struct parameter {
    typedef T type;

    const char *name;
    explicit constexpr parameter(const char *name) : name(name) {}
};

template<typename T> struct opt_parameter {
    typedef T type;

    const char *name;
    const T def_value;
    constexpr opt_parameter(const char *name,T def_value) : name(name), def_value(def_value) {}
};

class get_arg {
    struct get_arg_base {
        PyObject *args, *kwds;
        const char **names;
        const char *fname;
        Py_ssize_t kcount;

        get_arg_base(PyObject *args,PyObject *kwds,Py_ssize_t arg_len,const char **names,const char *fname);

        PyObject *operator()(size_t i,bool required);
        void finished();
    } base;

    template<typename T> static inline T as_param(const parameter<T> &p,size_t index,get_arg_base &ga) {
        return from_pyobject<T>(ga(index,true));
    }

    template<typename T> static inline T as_param(const opt_parameter<T> &p,size_t index,get_arg_base &ga) {
        PyObject *tmp = ga(index,false);
        return tmp ? from_pyobject<T>(tmp) : p.def_value;
    }

    template<typename... P,size_t... Indexes> static inline std::tuple<typename P::type...> _get_args(std::index_sequence<Indexes...>,const char *fname,PyObject *args,PyObject *kwds,const P&... params) {
        const char *arg_names[] = {params.name...};
        get_arg_base ga(args,kwds,sizeof...(P),arg_names,fname);
        std::tuple<typename P::type...> r{as_param(params,Indexes,ga)...};
        ga.finished();
        return r;
    }

public:
    Py_ssize_t arg_index;

    get_arg(PyObject *args,PyObject *kwds,Py_ssize_t arg_len,const char **names=NULL,const char *fname=NULL) : base(args,kwds,arg_len,names,fname), arg_index(0) {}
    template<int N> get_arg(PyObject *args,PyObject *kwds,const char *(&names)[N],const char *fname=NULL) : get_arg(args,kwds,N,names,fname) {}

    PyObject *operator()(bool required) { return base(arg_index++,required); }
    void finished() { base.finished(); }

    template<typename... P> static std::tuple<typename P::type...> get_args(const char *fname,PyObject *args,PyObject *kwds,const P&... params) {
        return _get_args<P...>(std::make_index_sequence<sizeof...(P)>(),fname,args,kwds,params...);
    }
};

constexpr parameter<PyObject*> param(const char *name) { return parameter<PyObject*>(name); }
template<typename T> constexpr parameter<T> param(const char *name) { return parameter<T>(name); }
constexpr opt_parameter<PyObject*> param(const char *name,PyObject *def_value) { return {name,def_value}; }
template<typename T> constexpr opt_parameter<T> param(const char *name,T def_value) { return {name,def_value}; }


void NoSuchOverload(PyObject *args);

inline void setter_no_delete(PyObject *arg) {
    if(!arg) {
        PyErr_SetString(PyExc_TypeError,no_delete_msg);
        throw py_error_set();
    }
}


PyObject *immutable_copy_func(PyObject *self,PyObject*);
constexpr PyMethodDef immutable_copy = {"__copy__",&immutable_copy_func,METH_NOARGS,NULL};
constexpr PyMethodDef immutable_deepcopy = {"__deepcopy__",&immutable_copy_func,METH_O,NULL};


inline void add_class(PyObject *module,const char *name,PyTypeObject *type) {
    Py_INCREF(type);
    PyModule_AddObject(module,name,reinterpret_cast<PyObject*>(type));
}

#endif
