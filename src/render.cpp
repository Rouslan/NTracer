
#if defined(_WIN32) || defined(__WIN32__)
    #define THREAD_WIN
#elif defined(linux) || defined(__linux) || defined(__linux__) || defined(sun) || defined(__sun) || defined(_AIX)
    #define THREAD_LINUX
#elif defined(__FreeBSD__) || defined(__MACOSX__) || defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
    #define THREAD_BSD
#elif defined(hpux) || defined(_hpux) || defined(__hpux)
    #define THEAD_HPUX
#elif defined(sgi) || defined(__sgi)
    #define THREAD_IRIX
#endif

#include <Python.h>

#if defined(THREAD_WIN)
#include <windows.h>
#elif defined(THREAD_LINUX) || defined(THREAD_IRIX)
#include <unistd.h>
#elif defined(THREAD_BSD)
#include <sys/param.h>
#include <sys/sysctl.h>
#elif defined(THREAD_HPUX)
#include <sys/mpctl.h>
#endif

#include <structmember.h>
#include <exception>
#include <assert.h>
#include <vector>
#include <forward_list>
#include <SDL_thread.h>
#include <atomic>
#include <memory>
#include <pygame/pygame.h>

#include "pyobject.hpp"
#define RENDER_MODULE
#include "render.hpp"


using namespace type_object_abbrev;


const int FRAME_READY = SDL_USEREVENT;


class already_running_error : public std::exception {
public:
    const char *what() const throw() {
        return "The renderer is already running";
    }
};

class sdl_error : public std::exception {
public:
    const char *what() const throw() {
        return SDL_GetError();
    }
};

class mutex {
    friend class scoped_lock;
    
    SDL_mutex *ptr;
public:
    mutex() : ptr(SDL_CreateMutex()) {}
    mutex(const mutex&) = delete;
    ~mutex() noexcept { SDL_DestroyMutex(ptr); }
    
    mutex &operator=(const mutex&) = delete;
};

class scoped_lock {
    friend class condition;
    
    SDL_mutex *mut;
public:
    scoped_lock(const mutex &m) : mut(m.ptr) {
        if(SDL_mutexP(mut)) throw sdl_error();
    }
    ~scoped_lock() noexcept {
#ifdef NDEBUG
        SDL_mutexV(mut);
#else
        assert(!SDL_mutexV(mut));
#endif
    }
    
    scoped_lock &operator=(const scoped_lock&) = delete;
};

class condition {
    SDL_cond *ptr;
public:
    condition() : ptr(SDL_CreateCond()) {}
    condition(const condition&) = delete;
    ~condition() noexcept { SDL_DestroyCond(ptr); }
    
    condition &operator=(const condition&) = delete;

    void wait(const scoped_lock &lock) const {
        if(SDL_CondWait(ptr,lock.mut)) throw sdl_error();
    }

    void signal_all() const {
        if(SDL_CondBroadcast(ptr)) throw sdl_error();
    }
};


struct renderer {
    unsigned int threads;
    volatile unsigned int busy_threads;
    volatile unsigned int job;
    condition barrier;
    mutex mut;
    std::vector<SDL_Thread*> workers;
    scene *sc;
    SDL_Surface *destination;
    std::atomic<unsigned int> line;
    PyObject *self;
    volatile enum {NORMAL,CANCEL,QUIT} state;

    renderer(PyObject *self,unsigned int threads=0);
    ~renderer();
};


struct obj_Scene {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
    
    scene &(obj_Scene::*_get_base)();

    scene &get_base() {
        return (this->*_get_base)();
    }
};

template<> struct _wrapped_type<scene> {
    typedef obj_Scene type;
};

struct obj_Renderer {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
    storage_mode mode;
    
    renderer base;
    
    PyObject *idict;
    PyObject *weaklist;
    
    ~obj_Renderer() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    renderer &cast_base() {
        return base;
    }
    renderer &get_base() {
        switch(mode) {
        case CONTAINS:
            return base;
        default:
            PyErr_SetString(PyExc_RuntimeError,not_init_msg);
            throw py_error_set();
        }
    }
    
    static std::forward_list<obj_Renderer*> instances;
    static void abort_all();
};
std::forward_list<obj_Renderer*> obj_Renderer::instances;

template<> struct _wrapped_type<renderer> {
    typedef obj_Renderer type;
};


inline PySurfaceObject *py_to_surface(PyObject *obj) {
    if(!PySurface_Check(obj)) {
        PyErr_SetString(PyExc_TypeError,"object is not an instance of pygame.Surface");
        throw py_error_set();
    }
    return reinterpret_cast<PySurfaceObject*>(obj);
}


template<typename T> inline T trunc(T n,T max) { return n > max ? max : n; }
typedef unsigned char byte;
byte to_byte(float val) {
    return byte(trunc(val,1.0f) * 255);
}


unsigned int non_neg(int x) {
    return x >= 0 ? static_cast<unsigned int>(x) : 0;
}

template<typename T> struct py_deleter {
    void operator()(T *ptr) const {
        py::free(ptr);
    }
};

unsigned int cpu_cores() {
#if defined(THREAD_WIN)
        
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);

    return sysinfo.dwNumberOfProcessors;

#elif defined(THREAD_LINUX)

    return non_neg(sysconf(_SC_NPROCESSORS_ONLN));

#elif defined(THREAD_BSD)

    int cores;
    int mib[4];
    size_t len = sizeof(cores); 

    mib[0] = CTL_HW;
    mib[1] = HW_AVAILCPU;
    sysctl(mib,2,&cores,&len,NULL,0);

    if(numCPU < 1) {
         mib[1] = HW_NCPU;
         sysctl(mib,2,&cores,&len,NULL,0);

         if(cores < 1) cores = 1;
    }

    return static_cast<unsigned int>(cores);

#elif defined(THREAD_HPUX)
    
    return non_neg(mpctl(MPC_GETNUMSPUS,NULL,NULL));

#elif defined(THREAD_IRIX)
    
    return non_neg(sysconf(_SC_NPROC_ONLN));
    
#else
#warning "don't know how to determine number of CPU cores"

    return 0;

#endif
}

void draw_pixel(const scene *sc,byte *&dest,const SDL_Surface *surface,int x,int y) {
    color c = sc->calculate_color(x,y,surface->w,surface->h);

    Uint32 pval = SDL_MapRGB(surface->format,to_byte(c.r),to_byte(c.g),to_byte(c.b));
    switch(surface->format->BytesPerPixel) {
    case 4:
        *reinterpret_cast<Uint32*>(dest) = pval;
        break;
    case 3:
        dest[2] = (pval >> 16) & 0xff;
    case 2:
        dest[1] = (pval >> 8) & 0xff;
    case 1:
        dest[0] = pval & 0xff;
        break;
    default:
        assert(false);
    }
    dest += surface->format->BytesPerPixel;
}

int worker(obj_Renderer *self) {
    renderer &r = self->base;
    
    // to minimize locking, each line is buffered
    std::unique_ptr<byte[]> buffer;
    int buffer_size = -1;

    
    if(!r.busy_threads) {
        scoped_lock lock(r.mut);
        
        do {
            if(UNLIKELY(r.state == renderer::QUIT)) return 0;

            // wait for the first job
            r.barrier.wait(lock);
        } while(!r.busy_threads);
    }

    for(;;) {
        int byte_width = r.destination->w * r.destination->format->BytesPerPixel;
        if(buffer_size < byte_width) {
            buffer.reset(new byte[byte_width]);
            buffer_size = byte_width;
        }

        int y;
        while((y = r.line.fetch_add(1)) < r.destination->h) {
            byte *pixels = buffer.get();

            for(int x = 0; x < r.destination->w; ++x) {
                if(UNLIKELY(r.state != renderer::NORMAL)) goto finish;

                draw_pixel(r.sc,pixels,r.destination,x,y);
            }

            if(SDL_MUSTLOCK(r.destination)) SDL_LockSurface(r.destination);
            memcpy(
                reinterpret_cast<byte*>(r.destination->pixels) + y * r.destination->pitch,
                buffer.get(),
                byte_width);
            if(SDL_MUSTLOCK(r.destination)) SDL_UnlockSurface(r.destination);
        }

    finish:
        {
            scoped_lock lock(r.mut);

            if(--r.busy_threads == 0) {
                // when all the workers are finished
                
                PyGILState_STATE gilstate = PyGILState_Ensure();
                
                if(LIKELY(r.state == renderer::NORMAL)) {
                    // notify the main thread
                    try {
                        SDL_Event e;
                        py::dict attr;
                        py::object pye(py::new_ref(py::check_obj(PyEvent_New2(FRAME_READY,attr.ref()))));
                        attr["source"] = py::borrowed_ref(reinterpret_cast<PyObject*>(self));
                        if(PyEvent_FillUserEvent(reinterpret_cast<PyEventObject*>(pye.ref()),&e)) throw py_error_set();
                        SDL_PushEvent(&e);
                    } catch(py_error_set&) {
                        PyErr_Print();
                    } catch(std::exception &e) {
                        PySys_WriteStderr("error: %.500s\n",e.what());
                    }
                } else if(r.state == renderer::CANCEL) {
                    /* If the job is being canceled, abort_render is waiting on 
                       this condition. The side-effect of waking the other 
                       workers is harmless. */
                    r.barrier.signal_all();
                }
                r.sc->unlock();
                SDL_FreeSurface(r.destination);
                Py_DECREF(self);
                
                PyGILState_Release(gilstate);
            }

            unsigned int finished;
            do {
                if(UNLIKELY(r.state == renderer::QUIT)) return 0;
                finished = r.job;

                // wait for the next job
                r.barrier.wait(lock);
            } while(finished == r.job);
        }
    }
}

renderer::renderer(PyObject *self,unsigned int _threads) : threads(_threads), busy_threads(0), job(0), self(self), state(NORMAL) {
    if(threads == 0) {
        threads = cpu_cores();
        if(threads == 0) threads = 1;
    }
    workers.reserve(threads);
    for(unsigned int i=0; i<threads; ++i) workers.push_back(SDL_CreateThread(reinterpret_cast<int (*)(void*)>(&worker),self));
}

renderer::~renderer() {
    /* we don't have to worry about a worker needing the GIL, because an extra
       reference to "self" will prevent the destructor from being called while
       a job is being processed */
    assert(!busy_threads);
    
    {
        //py::AllowThreads _;
        scoped_lock lock(mut);
        state = QUIT;
        //barrier.signal_all();
     }
    barrier.signal_all();
    
    for(unsigned int i=0; i<threads; ++i) SDL_WaitThread(workers[i],NULL);
}


PyObject *obj_Scene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    PyErr_SetString(PyExc_TypeError,"the Scene type cannot be instantiated directly");
    return nullptr;
}

PyTypeObject obj_Scene::_pytype = make_type_object(
    "render.Scene",
    sizeof(obj_Scene),
    tp_new = &obj_Scene_new);


void obj_Renderer_dealloc(obj_Renderer *self) {
    auto last = obj_Renderer::instances.before_begin();
    for(auto itr = obj_Renderer::instances.begin(); itr != obj_Renderer::instances.end(); last=itr++) {
        if(*itr == self) {
            obj_Renderer::instances.erase_after(last);
            break;
        }
    }
    
    switch(self->mode) {
    case CONTAINS:
        self->~obj_Renderer();
        break;

    default:
        Py_XDECREF(self->idict);
        if(self->weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(self));
        break;
    }
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

int obj_Renderer_traverse(obj_Renderer *self,visitproc visit,void *arg) {
    Py_VISIT(self->idict);

    return 0;
}

int obj_Renderer_clear(obj_Renderer *self) {
    Py_CLEAR(self->idict);

    return 0;
}

PyObject *obj_Renderer_begin_render(obj_Renderer *self,PyObject *args,PyObject *kwds) {
    try {
        renderer &r = self->get_base();
        
        const char *names[] = {"dest","scene"};
        get_arg ga(args,kwds,names,"Renderer.begin_render");
        PySurfaceObject *dest = py_to_surface(ga(true));
        scene *sc = &get_base<scene>(ga(true));
        ga.finished();
        
        Py_INCREF(self);
        try {
            py::AllowThreads _; // without this, a dead-lock can occur
            scoped_lock lock(r.mut);
            
            if(r.busy_threads) throw already_running_error();
            
            r.busy_threads = r.threads;
            r.line.store(0,std::memory_order_relaxed);
            ++r.job;
            r.sc = sc;
            sc->lock();
            try {
                r.destination = PySurface_AsSurface(dest);
                ++r.destination->refcount;
                r.barrier.signal_all();
            } catch(...) {
                r.sc->unlock();
                throw;
            }
        } catch(...) {
            Py_DECREF(self);
        }

        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Renderer_abort_render(obj_Renderer *self,PyObject*) {
    try {
        renderer &r = self->get_base();
        
        {
            py::AllowThreads _; // without this, a dead-lock can occur
            scoped_lock lock(r.mut);
            
            if(r.busy_threads) {
                r.state = renderer::CANCEL;
                r.barrier.signal_all();
                do {
                    r.barrier.wait(lock);
                } while(r.busy_threads);
                r.state = renderer::NORMAL;
            }
        }
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Renderer_render_sync(PyObject*,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"dest","scene"};
        get_arg ga(args,kwds,names,"Renderer.render_sync");
        PySurfaceObject *dest = py_to_surface(ga(true));
        scene *sc = &get_base<scene>(ga(true));
        ga.finished();
        
        SDL_Surface *surface = PySurface_AsSurface(dest);
        
        {
            py::AllowThreads _;
            
            if(SDL_MUSTLOCK(surface)) SDL_LockSurface(surface);
            for(int y=0; y<surface->h; ++y) {
                auto line = reinterpret_cast<byte*>(surface->pixels) + y*surface->pitch;
                
                for(int x=0; x<surface->w; ++x) {
                    draw_pixel(sc,line,surface,x,y);
                }
            }
            if(SDL_MUSTLOCK(surface)) SDL_UnlockSurface(surface);
        }
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Renderer_methods[] = {
    {"begin_render",reinterpret_cast<PyCFunction>(&obj_Renderer_begin_render),METH_VARARGS|METH_KEYWORDS,NULL},
    {"abort_render",reinterpret_cast<PyCFunction>(&obj_Renderer_abort_render),METH_NOARGS,NULL},
    {"render_sync",reinterpret_cast<PyCFunction>(&obj_Renderer_render_sync),METH_STATIC|METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

int obj_Renderer_init(obj_Renderer *self,PyObject *args,PyObject *kwds) {
    switch(self->mode) {
    case CONTAINS:
        self->base.~renderer();
        break;
    default:
        assert(self->mode == UNINITIALIZED);
        self->mode = CONTAINS;
        break;
    }
    
    try {
        const char *names[] = {"threads"};
        get_arg ga(args,kwds,names,"Renderer.__init__");
        PyObject *temp = ga(false);
        unsigned int threads = temp ? from_pyobject<unsigned int>(temp) : 0;
        ga.finished();
        new(&self->base) renderer(reinterpret_cast<PyObject*>(self),threads);
    } PY_EXCEPT_HANDLERS(-1)
    
    return 0;
}

PyTypeObject obj_Renderer::_pytype = make_type_object(
    "render.Renderer",
    sizeof(obj_Renderer),
    tp_dealloc = reinterpret_cast<destructor>(&obj_Renderer_dealloc),
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &obj_Renderer_traverse,
    tp_clear = &obj_Renderer_clear,
    tp_weaklistoffset = offsetof(obj_Renderer,weaklist),
    tp_methods = obj_Renderer_methods,
    tp_dictoffset = offsetof(obj_Renderer,idict),
    tp_init = &obj_Renderer_init,
    
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        auto r = type->tp_alloc(type,0);
        if(!r) return nullptr;
        try {
            obj_Renderer::instances.push_front(reinterpret_cast<obj_Renderer*>(r));
        } catch(std::bad_alloc&) {
            Py_DECREF(r);
            PyErr_NoMemory();
            return nullptr;
        }
        return r;
    });

void obj_Renderer::abort_all() {
    for(auto inst : instances) {
        if(inst->mode == CONTAINS) {
            auto r = obj_Renderer_abort_render(inst,nullptr);
            if(r) Py_DECREF(r);
        }
    }
}


PyObject *obj_Color_repr(wrapped_type<color> *self) {
    auto &base = self->get_base();
    PyObject *r = nullptr;
    char *cr;
    char *cg = nullptr;
    char *cb = nullptr;
    
    if((cr = PyOS_double_to_string(base.r,'r',0,0,nullptr))) {
        if((cg = PyOS_double_to_string(base.g,'r',0,0,nullptr))) {
            if((cb = PyOS_double_to_string(base.b,'r',0,0,nullptr))) {
                r = PYSTR(FromFormat)("Color(%s,%s,%s)",cr,cg,cb);
            }
        }
    }
    
    PyMem_Free(cr);
    PyMem_Free(cg);
    PyMem_Free(cb);
    
    return r;
}

PyObject *obj_Color_richcompare(wrapped_type<color> *self,PyObject *arg,int op) {
    color *cb = get_base_if_is_type<color>(arg);
    
    if((op == Py_EQ || op == Py_NE) && cb) {
        return to_pyobject((self->get_base() == *cb) == (op == Py_EQ));
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Color___neg__(wrapped_type<color> *self) {
    try {
        return to_pyobject(-self->get_base());
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Color___add__(PyObject *a,PyObject *b) {
    color *ca, *cb;
    
    if((ca = get_base_if_is_type<color>(a)) && (cb = get_base_if_is_type<color>(b))) {
        return to_pyobject(*ca + *cb);
    }
    
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Color___sub__(PyObject *a,PyObject *b) {
    color *ca, *cb;
    
    if((ca = get_base_if_is_type<color>(a)) && (cb = get_base_if_is_type<color>(b))) {
        return to_pyobject(*ca - *cb);
    }
        
    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Color___mul__(PyObject *a,PyObject *b) {
    color *ca, *cb;
 
    if((ca = get_base_if_is_type<color>(a))) {
        if((cb = get_base_if_is_type<color>(b))) return to_pyobject(*ca * *cb);
        if(PyNumber_Check(b)) return to_pyobject(*ca * from_pyobject<float>(b));
    } else if(PyNumber_Check(a)) {
        assert(PyObject_TypeCheck(b,&wrapped_type<n_vector>::pytype));
        return to_pyobject(from_pyobject<float>(a) * reinterpret_cast<wrapped_type<color>*>(b)->get_base());
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyObject *obj_Color___div__(PyObject *a,PyObject *b) {
    color *ca, *cb;
 
    if((ca = get_base_if_is_type<color>(a))) {
        if((cb = get_base_if_is_type<color>(b))) return to_pyobject(*ca / *cb);
        if(PyNumber_Check(b)) return to_pyobject(*ca / from_pyobject<float>(b));
    }

    Py_INCREF(Py_NotImplemented);
    return Py_NotImplemented;
}

PyNumberMethods obj_Color_number_methods = {
    &obj_Color___add__,
    &obj_Color___sub__,
    &obj_Color___mul__,
#if PY_MAJOR_VERSION < 3
    &obj_Color___div__,
#endif
    NULL,
    NULL,
    NULL,
    reinterpret_cast<unaryfunc>(&obj_Color___neg__),
    NULL,
    NULL,
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
    &obj_Color___div__,
    NULL,
    NULL,
    NULL
};

/*Py_ssize_t obj_Color___sequence_len__(PyObject *self) {
    return 3;
}

void c_index_check(const n_vector &v,Py_ssize_t index) {
    if(index < 0 || index >= 3) THROW_PYERR_STRING(IndexError,"color index out of range");
}

PyObject *obj_Color___sequence_getitem__(wrapped_type<color> *self,Py_ssize_t index) {
    try {
        auto &v = self->get_base();
        v_index_check(v,index);
        return to_pyobject(v[index]);
    } PY_EXCEPT_HANDLERS(NULL)
}

PySequenceMethods obj_Color_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_Color___sequence_len__),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_Color___sequence_getitem__),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};*/

PyObject *obj_Color_apply(wrapped_type<color> *self,PyObject *_func) {
    try {
        auto &base = self->get_base();
        py::object func = py::borrowed_ref(_func);

        return to_pyobject(color(
            from_pyobject<float>(func(base.r)),
            from_pyobject<float>(func(base.g)),
            from_pyobject<float>(func(base.b))));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Color_methods[] = {
    {"apply",reinterpret_cast<PyCFunction>(&obj_Color_apply),METH_O,NULL},
    {NULL}
};

PyObject *obj_Color_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"r","g","b"};
        get_arg ga(args,kwds,names,"Color.__new__");
        auto r = from_pyobject<float>(ga(true));
        auto g = from_pyobject<float>(ga(true));
        auto b = from_pyobject<float>(ga(true));
        ga.finished();

        auto ptr = py::check_obj(type->tp_alloc(type,0));
        new(&reinterpret_cast<wrapped_type<color>*>(ptr)->alloc_base()) color(r,g,b);
        
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMemberDef obj_Color_members[] = {
    {const_cast<char*>("r"),T_FLOAT,offsetof(wrapped_type<color>,base.r),READONLY,NULL},
    {const_cast<char*>("g"),T_FLOAT,offsetof(wrapped_type<color>,base.g),READONLY,NULL},
    {const_cast<char*>("b"),T_FLOAT,offsetof(wrapped_type<color>,base.b),READONLY,NULL},
    {NULL}
};

PyTypeObject color_obj_base::_pytype = make_type_object(
    "render.Color",
    sizeof(wrapped_type<color>),
    tp_dealloc = destructor_dealloc<wrapped_type<color> >::value,
    tp_repr = &obj_Color_repr,
    tp_as_number = &obj_Color_number_methods,
    //tp_as_sequence = &obj_Color_sequence_methods,
    tp_richcompare = &obj_Color_richcompare,
    tp_methods = obj_Color_methods,
    tp_members = obj_Color_members,
    tp_new = &obj_Color_new);


PyObject *obj_Material_repr(material *self) {
    PyObject *ret = nullptr;
    char *cr;
    char *cg = nullptr;
    char *cb = nullptr;
    char *o = nullptr;
    char *r = nullptr;
    
    if((cr = PyOS_double_to_string(self->c.r,'r',0,0,nullptr))) {
        if((cg = PyOS_double_to_string(self->c.g,'r',0,0,nullptr))) {
            if((cb = PyOS_double_to_string(self->c.b,'r',0,0,nullptr))) {
                if(self->reflectivity == 0 && self->opacity == 1) {
                    ret = PYSTR(FromFormat)("Material((%s,%s,%s))",cr,cg,cb);
                } else if((o = PyOS_double_to_string(self->opacity,'r',0,0,nullptr))) {
                    if(self->reflectivity == 0) {
                        ret = PYSTR(FromFormat)("Material((%s,%s,%s),%s)",cr,cg,cb,o);
                    } else if((r = PyOS_double_to_string(self->reflectivity,'r',0,0,nullptr))) {
                        ret = PYSTR(FromFormat)("Material((%s,%s,%s),%s,%s)",cr,cg,cb,o,r);
                    }
                }
            }
        }
    }
    
    PyMem_Free(cr);
    PyMem_Free(cg);
    PyMem_Free(cb);
    PyMem_Free(o);
    PyMem_Free(r);
    
    return ret;
}

PyObject *obj_Material_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"color","opacity","reflectivity"};
        get_arg ga(args,kwds,names,"Material.__new__");
        auto obj_c = ga(true);
        float o = 1;
        float r = 0;
        auto temp = ga(false);
        if(temp) o = from_pyobject<float>(temp);
        temp = ga(false);
        if(temp) r = from_pyobject<float>(temp);
        ga.finished();
        
        auto ptr = py::check_obj(type->tp_alloc(type,0));
        auto base = reinterpret_cast<material*>(ptr);
        
        if(PyTuple_Check(obj_c)) {
            if(PyTuple_GET_SIZE(obj_c) != 3) {
                PyErr_SetString(PyExc_ValueError,"\"color\" must have exactly 3 values");
                return nullptr;
            }
            base->c.r = from_pyobject<float>(PyTuple_GET_ITEM(obj_c,0));
            base->c.g = from_pyobject<float>(PyTuple_GET_ITEM(obj_c,1));
            base->c.b = from_pyobject<float>(PyTuple_GET_ITEM(obj_c,2));
        } else {
            base->c = get_base<color>(obj_c);
        }
        
        base->opacity = o;
        base->reflectivity = r;
        
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

int obj_Material_set_color(material *self,PyObject *arg,void*) {
    try {
        setter_no_delete(arg);
        self->c = get_base<color>(arg);
        return 0;
    } PY_EXCEPT_HANDLERS(-1)
}

PyGetSetDef obj_Material_getset[] = {
    {const_cast<char*>("color"),OBJ_GETTER(material,self->c),reinterpret_cast<setter>(&obj_Material_set_color),NULL,NULL},
    {NULL}
};

PyMemberDef obj_Material_members[] = {
    {const_cast<char*>("opacity"),T_FLOAT,offsetof(material,opacity),0,NULL},
    {const_cast<char*>("reflectivity"),T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {NULL}
};

PyTypeObject material::_pytype = make_type_object(
    "render.Material",
    sizeof(material),
    tp_dealloc = destructor_dealloc<material>::value,
    tp_repr = &obj_Material_repr,
    tp_members = obj_Material_members,
    tp_getset = obj_Material_getset,
    tp_new = &obj_Material_new);


PyMethodDef func_table[] = {
    {NULL}
};

#if PY_MAJOR_VERSION >= 3
#define INIT_ERR_VAL NULL

struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "render",
    NULL,
    0,
    func_table,
    NULL,
    NULL,
    NULL,
    NULL
};

extern "C" SHARED(PyObject) * PyInit_render(void) {
#else
#define INIT_ERR_VAL

extern "C" SHARED(void) initrender(void) {
#endif
    import_pygame_base();
    if(PyErr_Occurred()) return INIT_ERR_VAL;

    import_pygame_surface();
    if(PyErr_Occurred()) return INIT_ERR_VAL;

    import_pygame_event();
    if(PyErr_Occurred()) return INIT_ERR_VAL;
        
    if(UNLIKELY(PyType_Ready(obj_Scene::pytype()) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(obj_Renderer::pytype()) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(wrapped_type<color>::pytype()) < 0)) return INIT_ERR_VAL;
    if(UNLIKELY(PyType_Ready(material::pytype()) < 0)) return INIT_ERR_VAL;

#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3("render",func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;

    add_class(m,"Scene",obj_Scene::pytype());
    add_class(m,"Renderer",obj_Renderer::pytype());
    add_class(m,"Color",wrapped_type<color>::pytype());
    add_class(m,"Material",material::pytype());
    
    /* When PyGame shuts down, it destroys all surface objects regardless of
       reference counts, so any threads working with surfaces need to be stopped
       first. */
    PyGame_RegisterQuit(&obj_Renderer::abort_all);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
