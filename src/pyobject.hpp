#ifndef pyobject_hpp
#define pyobject_hpp

#include <Python.h>
#include <algorithm>
#include <iterator>
#include <structmember.h>

#include "py_common.hpp"


#define THROW_PYERR_STRING(exception,message) { PyErr_SetString(PyExc_##exception,message); throw py_error_set(); }


namespace py {

    // Exception-safe alternative to Py_BEGIN_ALLOW_THREADS and Py_END_ALLOW_THREADS
    class AllowThreads {
#ifdef WITH_THREAD
        PyThreadState *save;
    public:
        AllowThreads() { save = PyEval_SaveThread(); }
        ~AllowThreads() { PyEval_RestoreThread(save); }
#endif
    };

    inline PyObject *check_obj(PyObject *o) {
        if(!o) throw py_error_set();
        return o;
    }

    inline PyObject *incref(PyObject *o) {
        assert(o);
        Py_INCREF(o);
        return o;
    }

    inline PyObject *xincref(PyObject *o) {
        Py_XINCREF(o);
        return o;
    }

    struct borrowed_ref {
        PyObject *_ptr;
        explicit borrowed_ref(PyObject *ptr) : _ptr(ptr) {}
    };

    struct new_ref {
        PyObject *_ptr;
        explicit new_ref(PyObject *ptr) : _ptr(ptr) {}
    };
    
    inline void *malloc(size_t size) {
        void *r = PyMem_Malloc(size);
        if(!r) throw std::bad_alloc();
        return r;
    }
    
    inline void free(void *ptr) noexcept {
        PyMem_Free(ptr);
    }

    template<typename T> inline T *get_base_or_none(PyObject *o) {
        return o == Py_None ? NULL : &get_base<T>(o);
    }


    class object;
    class object_attr_proxy;
    class object_item_proxy;
    class object_iterator;

    class _object_base {
    protected:
        PyObject *_ptr;

        void reset(PyObject *b) {
            Py_INCREF(b);

            // cyclic garbage collection safety
            PyObject *tmp = _ptr;
            _ptr = b;
            Py_DECREF(tmp);
        }

        _object_base(PyObject *ptr) : _ptr(ptr) {}
        _object_base(borrowed_ref r) : _ptr(incref(r._ptr)) {}
        _object_base(new_ref r) : _ptr(r._ptr) { assert(_ptr); }
        _object_base(const _object_base &b) : _ptr(incref(b._ptr)) {}

        ~_object_base() {
            Py_DECREF(_ptr);
        }

        void swap(_object_base &b) {
            std::swap(_ptr,b._ptr);
        }

    public:
        operator bool() const {
            return PyObject_IsTrue(_ptr);
        }

        PyObject *ref() const { return _ptr; }
        PyObject *get_new_ref() const { return incref(_ptr); }
        Py_ssize_t ref_count() const { return _ptr->ob_refcnt; }

        object_attr_proxy attr(const char *name) const;

        bool has_attr(const char *name) const { return PyObject_HasAttrString(_ptr,name); }
        bool has_attr(const _object_base &name) const { return PyObject_HasAttr(_ptr,name._ptr); }
        
        inline object operator()() const;
        template<typename Arg1,typename... Args> inline object operator()(const Arg1 &arg1,const Args&... args) const;

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

        int gc_traverse(visitproc visit,void *arg) const { return _ptr != Py_None ? (*visit)(_ptr,arg) : 0; }
        void gc_clear() { reset(Py_None); }
    };

    class object : public _object_base {
    public:
        object(borrowed_ref r) : _object_base(r) {}
        object(new_ref r) : _object_base(r) {}
        object(const _object_base &b) : _object_base(b) {}

        object &operator=(const _object_base &b) {
            reset(b.ref());
            return *this;
        }

        void swap(object &b) { _object_base::swap(b); }
        
        inline object_iterator begin() const;
        inline object_iterator end() const;
    };

    template<typename T> inline object make_object(const T &x) {
        return object(new_ref(to_pyobject(x)));
    }
    
    template<> inline object make_object<_object_base>(const _object_base &x) {
        return x;
    }

    class object_attr_proxy {
        friend class _object_base;
        friend void del(const object_attr_proxy &attr);

        PyObject *_ptr;
        const char *name;

        object_attr_proxy(PyObject *ptr,const char *name) : _ptr(ptr), name(name) {}
    public:
        operator object() const { return new_ref(check_obj(PyObject_GetAttrString(_ptr,name))); }

        object_attr_proxy &operator=(object val) {
            if(PyObject_SetAttrString(_ptr,name,val.ref()) == -1) throw py_error_set();
            return *this;
        }

        // so object_a.attr("x") = object_b.attr("y") works as expected
        object_attr_proxy &operator=(const object_attr_proxy &val) {
            return operator=(static_cast<object>(val));
        }

        template<typename... Args> object operator()(const Args&... args) const {
            return new_ref(check_obj(PyObject_CallMethodObjArgs(
                _ptr,
                object(new_ref(check_obj(PYSTR(InternFromString)(name)))).ref(),
                make_object(args).ref()...,
                0)));
        }
    };

    inline object_attr_proxy _object_base::attr(const char *name) const { return object_attr_proxy(_ptr,name); }

    inline object _object_base::operator()() const {
        return new_ref(check_obj(PyObject_CallObject(_ptr,0)));
    }

    template<typename Arg1,typename... Args> inline object _object_base::operator()(const Arg1 &arg1,const Args&... args) const {
        return new_ref(check_obj(PyObject_CallFunctionObjArgs(_ptr,
            make_object(arg1).ref(),
            make_object(args).ref()...,
            0)));
    }

    inline void del(const object_attr_proxy &attr) {
        if(PyObject_DelAttrString(attr._ptr,attr.name) == -1) throw py_error_set();
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

        operator object() const { return new_ref(check_obj(PyObject_GetItem(_ptr,key))); }

        object_item_proxy &operator=(object val) {
            if(PyObject_SetItem(_ptr,key,val.ref()) == -1) throw py_error_set();
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
        if(PyObject_DelItem(item._ptr,item.key) == -1) throw py_error_set();
    }


    template<typename T> class _nullable {
        PyObject *_ptr;

        void reset(PyObject *b) {
            // cyclic garbage collection safety
            PyObject *tmp = _ptr;
            _ptr = b;
            Py_XDECREF(tmp);
        }
    public:
        _nullable() : _ptr(nullptr) {}
        _nullable(const _nullable<T> &b) : _ptr(b._ptr) { Py_XINCREF(_ptr); }
        _nullable(const T &b) : _ptr(b.new_ref()) {}
        _nullable(borrowed_ref r) : _ptr(r._ptr) { Py_XINCREF(_ptr); }
        _nullable(new_ref r) : _ptr(r._ptr) {}
        
        _nullable<T> &operator=(borrowed_ref r) {
            Py_XINCREF(r._ptr);
            reset(r._ptr);
            return *this;
        }
        
        _nullable<T> &operator=(new_ref r) {
            reset(r._ptr);
            return *this;
        }

        _nullable<T> &operator=(const _nullable<T> &b) {
            Py_XINCREF(b._ptr);
            reset(b._ptr);
            return *this;
        }
        _nullable<T> &operator=(const T &b) {
            Py_INCREF(b._ptr);
            reset(b._ptr);
            return *this;
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
        
        void swap(const _nullable<T> &b) {
            std::swap(_ptr,b._ptr);
        }

        int gc_traverse(visitproc visit,void *arg) const { return _ptr ? (*visit)(_ptr,arg) : 0; }
        void gc_clear() { reset(nullptr); }
    };

    typedef _nullable<object> nullable_object;


#if PY_VERSION_HEX >= 0x02060000
    class BufferView {
        Py_buffer view;
    public:
        BufferView(PyObject *obj,int flags) {
            if(PyObject_GetBuffer(obj,&view,flags)) throw py_error_set();
        }

        BufferView(object obj,int flags) {
            if(PyObject_GetBuffer(obj.ref(),&view,flags)) throw py_error_set();
        }

        ~BufferView() {
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
        void *internal() const { return view.internal; }
    };
#endif

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
        tuple(new_ref r) : _object_base(r) { assert(PyTuple_Check(r._ptr)); }
        explicit tuple(Py_ssize_t len) : _object_base(new_ref(check_obj(PyTuple_New(len)))) {}
        tuple(const tuple &b) : _object_base(b) {}
        explicit tuple(const _object_base &b) : _object_base(object(py::borrowed_ref(reinterpret_cast<PyObject*>(&PyTuple_Type)))(b)) {
            assert(PyTuple_Check(_ptr));
        }

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

    typedef _nullable<tuple> nullable_tuple;


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
            PyList_SET_ITEM(_ptr,index,val.get_new_ref());
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
        list(new_ref r) : _object_base(r) { assert(PyList_Check(r._ptr)); }
        list() : _object_base(new_ref(check_obj(PyList_New(0)))) {}
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
    
    typedef _nullable<list> nullable_list;
    
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
        dict(new_ref r) : _object_base(r) { assert(PyDict_Check(r._ptr)); }
        dict() : _object_base(new_ref(check_obj(PyDict_New()))) {}
        dict(const dict &b) : _object_base(b) {}

        dict &operator=(const dict &b) {
            reset(b._ptr);
            return *this;
        }

        void swap(dict &b) { _object_base::swap(b); }

        template<typename T> dict_item_proxy operator[](T key) const { return dict_item_proxy(_ptr,to_pyobject(key)); }
        Py_ssize_t size() const { return PyDict_Size(_ptr); }
        template<typename T> nullable_object find(T key) const {
#if PY_MAJOR_VERSION >= 3
            PyObject *item = PyDict_GetItemWithError(_ptr,to_pyobject(key));
            if(!item && PyErr_Occurred()) throw py_error_set();
            return borrowed_ref(item);
#else
            /* mp_subscript is used instead of PyDict_GetItem because
               PyDict_GetItem swallows all errors */
            PyMappingMethods *m = _ptr->ob_type->tp_as_mapping;
            assert(m && m->mp_subscript);
            PyObject *item = (*m->mp_subscript)(_ptr,to_pyobject(key));
            if(!item) {
                if(!PyErr_ExceptionMatches(PyExc_KeyError)) throw py_error_set();
                PyErr_Clear();
            }
            return new_ref(item);
#endif
        }

        dict copy(const dict &b) const {
            return new_ref(PyDict_Copy(b._ptr));
        }
    };

    typedef _nullable<dict> nullable_dict;

    inline void del(const dict_item_proxy &attr) {
        if(PyDict_DelItem(attr._ptr,attr.key)) throw py_error_set();
    }
    
    
    class bytes : public _object_base {
    public:
        bytes(borrowed_ref r) : _object_base(r) { assert(PYBYTES(Check)(r._ptr)); }
        bytes(new_ref r) : _object_base(r) { assert(PYBYTES(Check)(r._ptr)); }
        bytes(const char *str="") : _object_base(new_ref(check_obj(PYBYTES(FromString)(str)))) {}
        explicit bytes(Py_ssize_t s) : _object_base(new_ref(check_obj(PYBYTES(FromStringAndSize)(NULL,s)))) {}
        bytes(const bytes &b) : _object_base(b) {}

        bytes &operator=(const bytes &b) {
            reset(b._ptr);
            return *this;
        }
        
        void swap(bytes &b) { _object_base::swap(b); }
        
        //bytes &operator+=(const bytes &b);
        
        Py_ssize_t size() const { return PYBYTES(GET_SIZE)(_ptr); }
        
        char *data() { return PYBYTES(AS_STRING)(_ptr); }
        const char *data() const { return PYBYTES(AS_STRING)(_ptr); }
    };
    
    typedef _nullable<bytes> nullable_bytes;


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

        nullable_object _obj;

    public:
        pyptr() = default;
        explicit pyptr(new_ref r) : _obj(r) {}
        explicit pyptr(borrowed_ref r) : _obj(r) {}
        explicit pyptr(object o) : _obj(o) {}

        template<typename U,typename=typename std::enable_if<std::is_convertible<U*,T*>::value>::type> pyptr(const pyptr<U> &b) : _obj(b._obj) {}

        template<typename U,typename=typename std::enable_if<std::is_convertible<U*,T*>::value>::type> pyptr<T> &operator=(const pyptr<U> &b) {
            _obj = b._obj;
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
        return PYBYTES(GET_SIZE)(o.ref());
    }
    
    inline object str(const object &o) {
        return new_ref(check_obj(PyObject_Str(o.ref())));
    }
    
    inline object repr(const object &o) {
        return new_ref(check_obj(PyObject_Repr(o.ref())));
    }

    inline object iter(const object &o) {
        return new_ref(check_obj(PyObject_GetIter(o.ref())));
    }

    inline nullable_object next(const object &o) {
        PyObject *r = PyIter_Next(o.ref());
        if(!r && PyErr_Occurred()) throw py_error_set();
        return new_ref(r);
    }
    
    class object_iterator {
        object itr;
        nullable_object item;
    public:
        typedef object value_type;
        typedef void difference_type;
        typedef object *pointer;
        typedef object &reference;
        typedef std::input_iterator_tag iterator_category;

        object_iterator(const _object_base &itr,const nullable_object &item) : itr(itr), item(item) {}
        
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
        return object_iterator(*this,nullable_object());
    }


    template<typename T> class array_adapter {
        const object origin;
        const size_t size;
        T *const items;
        void index_check(Py_ssize_t i) const {
            if(i < 0 || size_t(i) >= size) THROW_PYERR_STRING(IndexError,"index out of range")
        }

    public:
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


namespace std {
    template<> inline void swap(py::object &a,py::object &b) { a.swap(b); }
    template<> inline void swap(py::tuple &a,py::tuple &b) { a.swap(b); }
    template<> inline void swap(py::dict &a,py::dict &b) { a.swap(b); }
    template<> inline void swap(py::list &a,py::list &b) { a.swap(b); }
    template<> inline void swap(py::bytes &a,py::bytes &b) { a.swap(b); }
    template<typename T> inline void swap(py::_nullable<T> &a,py::_nullable<T> &b) { a.swap(b); }
    template<typename T> inline void swap(py::pyptr<T> &a,py::pyptr<T> &b) { a.swap(b); }
}

template<typename T> inline T from_pyobject(const py::_object_base &o) {
    return from_pyobject<T>(o.ref());
}

template<> inline py::tuple from_pyobject<py::tuple>(PyObject *o) {
    if(!PyTuple_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of tuple")
    return py::borrowed_ref(o);
}

template<> inline py::dict from_pyobject<py::dict>(PyObject *o) {
    if(!PyDict_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of dict")
    return py::borrowed_ref(o);
}

template<> inline py::list from_pyobject<py::list>(PyObject *o) {
    if(!PyList_Check(o)) THROW_PYERR_STRING(TypeError,"object is not an instance of list")
    return py::borrowed_ref(o);
}

template<> inline py::bytes from_pyobject<py::bytes>(PyObject *o) {
    if(!PYBYTES(Check(o))) {
        PyErr_SetString(PyExc_TypeError,"object is not an instance of "
#if PY_MAJOR_VERSION >= 3
            "bytes"
#else
            "str"
#endif
            );
        throw py_error_set();
    }
    return py::borrowed_ref(o);
}

#endif
