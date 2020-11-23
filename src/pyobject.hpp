#ifndef pyobject_hpp
#define pyobject_hpp

#include "py_common.hpp"

#include <algorithm>
#include <iterator>
#include <structmember.h>


#define THROW_PYERR_STRING(exception,message) PyErr_SetString(PyExc_##exception,message), throw py_error_set()


namespace py {
    /* We can't use C++ inheritance with PyObject so this is provided to
       indicate which classes' pointers can be safely cast to PyObject* */
    struct pyobj_subclass {};

    struct pyobj_ptr {
        PyObject *ptr;
        pyobj_ptr() {}
        pyobj_ptr(PyObject *ptr) : ptr(ptr) {}
        pyobj_ptr(pyobj_subclass *ptr) : ptr(reinterpret_cast<PyObject*>(ptr)) {}

        operator PyObject*() const { return ptr; }
        operator bool() const { return ptr != nullptr; }
    };

    inline PyObject *ref(pyobj_ptr o) { return o; }


    // Exception-safe alternative to Py_BEGIN_ALLOW_THREADS and Py_END_ALLOW_THREADS
    class allow_threads {
#ifdef WITH_THREAD
        PyThreadState *save;
    public:
        allow_threads() : save(PyEval_SaveThread()) {}
        ~allow_threads() { PyEval_RestoreThread(save); }
#endif
    };

    class acquire_gil {
        PyGILState_STATE gilstate;
    public:
        acquire_gil() : gilstate(PyGILState_Ensure()) {}
        ~acquire_gil() { PyGILState_Release(gilstate); }
    };

    inline PyObject *check_obj(PyObject *o) {
        if(UNLIKELY(!o)) throw py_error_set();
        return o;
    }

    inline PyObject *incref(pyobj_ptr o) {
        assert(o);
        Py_INCREF(o.ptr);
        return o;
    }

    inline PyObject *xincref(pyobj_ptr o) {
        Py_XINCREF(o.ptr);
        return o;
    }

    /* wrapping it in a function prevents evaluating an expression more than
       once due to macro expansion */
    inline void decref(pyobj_ptr o) {
        Py_DECREF(o.ptr);
    }

    /* wrapping it in a function prevents evaluating an expression more than
       once due to macro expansion */
    inline void xdecref(pyobj_ptr o) {
        Py_XDECREF(o.ptr);
    }

    struct borrowed_ref {
        PyObject *_ptr;
        explicit borrowed_ref(pyobj_ptr ptr) : _ptr(ptr) {}
    };

    struct new_ref {
        PyObject *_ptr;
        explicit new_ref(pyobj_ptr ptr) : _ptr(ptr) {}
    };

    // alias for classes to use, that have new_ref function
    typedef new_ref _new_ref;

    inline new_ref check_new_ref(pyobj_ptr o) {
        return new_ref(check_obj(o));
    }

    inline void *malloc(size_t size) {
        void *r = PyMem_Malloc(size);
        if(UNLIKELY(!r)) throw std::bad_alloc();
        return r;
    }

    inline void free(void *ptr) noexcept {
        PyMem_Free(ptr);
    }

    template<typename T> inline T *get_base_or_none(PyObject *o) {
        return o == Py_None ? NULL : &get_base<T>(o);
    }

    inline const char *typename_base(const char *name) {
        assert(name);

        while(true) {
            assert(*name);
            if(*name == '.') return name+1;
            ++name;
        }
    }



    class object;
    class object_attr_proxy;
    class object_item_proxy;
    class object_iterator;

    class _object_base {
    protected:
        PyObject *_ptr;

        void reset_new(PyObject *b) {
            // cyclic garbage collection safety
            PyObject *tmp = _ptr;
            _ptr = b;
            Py_DECREF(tmp);
        }
        void reset(PyObject *b) {
            reset_new(incref(b));
        }

        _object_base(PyObject *ptr) : _ptr(ptr) {}
        _object_base(borrowed_ref r) : _ptr(incref(r._ptr)) {}
        _object_base(py::new_ref r) : _ptr(r._ptr) { assert(_ptr); }
        _object_base(const _object_base &b) : _ptr(incref(b._ptr)) {}

        _object_base &operator=(const _object_base &b) {
            reset(b._ptr);
            return *this;
        }

        ~_object_base() {
            /* XDECREF is used instead of DECREF so that objects that were
               allocated but not fully initialized can contain _object_base
               instances and still be safely destroyed */
            Py_XDECREF(_ptr);
        }

        void swap(_object_base &b) {
            std::swap(_ptr,b._ptr);
        }

    #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
        template<typename... Args> PyObject *vectorcall(Args*... args) const {
            PyObject **argarray[] = {nullptr,args...};
            return check_new_ref(PyObject_Vectorcall(
                _ptr,
                argarray+1,
                sizeof...(Args) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                nullptr));
        }
    #endif

    public:
        explicit operator bool() const {
            return PyObject_IsTrue(_ptr);
        }

        PyObject *ref() const { return _ptr; }
        PyObject *new_ref() const { return incref(_ptr); }
        Py_ssize_t ref_count() const { return _ptr->ob_refcnt; }

        object_attr_proxy attr(const char *name) const;

        bool has_attr(const char *name) const { return PyObject_HasAttrString(_ptr,name); }
        bool has_attr(const _object_base &name) const { return PyObject_HasAttr(_ptr,name._ptr); }

        inline object operator()() const;
        template<typename Arg1> inline object operator()(const Arg1 &arg1) const;
        template<typename Arg1,typename Arg2,typename... Args> inline object operator()(const Arg1 &arg1,const Arg2 &arg2,const Args&... args) const;

#define OBJECT_OP(OP,PYOP) bool operator OP(const _object_base &b) const { return bool(PyObject_RichCompareBool(_ptr,b._ptr,PYOP)); }
        OBJECT_OP(==,Py_EQ)
        OBJECT_OP(!=,Py_NE)
        OBJECT_OP(<,Py_LT)
        OBJECT_OP(<=,Py_LE)
        OBJECT_OP(>,Py_GT)
        OBJECT_OP(>=,Py_GE)
#undef OBJECT_OP

        template<typename T> object_item_proxy at(T key) const;
        template<typename T> object_item_proxy operator[](T key) const;

        PyTypeObject *type() const {
            return Py_TYPE(_ptr);
        }

        int gc_traverse(visitproc visit,void *arg) const { return _ptr != Py_None ? (*visit)(_ptr,arg) : 0; }
        void gc_clear() { reset(Py_None); }
    };

    class object : public _object_base {
    public:
        object() : _object_base(borrowed_ref(Py_None)) {}
        object(borrowed_ref r) : _object_base(r) {}
        object(_new_ref r) : _object_base(r) {}
        object(const _object_base &b) : _object_base(b) {}

        object &operator=(const _object_base &b) {
            reset(b.ref());
            return *this;
        }

        object &operator=(borrowed_ref r) {
            reset(r._ptr);
            return *this;
        }
        object &operator=(py::new_ref r) {
            reset_new(r._ptr);
            return *this;
        }

        void swap(object &b) { _object_base::swap(b); }

        inline object_iterator begin() const;
        inline object_iterator end() const;
    };

    template<typename T> inline typename std::enable_if<!std::is_base_of<_object_base,typename std::decay<T>::type>::value,object>::type make_object(T &&x) {
        return object(new_ref(to_pyobject(std::forward<T>(x))));
    }

    inline object make_object(const _object_base &x) {
        return x;
    }

    class object_attr_proxy {
        friend class _object_base;
        friend void del(const object_attr_proxy &attr);

        PyObject *_ptr;
        const char *name;

        object_attr_proxy(PyObject *ptr,const char *name) : _ptr(ptr), name(name) {}

    #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
        template<typename... Args> PyObject *vectorcall(Args*... args) const {
            PyObject **argarray[] = {_ptr,args...};
            return check_new_ref(PyObject_VectorcallMethod(
                object(check_new_ref(PyUnicode_InternFromString(name))).ref(),
                argarray,
                (sizeof...(Args)+1) | PY_VECTORCALL_ARGUMENTS_OFFSET,
                nullptr));
        }
    #endif
    public:
        operator object() const { return check_new_ref(PyObject_GetAttrString(_ptr,name)); }

        object_attr_proxy &operator=(object val) {
            if(UNLIKELY(PyObject_SetAttrString(_ptr,name,val.ref()) == -1)) throw py_error_set();
            return *this;
        }

        // so object_a.attr("x") = object_b.attr("y") works as expected
        object_attr_proxy &operator=(const object_attr_proxy &val) {
            return operator=(static_cast<object>(val));
        }

        object operator()() const {
        #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
            return check_new_ref(PyObject_CallMethodNoArgs(
                _ptr,
                object(check_new_ref(PyUnicode_InternFromString(name))).ref()));
        #else
            return check_new_ref(PyObject_CallMethodObjArgs(
                _ptr,
                object(check_new_ref(PyUnicode_InternFromString(name))).ref(),
                0));
        #endif
        }

        template<typename Arg1> object operator()(const Arg1 &arg1) const {
        #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
            return check_new_ref(PyObject_CallMethodOneArg(
                _ptr,
                object(check_new_ref(PyUnicode_InternFromString(name))).ref(),
                make_object(arg1).ref()));
        #else
            return check_new_ref(PyObject_CallMethodObjArgs(
                _ptr,
                object(check_new_ref(PyUnicode_InternFromString(name))).ref(),
                make_object(arg1).ref(),
                0));
        #endif
        }

        template<typename Arg1,typename Arg2,typename... Args>
        object operator()(const Arg1 &arg1,const Arg2 &arg2,const Args&... args) const {
        #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
            return check_new_ref(vectorcall(
                make_object(arg1).ref(),
                make_object(arg2).ref(),
                make_object(args).ref()...));
        #else
            return check_new_ref(PyObject_CallMethodObjArgs(
                _ptr,
                object(check_new_ref(PyUnicode_InternFromString(name))).ref(),
                make_object(arg1).ref(),
                make_object(arg2).ref(),
                make_object(args).ref()...,
                0));
        #endif
        }
    };

    inline object_attr_proxy _object_base::attr(const char *name) const { return object_attr_proxy(_ptr,name); }

    inline object _object_base::operator()() const {
    #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
        return check_new_ref(PyObject_CallNoArgs(_ptr));
    #else
        return check_new_ref(PyObject_CallObject(_ptr,0));
    #endif
    }

    template<typename Arg1> inline object _object_base::operator()(const Arg1 &arg1) const {
    #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
        return check_new_ref(PyObject_CallOneArg(_ptr,make_object(arg1).ref()));
    #else
        return check_new_ref(PyObject_CallFunctionObjArgs(_ptr,make_object(arg1).ref(),0));
    #endif
    }

    template<typename Arg1,typename Arg2,typename... Args>
    inline object _object_base::operator()(const Arg1 &arg1,const Arg2 &arg2,const Args&... args) const {
    #if !defined(Py_LIMITED_API) && PY_VERSION_HEX >= 0x03090000
            return check_new_ref(vectorcall(
                make_object(arg1).ref(),
                make_object(arg2).ref(),
                make_object(args).ref()...));
    #else
        return check_new_ref(PyObject_CallFunctionObjArgs(_ptr,
            make_object(arg1).ref(),
            make_object(arg2).ref(),
            make_object(args).ref()...,
            0));
    #endif
    }

    inline void del(const object_attr_proxy &attr) {
        if(UNLIKELY(PyObject_DelAttrString(attr._ptr,attr.name) == -1)) throw py_error_set();
    }


    class object_item_proxy {
        friend class _object_base;
        friend void del(const object_item_proxy &item);

        PyObject *_ptr;
        PyObject *key;

        object_item_proxy(PyObject *ptr,PyObject * key) : _ptr(ptr), key(key) {}
    public:
        object_item_proxy(const object_item_proxy &b) : _ptr(b._ptr), key(incref(b.key)) {}
        ~object_item_proxy() {
            Py_DECREF(key);
        }

        operator object() const { return check_new_ref(PyObject_GetItem(_ptr,key)); }

        object_item_proxy &operator=(object val) {
            if(UNLIKELY(PyObject_SetItem(_ptr,key,val.ref()) == -1)) throw py_error_set();
            return *this;
        }

        // so object_a[x] = object_b[y] works as expected
        object_item_proxy &operator=(const object_item_proxy &val) {
            return operator=(static_cast<object>(val));
        }
    };

    template<typename T> inline object_item_proxy _object_base::at(T key) const {
        return object_item_proxy(_ptr,to_pyobject(key));
    }

    template<typename T> inline object_item_proxy _object_base::operator[](T key) const {
        return at<T>(key);
    }

    inline void del(const object_item_proxy &item) {
        if(UNLIKELY(PyObject_DelItem(item._ptr,item.key) == -1)) throw py_error_set();
    }
}

inline PyObject *to_pyobject(py::new_ref r) {
    return r._ptr;
}

inline PyObject *to_pyobject(py::borrowed_ref r) {
    return py::incref(r._ptr);
}

inline PyObject *to_pyobject(const py::object &x) {
    return x.new_ref();
}

namespace py {
    template<typename T> class nullable {
        PyObject *_ptr;

        void reset(PyObject *b) {
            // cyclic garbage collection safety
            PyObject *tmp = _ptr;
            _ptr = b;
            Py_XDECREF(tmp);
        }
    public:
        nullable() : _ptr(nullptr) {}
        nullable(const nullable<T> &b) : _ptr(b._ptr) { Py_XINCREF(_ptr); }
        nullable(const T &b) : _ptr(b.new_ref()) {}
        nullable(borrowed_ref r) : _ptr(r._ptr) { Py_XINCREF(_ptr); }
        nullable(new_ref r) : _ptr(r._ptr) {}

        nullable &operator=(borrowed_ref r) {
            Py_XINCREF(r._ptr);
            reset(r._ptr);
            return *this;
        }

        nullable &operator=(new_ref r) {
            reset(r._ptr);
            return *this;
        }

        nullable &operator=(const nullable &b) {
            Py_XINCREF(b._ptr);
            reset(b._ptr);
            return *this;
        }
        nullable &operator=(const T &b) {
            Py_INCREF(b.ref());
            reset(b.ref());
            return *this;
        }

        ~nullable() {
            Py_XDECREF(_ptr);
        }

        operator bool() const { return _ptr != nullptr; }
        T operator*() const {
            assert(_ptr);
            return borrowed_ref(_ptr);
        }
        T operator->() const {
            assert(_ptr);
            return borrowed_ref(_ptr);
        }

        PyObject *ref() const { return _ptr; }

        void swap(nullable &b) {
            std::swap(_ptr,b._ptr);
        }

        int gc_traverse(visitproc visit,void *arg) const { return _ptr ? (*visit)(_ptr,arg) : 0; }
        void gc_clear() { reset(nullptr); }
    };

    class buffer_view {
        Py_buffer view;
    public:
        buffer_view(PyObject *obj,int flags) {
            if(PyObject_GetBuffer(obj,&view,flags)) throw py_error_set();
        }

        buffer_view(object obj,int flags) {
            if(PyObject_GetBuffer(obj.ref(),&view,flags)) throw py_error_set();
        }

        buffer_view(const buffer_view &b) = delete;
        buffer_view &operator=(const buffer_view &b) = delete;

        ~buffer_view() {
            PyBuffer_Release(&view);
        }

        void *buf() const { return view.buf; }
        Py_ssize_t len() const { return view.len; }
        int readonly() const { return view.readonly; }
        const char *format() const { return view.format; }
        int ndim() const { return view.ndim; }
        Py_ssize_t *shape() const { return view.shape; }
        Py_ssize_t *strides() const { return view.strides; }
        Py_ssize_t *suboffsets() const { return view.suboffsets; }
        Py_ssize_t itemsize() const { return view.itemsize; }
    };

    class tuple_iterator {
        PyObject **data;
    public:
        typedef object value_type;
        typedef Py_ssize_t difference_type;
        typedef object *pointer;
        typedef object &reference;
        typedef std::random_access_iterator_tag iterator_category;

        explicit tuple_iterator(PyObject **data) : data(data) {}

        object operator*() const {
            return borrowed_ref(*data);
        }

        tuple_iterator &operator++() {
            ++data;
            return *this;
        }
        tuple_iterator operator++(int) {
            tuple_iterator tmp = *this;
            ++data;
            return tmp;
        }
        bool operator==(tuple_iterator b) const {
            return data == b.data;
        }
        bool operator!=(tuple_iterator b) const {
            return data != b.data;
        }

        tuple_iterator operator+(Py_ssize_t b) const {
            return tuple_iterator(data + b);
        }
        tuple_iterator &operator+=(Py_ssize_t b) {
            data += b;
            return *this;
        }
        tuple_iterator operator-(Py_ssize_t b) const {
            return tuple_iterator(data - b);
        }
        Py_ssize_t operator-(tuple_iterator b) const {
            return static_cast<Py_ssize_t>(data - b.data);
        }
        tuple_iterator &operator-=(Py_ssize_t b) {
            data -= b;
            return *this;
        }
    };

    class tuple : public _object_base {
    public:
        tuple(borrowed_ref r) : _object_base(r) { assert(PyTuple_Check(r._ptr)); }
        tuple(_new_ref r) : _object_base(r) { assert(PyTuple_Check(r._ptr)); }
        explicit tuple(Py_ssize_t len) : _object_base(check_new_ref(PyTuple_New(len))) {}
        tuple(const tuple &b) : _object_base(b) {}
        explicit tuple(const _object_base &b) : _object_base(object(py::borrowed_ref(reinterpret_cast<PyObject*>(&PyTuple_Type)))(b)) {
            assert(PyTuple_Check(_ptr));
        }
        template<typename T> tuple(T start,T end) : tuple(std::distance(start,end)) {
            for(auto ti = &PyTuple_GET_ITEM(_ptr,0); start != end; ++start, ++ti) {
                *ti = to_pyobject(*start);
            }
        }
        tuple(std::initializer_list<object> ol) : tuple(std::begin(ol),std::end(ol)) {}

        tuple &operator=(const tuple &b) {
            reset(b._ptr);
            return *this;
        }

        void swap(tuple &b) { _object_base::swap(b); }

        object at(Py_ssize_t i) const { return borrowed_ref(check_obj(PyTuple_GetItem(_ptr,i))); }
        void set_unsafe(Py_ssize_t i,PyObject *item) const { PyTuple_SET_ITEM(_ptr,i,item); }
        object operator[](Py_ssize_t i) const { return borrowed_ref(PyTuple_GET_ITEM(_ptr,i)); }
        Py_ssize_t size() const { return PyTuple_GET_SIZE(_ptr); }

        tuple_iterator begin() const {
            return tuple_iterator(&PyTuple_GET_ITEM(_ptr,0));
        }

        tuple_iterator end() const {
            return tuple_iterator(&PyTuple_GET_ITEM(_ptr,size()));
        }
    };

    template<typename... Args> tuple make_tuple(Args&&... args) {
        return {make_object(std::forward<Args>(args))...};
    }


    class list_item_proxy {
        friend class list;
        friend void del(const list_item_proxy &item);

        PyObject *_ptr;
        Py_ssize_t index;

        list_item_proxy(PyObject *ptr,Py_ssize_t index) : _ptr(ptr), index(index) {}
    public:
        list_item_proxy(const list_item_proxy &b) : _ptr(b._ptr), index(b.index) {}

        operator object() const {
            return borrowed_ref(PyList_GET_ITEM(_ptr,index));
        }

        list_item_proxy &operator=(object val) {
            PyObject *oldval = PyList_GET_ITEM(_ptr,index);
            PyList_SET_ITEM(_ptr,index,val.new_ref());
            Py_DECREF(oldval);
            return *this;
        }

        // so object_a[x] = object_b[y] works as expected
        list_item_proxy &operator=(const list_item_proxy &val) {
            return operator=(static_cast<object>(val));
        }
    };

    class list : public _object_base {
    public:
        list(borrowed_ref r) : _object_base(r) { assert(PyList_Check(r._ptr)); }
        list(_new_ref r) : _object_base(r) { assert(PyList_Check(r._ptr)); }
        list() : _object_base(check_new_ref(PyList_New(0))) {}
        list(const list &b) : _object_base(b) {}
        explicit list(const _object_base &b) : _object_base(object(py::borrowed_ref(reinterpret_cast<PyObject*>(&PyList_Type)))(b)) {
            assert(PyList_Check(_ptr));
        }

        list &operator=(const list &b) {
            reset(b._ptr);
            return *this;
        }

        void swap(list &b) { _object_base::swap(b); }

        list_item_proxy operator[](Py_ssize_t index) const {
            assert(index >= 0 && index < size());
            return list_item_proxy(_ptr,index);
        }

        Py_ssize_t size() const { return PyList_GET_SIZE(_ptr); }

        void append(const object &item) {
            if(PyList_Append(_ptr,item.ref())) throw py_error_set();
        }
    };

    inline void del(const list_item_proxy &item) {
        if(PyList_SetSlice(item._ptr,item.index,item.index,NULL)) throw py_error_set();
    }

    class dict_item_proxy {
        friend class dict;
        friend void del(const dict_item_proxy &item);

        PyObject *_ptr;
        PyObject *key;

        dict_item_proxy(PyObject *ptr,PyObject *key) : _ptr(ptr), key(key) {}
    public:
        dict_item_proxy(const dict_item_proxy &b) : _ptr(b._ptr), key(incref(b.key)) {}
        ~dict_item_proxy() {
            Py_DECREF(key);
        }

        operator object() const {
            /* using mp_subscript because it sets the error for us if the key
               isn't found */
            PyMappingMethods *m = _ptr->ob_type->tp_as_mapping;
            assert(m && m->mp_subscript);
            PyObject *item = (*m->mp_subscript)(_ptr,key);
            if(!item) throw py_error_set();
            return new_ref(item);
        }

        dict_item_proxy &operator=(object val) {
            if(PyDict_SetItem(_ptr,key,val.ref())) throw py_error_set();
            return *this;
        }

        // so object_a[x] = object_b[y] works as expected
        dict_item_proxy &operator=(const dict_item_proxy &val) {
            return operator=(static_cast<object>(val));
        }
    };

    class dict : public _object_base {
    public:
        dict(borrowed_ref r) : _object_base(r) { assert(PyDict_Check(r._ptr)); }
        dict(_new_ref r) : _object_base(r) { assert(PyDict_Check(r._ptr)); }
        dict() : _object_base(check_new_ref(PyDict_New())) {}
        dict(const dict &b) : _object_base(b) {}

        dict &operator=(const dict &b) {
            reset(b._ptr);
            return *this;
        }

        void swap(dict &b) { _object_base::swap(b); }

        template<typename T> dict_item_proxy operator[](T key) const { return dict_item_proxy(_ptr,to_pyobject(key)); }
        Py_ssize_t size() const { return PyDict_Size(_ptr); }
        template<typename T> nullable<object> find(T key) const {
            PyObject *item = PyDict_GetItemWithError(_ptr,to_pyobject(key));
            if(!item && PyErr_Occurred()) throw py_error_set();
            return borrowed_ref(item);
        }

        dict copy(const dict &b) const {
            return _new_ref(PyDict_Copy(b._ptr));
        }
    };

    inline void del(const dict_item_proxy &attr) {
        if(PyDict_DelItem(attr._ptr,attr.key)) throw py_error_set();
    }


    class bytes : public _object_base {
    public:
        bytes(borrowed_ref r) : _object_base(r) { assert(PyBytes_Check(r._ptr)); }
        bytes(_new_ref r) : _object_base(r) { assert(PyBytes_Check(r._ptr)); }
        bytes(const char *str="") : _object_base(check_new_ref(PyBytes_FromString(str))) {}
        explicit bytes(Py_ssize_t s) : _object_base(check_new_ref(PyBytes_FromStringAndSize(nullptr,s))) {}
        bytes(const char *str,Py_ssize_t s) : _object_base(check_new_ref(PyBytes_FromStringAndSize(str,s))) {}
        bytes(const bytes &b) : _object_base(b) {}

        bytes &operator=(const bytes &b) {
            reset(b._ptr);
            return *this;
        }

        void swap(bytes &b) { _object_base::swap(b); }

        //bytes &operator+=(const bytes &b);

        Py_ssize_t size() const { return PyBytes_GET_SIZE(_ptr); }

        char *data() { return PyBytes_AS_STRING(_ptr); }
        const char *data() const { return PyBytes_AS_STRING(_ptr); }
    };


    class set_base : public _object_base {
    public:
        Py_ssize_t size() const { return PySet_GET_SIZE(_ptr); }

        bool contains(PyObject *x) {
            int r = PySet_Contains(_ptr,x);
            if(UNLIKELY(r == -1)) throw py_error_set();
            return r != 0;
        }
        template<typename T> bool contains(const T &x) {
            return contains(make_object(x).ref());
        }

    protected:
        template<typename T> set_base(T x) : _object_base(x) {}
        ~set_base() = default;
    };

    class set : public set_base {
    public:
        set(borrowed_ref r) : set_base(r) { assert(PySet_Check(r._ptr)); }
        set(_new_ref r) : set_base(r) { assert(PySet_Check(r._ptr)); }
        set() : set_base(check_new_ref(PySet_New(nullptr))) {}

        void add(PyObject *x) {
            if(UNLIKELY(PySet_Add(_ptr,x)) == -1) throw py_error_set();
        }
        template<typename T> bool add(const T &x) {
            return add(make_object(x).ref());
        }

        bool discard(PyObject *x) {
            int r = PySet_Discard(_ptr,x);
            if(UNLIKELY(r == -1)) throw py_error_set();
            return r != 0;
        }
        template<typename T> bool discard(const T &x) {
            return discard(make_object(x).ref());
        }
    };


    template<typename T=object> class weak_ref {
        object obj;

        PyObject *deref() const {
            // obj will be None if gc_clear is called
            assert(obj.ref() != Py_None);

            return PyWeakref_GET_OBJECT(obj.ref());
        }

    public:
        weak_ref(borrowed_ref r) : obj(r) { assert(PyWeakref_CheckRef(obj.ref())); }
        weak_ref(new_ref r) : obj(r) { assert(PyWeakref_CheckRef(obj.ref())); }
        explicit weak_ref(const object &b,PyObject *callback=nullptr) : obj(check_new_ref(PyWeakref_NewRef(b.ref(),callback))) {}
        weak_ref(const object &b,const nullable<object> &callback) : weak_ref(b,callback.ref()) {}

        operator bool() const { return deref() != nullptr; }

        T operator*() const {
            assert(deref());
            return borrowed_ref(deref());
        }
        T operator->() const { return operator*(); }

        nullable<T> try_deref() const {
            return borrowed_ref(deref());
        }

        PyObject *ref() const { return obj.ref(); }

        void swap(weak_ref &b) {
            std::swap(obj,b.obj);
        }

        int gc_traverse(visitproc visit,void *arg) const { return obj.gc_traverse(visit,arg); }
        void gc_clear() { obj.gc_clear(); }
    };


    /*template<typename T,bool invariable=invariable_storage<T>::value> class pyptr {
        template<typename U> friend class pyptr;

        object _obj;
        T *base;
    public:
        pyptr() : base(0) {}
        pyptr(new_ref r) : _obj(r), base(get_base_or_none<T>(_obj.ref())) {}
        pyptr(borrowed_ref r) : _obj(r), base(get_base_or_none<T>(_obj.ref())) {}
        pyptr(object o) : _obj(o), base(get_base_or_none<T>(_obj.ref())) {}

        template<typename U> pyptr(const pyptr<U> &b) : _obj(b._obj), base(b.base) {}

        template<typename U> pyptr<T> &operator=(const pyptr<U> &b) {
            _obj = b._obj;
            base = b.base;
        }

        T &operator*() { return *base; }
        const T &operator*() const { return *base; }
        T *operator->() { return base; }
        const T *operator->() const { return base; }

        bool operator==(const pyptr &b) const { return _obj == b._obj; }
        bool operator!=(const pyptr &b) const { return _obj != b._obj; }
        operator bool() const { return base != 0; }

        T *ref() const { return base; }

        object obj() const { return _obj; }

        void swap(pyptr<T> &b) {
            _obj.swap(b._obj);
            T *tmp = base;
            base = b.base;
            b.base = tmp;
        }
    };

    template<typename T> class pyptr<T,true> {
        template<typename U> friend class pyptr;

        object _obj;

    public:
        pyptr() {}
        pyptr(new_ref r) : _obj(r) { get_base_or_none<T>(_obj.ref()); }
        pyptr(borrowed_ref r) : _obj(r) { get_base_or_none<T>(_obj.ref()); }
        pyptr(object o) : _obj(o) { get_base_or_none<T>(_obj.ref()); }

        template<typename U,typename=typename std::enable_if<std::is_convertable<U*,T*>::value>::type> pyptr(const pyptr<U> &b) : _obj(b._obj) {}

        template<typename U,typename=typename std::enable_if<std::is_convertable<U*,T*>::value>::type> pyptr<T> &operator=(const pyptr<U> &b) {
            _obj = b._obj;
        }

        T &operator*() const { return *ref(); }
        T *operator->() const { return ref(); }

        bool operator==(const pyptr &b) const { return _obj == b._obj; }
        bool operator!=(const pyptr &b) const { return _obj != b._obj; }
        operator bool() const { return _obj.ref() != Py_None; }

        T *ref() const { return reinterpret_cast<typename wrapped_type<T>::type*>(_obj.ref())->base; }

        object obj() const { return _obj; }

        void swap(pyptr<T> &b) {
            _obj.swap(b._obj);
        }
    };

    // like dynamic_cast except Python's type system is used instead of RTTI
    template<typename T,typename U> inline pyptr<T> python_cast(const pyptr<U> &a) {
        return pyptr<T>(a.obj());
    }*/

    template<typename T> class pyptr {

        nullable<object> _obj;

    public:
        pyptr() = default;
        explicit pyptr(new_ref r) : _obj(r) {}
        explicit pyptr(borrowed_ref r) : _obj(r) {}
        explicit pyptr(object o) : _obj(o) {}
        explicit pyptr(T *o) : _obj(new_ref(reinterpret_cast<PyObject*>(o))) {}

        template<typename U,typename=typename std::enable_if<std::is_convertible<U*,T*>::value>::type> pyptr(const pyptr<U> &b) : _obj(b._obj) {}

        template<typename U,typename=typename std::enable_if<std::is_convertible<U*,T*>::value>::type> pyptr<T> &operator=(const pyptr<U> &b) {
            _obj = b._obj;
        }

        pyptr &operator=(new_ref r) {
            _obj = r;
            return *this;
        }
        pyptr &operator=(borrowed_ref r) {
            _obj = r;
            return *this;
        }

        T &operator*() const { return *get(); }
        T *operator->() const { return get(); }

        template<typename U> bool operator==(const pyptr<U> &b) const { return _obj == b._obj; }
        template<typename U> bool operator!=(const pyptr<U> &b) const { return _obj != b._obj; }
        operator bool() const { return _obj; }

        T *get() const {
            assert(_obj);
            return reinterpret_cast<T*>(_obj.ref());
        }

        PyObject *ref() const { return _obj.ref(); }

        object obj() const { return *_obj; }

        void swap(pyptr<T> &b) {
            _obj.swap(b._obj);
        }

        int gc_traverse(visitproc visit,void *arg) const { return _obj.gc_traverse(visit,arg); }
        void gc_clear() { _obj.gc_clear(); }
    };


    inline Py_ssize_t len(const object &o) {
        return PyObject_Length(o.ref());
    }

    inline Py_ssize_t len(const tuple &o) {
        return PyTuple_GET_SIZE(o.ref());
    }

    inline Py_ssize_t len(const dict &o) {
        return PyDict_Size(o.ref());
    }

    inline Py_ssize_t len(const list &o) {
        return PyList_GET_SIZE(o.ref());
    }

    inline Py_ssize_t len(const bytes &o) {
        return PyBytes_GET_SIZE(o.ref());
    }

    inline object str(const object &o) {
        return check_new_ref(PyObject_Str(o.ref()));
    }

    inline object repr(const object &o) {
        return check_new_ref(PyObject_Repr(o.ref()));
    }

    inline object import_module(const char *mod) {
        return check_new_ref(PyImport_ImportModule(mod));
    }

    inline object iter(PyObject *o) {
        return check_new_ref(PyObject_GetIter(o));
    }

    inline object iter(const object &o) {
        return iter(o.ref());
    }

    inline nullable<object> next(const object &o) {
        PyObject *r = PyIter_Next(o.ref());
        if(!r && PyErr_Occurred()) throw py_error_set();
        return new_ref(r);
    }

    class object_iterator {
        object itr;
        nullable<object> item;
    public:
        typedef object value_type;
        typedef void difference_type;
        typedef object *pointer;
        typedef object &reference;
        typedef std::input_iterator_tag iterator_category;

        object_iterator(const _object_base &itr,const nullable<object> &item) : itr(itr), item(item) {}

        object operator*() const {
            return *item;
        }

        object_iterator &operator++() {
            item = next(itr);
            return *this;
        }
        object_iterator operator++(int) {
            object_iterator tmp = *this;
            item = next(itr);
            return tmp;
        }

        bool operator==(const object_iterator &b) const {
            return itr.ref() == b.itr.ref() && !item && !b.item;
        }
        bool operator!=(const object_iterator &b) const {
            return itr.ref() != b.itr.ref() || item || b.item;
        }
    };

    inline object_iterator object::begin() const {
        return object_iterator(*this,next(*this));
    }

    inline object_iterator object::end() const {
        return object_iterator(*this,nullable<object>());
    }


    template<typename T> struct array_adapter {
        const object origin;
        const size_t size;
        T *const items;
        void index_check(Py_ssize_t i) const {
            if(i < 0 || size_t(i) >= size) THROW_PYERR_STRING(IndexError,"index out of range");
        }

        array_adapter(PyObject *origin,size_t size,T *items) : origin(borrowed_ref(origin)), size(size), items(items) {}
        T &sequence_getitem(Py_ssize_t i) const {
            index_check(i);
            return items[i];
        }
        void sequence_setitem(Py_ssize_t i,const T &item) {
            index_check(i);
            items[i] = item;
        }
        Py_ssize_t length() const { return size; }
        int gc_traverse(visitproc visit,void *arg) const { return (*visit)(origin.ref(),arg); }
    };

    template<typename Item,const char* FullName,bool GC,bool ReadOnly=false> struct obj_array_adapter;

    namespace impl {
        template<typename Item,const char* FullName,bool GC,bool ReadOnly> struct array_adapter_alloc {
            static constexpr traverseproc traverse = nullptr;
            static constexpr long tp_flags = Py_TPFLAGS_DEFAULT;

            PY_MEM_NEW_DELETE
        protected:
            array_adapter_alloc() = default;
            ~array_adapter_alloc() = default;
        };

        template<typename Item,const char* FullName,bool ReadOnly> struct array_adapter_alloc<Item,FullName,true,ReadOnly> {
            static int _traverse(PyObject *self,visitproc visit,void *arg) {
                return reinterpret_cast<obj_array_adapter<Item,FullName,true,ReadOnly>*>(self)->data.gc_traverse(visit,arg);
            }
            static constexpr traverseproc traverse = &_traverse;
            static constexpr long tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_HAVE_GC;

            PY_MEM_GC_NEW_DELETE
        protected:
            array_adapter_alloc() = default;
            ~array_adapter_alloc() = default;
        };

        template<typename Item,const char* FullName,bool GC,bool ReadOnly> struct array_adapter_set_item {
            FIX_STACK_ALIGN static int _value(PyObject *self,Py_ssize_t index,PyObject *value) {
                try {
                    reinterpret_cast<obj_array_adapter<Item,FullName,GC,ReadOnly>*>(self)->data.sequence_setitem(index,from_pyobject<Item>(value));
                } PY_EXCEPT_HANDLERS(-1)
                return 0;
            }
            static constexpr ssizeobjargproc value = &_value;
        };

        template<typename Item,const char* FullName,bool GC> struct array_adapter_set_item<Item,FullName,GC,true> {
            static constexpr ssizeobjargproc value = nullptr;
        };
    }

    template<typename Item,const char* FullName,bool GC,bool ReadOnly>
    struct obj_array_adapter : impl::array_adapter_alloc<Item,FullName,GC,ReadOnly>, pyobj_subclass {
        static PySequenceMethods seq_methods;
        CONTAINED_PYTYPE_DEF
        PyObject_HEAD

        obj_array_adapter(PyObject *origin,size_t size,Item *items) : data(origin,size,items) {
            PyObject_Init(ref(this),pytype());
        }

        /* a bug in GCC 9 prevents us from using function attributes on lambda
        functions */
        FIX_STACK_ALIGN static PyObject *_sq_item(PyObject *self,Py_ssize_t index) {
            try {
                return to_pyobject(reinterpret_cast<obj_array_adapter<Item,FullName,GC,ReadOnly>*>(self)
                    ->data.sequence_getitem(index));
            } PY_EXCEPT_HANDLERS(nullptr)
        }

        array_adapter<Item> data;
    };

    template<typename Item,const char* FullName,bool GC,bool ReadOnly>
    PySequenceMethods obj_array_adapter<Item,FullName,GC,ReadOnly>::seq_methods = {
        .sq_length = [](PyObject *self) {
            return reinterpret_cast<obj_array_adapter<Item,FullName,GC,ReadOnly>*>(self)->data.length();
        },
        .sq_item = &obj_array_adapter<Item,FullName,GC,ReadOnly>::_sq_item,
        .sq_ass_item = impl::array_adapter_set_item<Item,FullName,GC,ReadOnly>::value};

    template<typename Item,const char* FullName,bool GC,bool ReadOnly>
        PyTypeObject obj_array_adapter<Item,FullName,GC,ReadOnly>::_pytype = {
            PyVarObject_HEAD_INIT(nullptr,0)
            .tp_name = FullName,
            .tp_basicsize = sizeof(obj_array_adapter<Item,FullName,GC,ReadOnly>),
            .tp_dealloc = [](PyObject *self) -> void {
                typedef obj_array_adapter<Item,FullName,GC,ReadOnly> self_t;

                reinterpret_cast<self_t*>(self)->~self_t();
                (*self_t::pytype()->tp_free)(self);
            },
            .tp_as_sequence = &obj_array_adapter<Item,FullName,GC,ReadOnly>::seq_methods,
            .tp_flags = obj_array_adapter<Item,FullName,GC,ReadOnly>::tp_flags,
            .tp_traverse = obj_array_adapter<Item,FullName,GC,ReadOnly>::traverse,
            .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
                PyErr_Format(PyExc_TypeError,"The %s type cannot be instantiated directly",typename_base(FullName));
                return nullptr;
            }};


    /*template<typename T> inline pyptr<T> newpy() {
        return new_ref(new wrapped_type<T>::type());
    }

    template<typename T,typename A1> inline pyptr<T> newpy(A1 a1) {
        return new_ref(new wrapped_type<T>::type(a1));
    }

    template<typename T,typename A1,typename A2> inline pyptr<T> newpy(A1 a1,A2 a2) {
        return new_ref(new wrapped_type<T>::type(a1,a2));
    }

    template<typename T,typename A1,typename A2,typename A3> inline pyptr<T> newpy(A1 a1,A2 a2,A3 a3) {
        return new_ref(new wrapped_type<T>::type(a1,a2,a3));
    }

    template<typename T,typename A1,typename A2,typename A3,typename A4> inline pyptr<T> newpy(A1 a1,A2 a2,A3 a3,A4 a4) {
        return new_ref(new wrapped_type<T>::type(a1,a2,a3,a4));
    }*/




    /*template<> inline PyObject *to_pyobject<const std::string&>(const std::string &x) {
        return PyString_FromStringAndSize(x.c_str(),x.size());
    }

    template<> inline PyObject *to_pyobject<const std::wstring&>(const std::wstring &x) {
        return PyUnicode_FromWideChar(x.c_str(),x.size());
    }*/
}



inline void swap(py::object &a,py::object &b) { a.swap(b); }
inline void swap(py::tuple &a,py::tuple &b) { a.swap(b); }
inline void swap(py::dict &a,py::dict &b) { a.swap(b); }
inline void swap(py::list &a,py::list &b) { a.swap(b); }
inline void swap(py::bytes &a,py::bytes &b) { a.swap(b); }
template<typename T> inline void swap(py::nullable<T> &a,py::nullable<T> &b) { a.swap(b); }
template<typename T> inline void swap(py::pyptr<T> &a,py::pyptr<T> &b) { a.swap(b); }

template<typename T> inline T from_pyobject(const py::_object_base &o) {
    return from_pyobject<T>(o.ref());
}

template<> inline py::object from_pyobject<py::object>(PyObject *o) {
    return py::borrowed_ref(o);
}

template<> inline py::tuple from_pyobject<py::tuple>(PyObject *o) {
    if(!PyTuple_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of tuple");
    return py::borrowed_ref(o);
}

template<> inline py::dict from_pyobject<py::dict>(PyObject *o) {
    if(!PyDict_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of dict");
    return py::borrowed_ref(o);
}

template<> inline py::list from_pyobject<py::list>(PyObject *o) {
    if(!PyList_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of list");
    return py::borrowed_ref(o);
}

template<> inline py::bytes from_pyobject<py::bytes>(PyObject *o) {
    if(!PyBytes_Check(o))
        THROW_PYERR_STRING(TypeError,"object is not an instance of bytes");

    return py::borrowed_ref(o);
}

template<typename T> inline PyObject *to_pyobject(const py::pyptr<T> &x) {
    return py::incref(x.ref());
}

template<typename... T> inline PyObject *to_pyobject(const std::tuple<T...> &x) {
    return std::apply(&py::make_tuple<T...>,x).new_ref();
}

#endif
