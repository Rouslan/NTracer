
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
#include <SDL_thread.h>
#include <atomic>
#include <memory>
#include <pygame/pygame.h>

#include "py_common.hpp"
#include "pyobject.hpp"
#include "scene.hpp"


using namespace type_object_abbrev;


const int FRAME_READY = SDL_USEREVENT;


class already_running_error : public std::exception {
public:
    const char *what() throw() {
        return "The renderer is already running";
    }
};

class sdl_error : public std::exception {
public:
    const char *what() throw() {
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
    Scene *scene;
    SDL_Surface *destination;
    std::atomic<unsigned int> line;
    PyObject *self;
    volatile enum {NORMAL,CANCEL,QUIT} state;

    renderer(PyObject *self,unsigned int threads=0);
    ~renderer();
};


struct obj_Scene {
    static PyTypeObject pytype;
    
    PyObject_HEAD

    /* a dummy member whose offset in the struct should be the same as any
       derived type's */
    union { void *a; long double b; } base;
    
    Scene &cast_base() {
        return reinterpret_cast<Scene&>(base);
    }
    Scene &get_base() {
        return reinterpret_cast<Scene&>(base);
    }
};

template<> struct wrapped_type<Scene> {
    typedef obj_Scene type;
};
template<> struct invariable_storage<Scene> {
    enum {value = 1};
};

struct obj_Renderer {
    static PyTypeObject pytype;
    
    PyObject_HEAD
    storage_mode mode;
    
    renderer base;
    
    PyObject *idict;
    PyObject *weaklist;
    
    PY_MEM_GC_NEW_DELETE

    obj_Renderer(_object * _0) : base(_0), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
        mode = CONTAINS;
    }
    obj_Renderer(_object * _0,unsigned int _1) : base(_0,_1), idict(NULL), weaklist(NULL) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
        mode = CONTAINS;
    }
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
};

template<> struct wrapped_type<renderer> {
    typedef obj_Renderer type;
};
template<> struct invariable_storage<renderer> {
    enum {value = 1};
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

                color c = r.scene->calculate_color(x,y,r.destination->w,r.destination->h);

                Uint32 pval = SDL_MapRGB(r.destination->format,to_byte(c.R),to_byte(c.G),to_byte(c.B));
                switch(r.destination->format->BytesPerPixel) {
                case 4:
                    *reinterpret_cast<Uint32*>(pixels) = pval;
                    break;
                case 3:
                    pixels[2] = (pval >> 16) & 0xff;
                case 2:
                    pixels[1] = (pval >> 8) & 0xff;
                case 1:
                    pixels[0] = pval & 0xff;
                    break;
                default:
                    assert(false);
                }
                pixels += r.destination->format->BytesPerPixel;
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
                r.scene->unlock();
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
        scoped_lock lock(mut);
        state = QUIT;
    }
    barrier.signal_all();
    for(unsigned int i=0; i<threads; ++i) SDL_WaitThread(workers[i],NULL);
}


PyObject *obj_Scene_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    PyErr_SetString(PyExc_TypeError,"the Scene type cannot be instantiated directly");
    return NULL;
}

PyTypeObject obj_Scene::pytype = make_type_object(
    "render.Scene",
    sizeof(obj_Scene),
    tp_new = &obj_Scene_new);


void obj_Renderer_dealloc(obj_Renderer *self) {
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
        Scene *scene = &get_base<Scene>(ga(true));
        ga.finished();
        
        Py_INCREF(self);
        try {
            py::AllowThreads _; // without this, a dead-lock can occur
            scoped_lock lock(r.mut);
            
            if(r.busy_threads) throw already_running_error();
            
            r.busy_threads = r.threads;
            r.line.store(0,std::memory_order_relaxed);
            ++r.job;
            r.scene = scene;
            scene->lock();
            try {
                r.destination = PySurface_AsSurface(dest);
                ++r.destination->refcount;
                r.barrier.signal_all();
            } catch(...) {
                r.scene->unlock();
                throw;
            }
        } catch(...) {
            Py_DECREF(self);
        }

        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(NULL)
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
    } PY_EXCEPT_HANDLERS(NULL)
}

PyMethodDef obj_Renderer_methods[] = {
    {"begin_render",reinterpret_cast<PyCFunction>(&obj_Renderer_begin_render),METH_VARARGS|METH_KEYWORDS,NULL},
    {"abort_render",reinterpret_cast<PyCFunction>(&obj_Renderer_abort_render),METH_NOARGS,NULL},
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

PyTypeObject obj_Renderer::pytype = make_type_object(
    "render.Renderer",
    sizeof(obj_Renderer),
    tp_dealloc = reinterpret_cast<destructor>(&obj_Renderer_dealloc),
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &obj_Renderer_traverse,
    tp_clear = &obj_Renderer_clear,
    tp_weaklistoffset = offsetof(obj_Renderer,weaklist),
    tp_methods = obj_Renderer_methods,
    tp_dictoffset = offsetof(obj_Renderer,idict),
    tp_init = &obj_Renderer_init);

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
    import_pygame_surface();
    if(PyErr_Occurred()) return INIT_ERR_VAL;

    import_pygame_event();
    if(PyErr_Occurred()) return INIT_ERR_VAL;
        
    if(UNLIKELY(PyType_Ready(&obj_Scene::pytype) < 0)) return INIT_ERR_VAL;
    obj_Renderer::pytype.tp_new = &PyType_GenericNew;
    if(UNLIKELY(PyType_Ready(&obj_Renderer::pytype) < 0)) return INIT_ERR_VAL;

#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3("render",func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;

    add_class(m,"Scene",&obj_Scene::pytype);
    add_class(m,"Renderer",&obj_Renderer::pytype);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
