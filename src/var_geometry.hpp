#ifndef var_geometry_hpp
#define var_geometry_hpp

#include <Python.h>
#include <algorithm>

#include "geometry.hpp"
#include "pyobject.hpp"
#include "camera.hpp"
#include "ntracer.hpp"


namespace var {
    template<typename T> T *new_var(Py_ssize_t size) {
        assert(size > 0);
        return reinterpret_cast<T*>(py::check_obj(reinterpret_cast<PyObject*>(
            (T::pytype.tp_flags & Py_TPFLAGS_HAVE_GC ? _PyObject_GC_NewVar : _PyObject_NewVar)(&T::pytype,size))));
    }
    
    template<typename T> T *internal_new_var(size_t size) {
        assert(size);
        T *r = reinterpret_cast<T*>(
            operator new(T::pytype.tp_basicsize + T::pytype.tp_itemsize * size));
        Py_TYPE(r) = NULL;
        Py_REFCNT(r) = 1;
        Py_SIZE(r) = size;
        return r;
    }
    
    struct external {
        template<typename Store> static Store new_store(int d) {
            return Store(d,new_var<typename Store::obj_t>(Store::size_needed(d)));
        }
        
        template<typename Store> static Store copy_store(const Store &s) {
            Py_INCREF(s.data);
            return s;
        }
        
        template<typename Store> static void free_store(Store &s) {
            Py_DECREF(s.data);
        }
        
        template<typename Store> static void replace_store(Store &a,const Store &b) {
            typename Store::obj_t *old = a.data;
            a.data = b.data;
            Py_INCREF(a.data);
            Py_DECREF(old);
        }
    };
    
    /* CPython's object system is not thread safe, so for temporary objects that
       are used by the scene's worker threads, we do our own reference counting
       and memory allocation */
    struct internal {
        template<typename Store> static Store new_store(int d) {
            return Store(d,internal_new_var<typename Store::obj_t>(Store::size_needed(d)));
        }
        
        template<typename Store> static Store copy_store(const Store &s) {
            ++Py_REFCNT(s.data);
            return s;
        }
        
        template<typename Store> static void free_store(Store &s) {
            assert(Py_REFCNT(s.data) > 0);
            if(--Py_REFCNT(s.data) == 0) operator delete(s.data);
        }
        
        template<typename Store> static void replace_store(Store &a,const Store &b) {
            // we increment first, just in-case a == b
            ++Py_REFCNT(b.data);
            free_store(a);
            a.data = b.data;
        }
    };
    
    
    /* an array that can be initialized with a call-back */
    template<typename T> struct init_array {
        size_t size;
        void *data;
        
        T &operator[](int i) { return reinterpret_cast<T*>(data)[i]; }
        const T &operator[](int i) const { return reinterpret_cast<const T*>(data)[i]; }
        
        operator T*() { return reinterpret_cast<T*>(data); }
        operator const T*() const { return reinterpret_cast<const T*>(data); }
        
        template<typename F> init_array(size_t size,F f) : size(size) {
            assert(size);
            
            data = operator new(sizeof(T) * size);
            
            size_t i=0;
            try {
                for(; i<size; ++i) new(&(*this)[i]) T(f(i));
            } catch(...) {
                while(i) (*this)[--i].~T();
                throw;
            }
        }
        
        init_array(const init_array&) = delete;
        init_array &operator=(const init_array&) = delete;
        
        ~init_array() {
            for(size_t i=0; i<size; ++i) (*this)[i].~T();
            operator delete(data);
        }
    };
    
    
    template<typename T> struct vector_store;
    template<typename T> using py_vector = vector_impl<vector_store<T>,external>;
    template<typename T> using vector = vector_impl<vector_store<T>,internal>;
    struct matrix_store;
    typedef matrix_impl<matrix_store,external> py_matrix;
    typedef matrix_impl<matrix_store,internal> matrix;
    struct camera_store;


    template<typename T> struct vector_obj : vector_obj_base {
        PyObject_VAR_HEAD
        T items[1];
        
        inline py_vector<T> cast_base();
        inline py_vector<T> get_base();
        
        static constexpr size_t item_size = sizeof(T);
        
        typedef py_vector<T> ref_type;
    };

    template<typename T> struct vector_store {
        typedef T item_t;
        typedef vector_obj<T> obj_t;
        
        obj_t *data;
            
        explicit vector_store(obj_t *obj) : data(obj) {
            // for internal thread-use objects, ob_type is always NULL
            assert(!obj->ob_type || PyObject_TypeCheck(reinterpret_cast<PyObject*>(obj),&obj_t::pytype));
        }
        
        vector_store(int d,obj_t *obj) : vector_store(obj) {}

        item_t &operator[](int n) const { return data->items[n]; }

        int dimension() const {
            return Py_SIZE(data);
        }
        
        static Py_ssize_t size_needed(int d) { return d; }
    };
    
    template<typename T> inline py_vector<T> vector_obj<T>::cast_base() {
        assert(ob_type);
        return vector_store<T>(this);
    }
    
    template<typename T> inline py_vector<T> vector_obj<T>::get_base() { return cast_base(); }
    
    
    struct matrix_obj : matrix_obj_base {
        PyObject_VAR_HEAD
        int dimension;
        REAL items[1];
        
        inline py_matrix cast_base();
        inline py_matrix get_base();
        
        static const size_t item_size = sizeof(REAL);
        
        typedef py_matrix ref_type;
    };

    struct matrix_store {
        typedef matrix_obj obj_t;
        typedef vector_store<REAL> v_store;
        
        struct pivot_buffer {
            int *data;
            pivot_buffer(int size) : data(new int[size]) {}
            ~pivot_buffer() {
                delete[] data;
            }
        };
        
        obj_t *data;

        explicit matrix_store(obj_t *obj) : data(obj) {
            // for internal thread-use objects, ob_type is always NULL
            assert(!obj->ob_type || PyObject_TypeCheck(reinterpret_cast<PyObject*>(obj),&obj_t::pytype));
        }
        
        matrix_store(int d,obj_t *obj) : matrix_store(obj) {
            obj->dimension = d;
        }

        template<typename F> static void rep(int d,F f) {
            for(int row=0; row<d; ++row) {
                for(int col=0; col<d; ++col) f(row,col);
            }
        }
        
        template<class F> static void rep1(int d,F f) { ::rep(d,f); }

        REAL *operator[](int n) const { return data->items + n*dimension(); }

        int dimension() const { return data->dimension; }
        
        static Py_ssize_t size_needed(int d) { return d*d; }
    };
    
    inline py_matrix matrix_obj::cast_base() {
        assert(ob_type);
        return matrix_store(this);
    }
    inline py_matrix matrix_obj::get_base() { return cast_base(); }


    struct camera_obj : camera_obj_base {
        PyObject_VAR_HEAD

        PyObject *idict;
        PyObject *weaklist;
        py_vector<REAL> origin;
        py_vector<REAL> axes[1];
        
        inline _camera<camera_store> cast_base();
        inline _camera<camera_store> get_base();
        
        static const size_t item_size = sizeof(py_vector<REAL>);
        
        typedef _camera<camera_store> ref_type;
        
        void dealloc() {
            origin.~py_vector<REAL>();
            for(int i=0; i<Py_SIZE(this); ++i) axes[i].~py_vector<REAL>();
        }
        
        inline void alloc(int d);
    };
    
    struct camera_store {
        typedef py_vector<REAL> vector_t;
        typedef init_array<vector_t> smaller_array;

        py::object obj;
        
        template<typename F> camera_store(int d,const vector_t &o,F a_init,PyObject *mem=NULL) : obj(
                mem ?
                py::object(py::borrowed_ref(mem)) :
                py::object(py::new_ref(reinterpret_cast<PyObject*>(new_var<camera_obj>(d))))) {
            assert(PyObject_TypeCheck(obj.ref(),&camera_obj::pytype));
            
            data()->idict = data()->weaklist = NULL;
            
            assert(o.dimension() == d);
            new(&data()->origin) vector_t(o);
            
            int i=0;
            try {
                for(; i<d; ++i) {
                    vector_t tmp = a_init(i);
                    assert(tmp.dimension() == d);
                    new(&data()->axes[i]) vector_t(tmp);
                }
            } catch(...) {
                data()->origin.~vector_t();
                while(i) data()->axes[--i].~vector_t();
            }
        }
        
        explicit camera_store(const py::object &o) : obj(o) {
            assert(PyObject_TypeCheck(o.ref(),&camera_obj::pytype));
        }

        camera_obj *data() const { return reinterpret_cast<camera_obj*>(obj.ref()); }
        int dimension() const { return Py_SIZE(data()); }
        vector_t &origin() { return data()->origin; }
        const vector_t &origin() const { return data()->origin; }
        vector_t *axes() { return data()->axes; }
        const vector_t *axes() const { return data()->axes; }
    };
    
    inline _camera<camera_store> camera_obj::cast_base() {
        return camera_store(py::borrowed_ref(reinterpret_cast<PyObject*>(this)));
    }
    inline _camera<camera_store> camera_obj::get_base() { return cast_base(); }
    
    inline void camera_obj::alloc(int d) {
        auto blank_v = [d](int i){ return py_vector<REAL>(d); };
        camera_store(d,blank_v(0),blank_v,reinterpret_cast<PyObject*>(this));
    }
    
    struct repr {
        typedef py_vector<REAL> py_vector_t;
        typedef py_matrix py_matrix_t;
        
        typedef vector<REAL> vector_t;
        typedef matrix matrix_t;
        
        typedef _camera<camera_store> camera_t;
        
        static const int required_d = 0;
        
        typedef var::vector_obj<REAL> vector_obj;
        typedef var::matrix_obj matrix_obj;
        typedef var::camera_obj camera_obj;
        
        template<typename T> using init_array = var::init_array<T>;
        template<typename T> using smaller_init_array = var::init_array<T>;
    };
}

inline PyObject *to_pyobject(const var::repr::py_vector_t &v) {
    return py::incref(reinterpret_cast<PyObject*>(v.store.data));
}

inline PyObject *to_pyobject(const var::repr::py_matrix_t &m) {
    return py::incref(reinterpret_cast<PyObject*>(m.store.data));
}

inline PyObject *to_pyobject(const var::repr::camera_t &c) {
    return c.store.obj.get_new_ref();
}

#endif

