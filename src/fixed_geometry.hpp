#ifndef fixed_geometry_hpp
#define fixed_geometry_hpp

#include <Python.h>

#include "py_common.hpp"
#include "geometry.hpp"
#include "camera.hpp"
#include "ntracer.hpp"

namespace fixed {
    template<typename F,int I> struct _rep { static void go(F f) { f(I); _rep<F,I-1>::go(f); } };
    template<typename F> struct _rep<F,-1> { static void go(F f) { } };

#ifdef __GNUC__
    template<int N,typename F> inline void rep(F f) __attribute__((always_inline,flatten));
#endif
    
    template<int N,typename F> inline void rep(F f) {
        _rep<F,N-1>::go(f);
    }
    
    struct alloc {
        template<typename Store> static Store new_store(int d) {
            return Store(d);
        }
        
        template<typename Store> static Store copy_store(const Store &s) {
            return s;
        }
        
        template<typename Store> static void free_store(Store &s) {}
        
        template<typename Store> static void replace_store(Store &a,const Store &b) {
            a = b;
        }
    };
    
    /* an array that can be initialized with a call-back */
    template<typename T,size_t Size> struct init_array {
        typename std::aligned_storage<sizeof(T) * Size,alignof(T)>::type data;
        
        T *begin() { return reinterpret_cast<T*>(&data); }
        const T *begin() const { return reinterpret_cast<const T*>(&data); }
        
        T *end() { return begin() + Size; }
        const T *end() const { return begin() + Size; }
        
        T &front() { return begin()[0]; }
        const T &front() const { return begin()[0]; }
        
        T &back() { return begin()[Size-1]; }
        const T &back() const { return begin()[Size-1]; }
        
        T &operator[](int i) { return begin()[i]; }
        const T &operator[](int i) const { return begin()[i]; }
        
        operator T*() { return begin(); }
        operator const T*() const { return begin(); }
        
        size_t size() const { return size; }
        
        template<typename F> init_array(size_t size,F f) {
            assert(size == Size);
            subinit(f,0);
        }
        
        ~init_array() {
            for(T &x : *this) x.~T();
        }
        
    private:
        template<typename F> void subinit(F f,size_t i) {
            new(&(*this)[i]) T(f(i));
            
            if(i < Size-1) {
                try {
                    subinit(f,i+1);
                } catch(...) {
                    (*this)[i].~T();
                    throw;
                }
            }
        }
    };

    
    template<int N,typename T> struct vector_store {
        typedef T item_t;
        
        explicit vector_store(int d) {
            assert(d == N);
        }
        
        T &operator[](int n) { return val[n]; }
        T operator[](int n) const { return val[n]; }
        
        int dimension() const { return N; }
        
        T val[N];
    };
    
    template<int N,typename T> using vector = vector_impl<vector_store<N,T>,alloc>;
    
    
    template<int N> struct matrix_store {
        typedef vector_store<N,real> v_store;
        struct pivot_buffer {
            int data[N];
            pivot_buffer(int size) {
                assert(size == N);
            }
        };
        
        explicit matrix_store(int d) {
            assert(d == N);
        }
        
        template<typename F> static void rep(int d,F f) {
            fixed::rep<N>([=](int row){ fixed::rep<N>([=](int col){ f(row,col); }); });
        }
        
        template<typename F> static void rep1(int d,F f) { fixed::rep<N>(f); }
        
        typedef impl::vector_methods<vector_store<N,real> > vector_t;
        
        real *operator[](int n) { return val[n]; }
        const real *operator[](int n) const { return val[n]; }
        
        static constexpr int _dimension = N;
        int dimension() const { return N; }
        
        real val[N][N];
    };
    
    template<int N> using matrix = matrix_impl<matrix_store<N>,alloc>;


    template<int N> struct camera_store {
        typedef vector<N,real> vector_t;
        typedef init_array<vector_t,N-1> smaller_array;
        
        template<typename F> camera_store(int d,const vector_t &o,F a_init) : _origin(o), _axes(N,a_init) {
            assert(d == N);
        }
        
        int dimension() const { return N; }
        vector_t &origin() { return _origin; }
        const vector_t &origin() const { return _origin; }
        vector_t *axes() { return _axes; }
        const vector_t *axes() const { return _axes; }
        
        vector_t _origin;
        init_array<vector_t,N> _axes;
    };
    
    
    template<typename T,typename Base> struct simple_py_wrapper : Base {
        PyObject_HEAD
        T base;
        PY_MEM_NEW_DELETE
        simple_py_wrapper(const T &b) : base(b) {
            PyObject_Init(reinterpret_cast<PyObject*>(this),&Base::pytype);
        }
        
        T &cast_base() { return base; }
        T &get_base() { return base; }
        
        static const size_t item_size = 0;
        
        typedef T &ref_type;
    };
    
    template<int N> struct camera_obj : camera_obj_base {
        PyObject_HEAD
        PyObject *idict;
        PyObject *weaklist;
        _camera<camera_store<N> > base;
        
        PY_MEM_GC_NEW_DELETE
        camera_obj(const _camera<camera_store<N> > &b) : idict(NULL), weaklist(NULL), base(b) {
            PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
        }
        ~camera_obj() {
            Py_XDECREF(idict);
            if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
        }
        
        _camera<camera_store<N> > &cast_base() { return base; }
        _camera<camera_store<N> > &get_base() { return base; }
        
        static const size_t item_size = 0;
        
        typedef _camera<camera_store<N> > &ref_type;
        
        void dealloc() const {}
        void alloc(int d) const {}
    };
    
    
    template<int N> struct repr {
        typedef vector<N,real> py_vector_t;
        typedef matrix<N> py_matrix_t;
        
        typedef vector<N,real> vector_t;
        typedef matrix<N> matrix_t;
        
        typedef _camera<camera_store<N> > camera_t;
        
        static const int required_d = N;
        
        typedef simple_py_wrapper<vector<N,real>,vector_obj_base> vector_obj;
        typedef simple_py_wrapper<matrix<N>,matrix_obj_base> matrix_obj;
        typedef fixed::camera_obj<N> camera_obj;
        
        template<typename T> using init_array = fixed::init_array<T,N>;
        template<typename T> using smaller_init_array = fixed::init_array<T,N-1>;
        
        template<typename T,typename Item,int StaticSize> struct flexible_obj {
            static const size_t base_size = sizeof(T);
            static const size_t item_size = 0;
            
            PyObject_HEAD
            
            fixed::init_array<Item,StaticSize> &items() { return _items; }
            const fixed::init_array<Item,StaticSize> &items() const { return _items; }
            
            flexible_obj(const flexible_obj&) = delete;
            
            void *operator new(size_t size,size_t items) {
                assert(items == StaticSize);
                void *ptr = PyObject_Malloc(size);
                if(!ptr) throw std::bad_alloc();
                return ptr;
            }
            
            void operator delete(void *ptr) {
                PyObject_Free(ptr);
            }
            
            template<typename F> flexible_obj(F item_init) : _items(StaticSize,item_init) {}
            
        private:
            fixed::init_array<Item,StaticSize> _items;
        };
    };
}

template<int N> struct smaller_store<fixed::matrix_store<N> > {
    static_assert(N > 1,"it can't get any smaller");
    
    typedef fixed::matrix_store<N-1> type;
};


template<int N> inline PyObject *to_pyobject(const fixed::vector<N,real> &v) {
    return reinterpret_cast<PyObject*>(new typename fixed::repr<N>::vector_obj(v));
}

template<int N> inline PyObject *to_pyobject(const fixed::matrix<N> &m) {
    return reinterpret_cast<PyObject*>(new typename fixed::repr<N>::matrix_obj(m));
}

template<int N> inline PyObject *to_pyobject(const _camera<fixed::camera_store<N> > &c) {
    return reinterpret_cast<PyObject*>(new fixed::camera_obj<N>(c));
}

#endif
