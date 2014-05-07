
#include <Python.h>
#include <structmember.h>
#include <exception>
#include <assert.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>

#include "pyobject.hpp"
#define RENDER_MODULE
#include "render.hpp"


#define SIMPLE_WRAPPER(ROOT) \
struct ROOT ## _obj_base { \
    CONTAINED_PYTYPE_DEF \
    PyObject_HEAD \
}; \
template<> struct _wrapped_type<ROOT> { \
    typedef simple_py_wrapper<ROOT,ROOT ## _obj_base> type; \
}


using namespace type_object_abbrev;

typedef char py_bool;
typedef unsigned char byte;


const int RENDER_CHUNK_SIZE = 32;
const int DEFAULT_SPECULAR_EXP = 8;

/* this is number of bits of the largest number that can be stored in a "long"
   across all platforms */
const byte MAX_BITSIZE = 31;

const byte MAX_PIXELSIZE = 16;


class already_running_error : public std::exception {
public:
    const char *what() const throw() {
        return "the renderer is already running";
    }
};


struct channel {
    float f_r, f_g, f_b, f_c;
    byte bit_size;
    py_bool tfloat;
};

SIMPLE_WRAPPER(channel);

PyMemberDef obj_Channel_members[] = {
    {const_cast<char*>("f_r"),T_FLOAT,offsetof(wrapped_type<channel>,base.f_r),READONLY,NULL},
    {const_cast<char*>("f_g"),T_FLOAT,offsetof(wrapped_type<channel>,base.f_g),READONLY,NULL},
    {const_cast<char*>("f_b"),T_FLOAT,offsetof(wrapped_type<channel>,base.f_b),READONLY,NULL},
    {const_cast<char*>("f_c"),T_FLOAT,offsetof(wrapped_type<channel>,base.f_c),READONLY,NULL},
    {const_cast<char*>("bit_size"),T_UBYTE,offsetof(wrapped_type<channel>,base.bit_size),READONLY,NULL},
    {const_cast<char*>("tfloat"),T_BOOL,offsetof(wrapped_type<channel>,base.tfloat),READONLY,NULL},
    {NULL}
};

PyTypeObject channel_obj_base::_pytype = make_type_object(
    "render.Channel",
    sizeof(wrapped_type<channel>),
    tp_dealloc = destructor_dealloc<wrapped_type<channel> >::value,
    tp_members = obj_Channel_members,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        try {
            auto ptr = py::check_obj(type->tp_alloc(type,0));
            try {
                auto &val = reinterpret_cast<wrapped_type<channel>*>(ptr)->alloc_base();
                
                const char *names[] = {"bit_size","f_r","f_g","f_b","f_c","tfloat"};
                get_arg ga(args,kwds,names,"Channel.__new__");
                int bit_size = from_pyobject<int>(ga(true));
                val.f_r = from_pyobject<float>(ga(true));
                val.f_g = from_pyobject<float>(ga(true));
                val.f_b = from_pyobject<float>(ga(true));
                val.f_c = 0;
                val.tfloat = 0;
            
                auto tmp = ga(false);
                if(tmp) val.f_c = from_pyobject<float>(tmp);
            
                tmp = ga(false);
                if(tmp) val.tfloat = char(from_pyobject<bool>(tmp));

                if(val.tfloat) {
                    if(bit_size != sizeof(float)*8) {
                        PyErr_Format(PyExc_ValueError,"if \"tfloat\" is true, \"bit_size\" can only be %d",sizeof(float)*8);
                        throw py_error_set();
                    }
                } else {
                    if(bit_size > MAX_BITSIZE) {
                        static_assert(MAX_BITSIZE < sizeof(float)*8,"the error message here doesn't make sense unless MAX_BITSIZE is smaller than 32");
                        PyErr_Format(PyExc_ValueError,"\"bit_size\" cannot be greater than %d (unless \"tfloat\" is true)",MAX_BITSIZE);
                        throw py_error_set();
                    } else if(bit_size < 1) THROW_PYERR_STRING(ValueError,"\"bit_size\" cannot be less than 1");
                }
                val.bit_size = bit_size;
            
                ga.finished();
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
            
            return ptr;
        } PY_EXCEPT_HANDLERS(nullptr)
    });


struct image_format {
    size_t width, height, pitch;
    std::vector<channel> channels;
    byte bytes_per_pixel;
    py_bool reversed;
};

SIMPLE_WRAPPER(image_format);

struct obj_ChannelList {
    CONTAINED_PYTYPE_DEF
    PY_MEM_NEW_DELETE
    PyObject_HEAD
    py::pyptr<wrapped_type<image_format> > parent;
    
    obj_ChannelList(wrapped_type<image_format> *parent) : parent(py::borrowed_ref(reinterpret_cast<PyObject*>(parent))) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),pytype());
    }
};

void im_check_buffer_size(const image_format &im,const Py_buffer &buff) {
    if(im.pitch < im.width * im.bytes_per_pixel) THROW_PYERR_STRING(ValueError,"invalid image format: \"pitch\" must be at least \"width\" times the pixel size in bytes");
    if(size_t(buff.len) < im.pitch * im.height) THROW_PYERR_STRING(ValueError,"the buffer is too small for an image with the given dimensions");
}

void im_set_channels(image_format &im,PyObject *arg) {
    auto channels_obj = py::iter(arg);
    std::vector<channel> channels;
    
    long bits = 0;
    while(auto item = py::next(channels_obj)) {
        auto &c = get_base<channel>(item.ref());
        bits += c.bit_size;
        channels.push_back(c);
    }
    if(bits > MAX_PIXELSIZE * 8) {
        PyErr_Format(PyExc_ValueError,"Too many bytes per pixel. The maximum is %d.",MAX_PIXELSIZE);
        throw py_error_set();
    }
    
    im.channels = std::move(channels);
    im.bytes_per_pixel = (bits + 7) / 8;
}

PyObject *obj_ImageFormat_set_channels(wrapped_type<image_format> *self,PyObject *arg) {
    try{
        im_set_channels(self->get_base(),arg);
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_ImageFormat_methods[] = {
    {"set_channels",reinterpret_cast<PyCFunction>(&obj_ImageFormat_set_channels),METH_O,NULL},
    {NULL}
};

PyGetSetDef obj_ImageFormat_getset[] = {
    {const_cast<char*>("channels"),OBJ_GETTER(
        wrapped_type<image_format>,
        reinterpret_cast<PyObject*>(new obj_ChannelList(self))),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_ImageFormat_members[] = {
    {const_cast<char*>("width"),member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.width),0,NULL},
    {const_cast<char*>("height"),member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.height),0,NULL},
    {const_cast<char*>("pitch"),member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.pitch),0,NULL},
    {const_cast<char*>("reversed"),T_BOOL,offsetof(wrapped_type<image_format>,base.reversed),0,NULL},
    {const_cast<char*>("bytes_per_pixel"),T_UBYTE,offsetof(wrapped_type<image_format>,base.bytes_per_pixel),READONLY,NULL},
    {NULL}
};

PyTypeObject image_format_obj_base::_pytype = make_type_object(
    "render.ImageFormat",
    sizeof(wrapped_type<image_format>),
    tp_dealloc = destructor_dealloc<wrapped_type<image_format> >::value,
    tp_members = obj_ImageFormat_members,
    tp_getset = obj_ImageFormat_getset,
    tp_methods = obj_ImageFormat_methods,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        try {
            auto ptr = py::check_obj(type->tp_alloc(type,0));
            try {
                auto &val = reinterpret_cast<wrapped_type<image_format>*>(ptr)->alloc_base();
                new(&val) image_format();
            
                const char *names[] = {"width","height","channels","pitch","reversed"};
                get_arg ga(args,kwds,names,"ImageFormat.__new__");
                val.width = from_pyobject<size_t>(ga(true));
                val.height = from_pyobject<size_t>(ga(true));
                auto channels = ga(true);
                val.pitch = 0;
                val.reversed = 0;
            
                auto tmp = ga(false);
                if(tmp) val.pitch = from_pyobject<size_t>(tmp);
            
                tmp = ga(false);
                if(tmp) val.reversed = from_pyobject<bool>(tmp) ? 1 : 0;
            
                ga.finished();
            
                im_set_channels(val,channels);
            
                if(val.pitch) {
                    if(val.pitch < val.width * val.bytes_per_pixel) THROW_PYERR_STRING(ValueError,"\"pitch\" must be at least \"width\" times the size of one pixel in bytes");
                } else val.pitch = val.width * val.bytes_per_pixel;
            } catch(...) {
                Py_DECREF(ptr);
                throw;
            }
            
            return ptr;
        } PY_EXCEPT_HANDLERS(nullptr)
    });


void check_index(const obj_ChannelList *cl,Py_ssize_t index) {
    if(index < 0 || size_t(index) >= cl->parent->get_base().channels.size()) THROW_PYERR_STRING(IndexError,"index out of range");
}

Py_ssize_t obj_ChannelList_len(obj_ChannelList *self) {
    return static_cast<Py_ssize_t>(self->parent->get_base().channels.size());
}

PyObject *obj_ChannelList_getitem(obj_ChannelList *self,Py_ssize_t index) {
    try {
        check_index(self,index);
        return to_pyobject(self->parent->get_base().channels[index]);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PySequenceMethods obj_ChannelList_sequence_methods = {
    reinterpret_cast<lenfunc>(&obj_ChannelList_len),
    NULL,
    NULL,
    reinterpret_cast<ssizeargfunc>(&obj_ChannelList_getitem),
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

PyTypeObject obj_ChannelList::_pytype = make_type_object(
    "render.ChannelList",
    sizeof(obj_ChannelList),
    tp_dealloc = destructor_dealloc<obj_ChannelList>::value,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_CHECKTYPES,
    tp_as_sequence = &obj_ChannelList_sequence_methods,
    tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the ChannelList type cannot be instantiated directly");
        return nullptr;
    });


struct renderer {
    volatile unsigned int busy_threads;
    volatile unsigned int job;
    image_format format;
    std::mutex mut;
    std::vector<std::thread> workers;
    scene *sc;
    Py_buffer buffer;
    std::atomic<unsigned int> chunk;
    volatile enum {NORMAL,CANCEL,QUIT} state;

protected:
    renderer() : busy_threads(0), job(0), state(NORMAL) {}
    ~renderer() {}
};

struct callback_renderer_obj_base;
template<typename Base> struct obj_Renderer;

struct callback_renderer : renderer {
    std::condition_variable barrier;
    PyObject *callback;

    callback_renderer(obj_Renderer<callback_renderer_obj_base> *self,unsigned int threads=0);
    ~callback_renderer();
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

struct callback_renderer_obj_base {
    typedef callback_renderer type;
    
    CONTAINED_PYTYPE_DEF
};

template<typename Base> struct obj_Renderer : Base {
    PyObject_HEAD
    storage_mode mode;
    
    typename Base::type base;
    
    PyObject *idict;
    PyObject *weaklist;
    
    ~obj_Renderer() {
        Py_XDECREF(idict);
        if(weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(this));
    }
    
    typename Base::type &cast_base() {
        return base;
    }
    typename Base::type &get_base() {
        switch(mode) {
        case CONTAINS:
            return base;
        default:
            PyErr_SetString(PyExc_RuntimeError,not_init_msg);
            throw py_error_set();
        }
    }
};

typedef obj_Renderer<callback_renderer_obj_base> obj_CallbackRenderer;

template<> struct _wrapped_type<callback_renderer> {
    typedef obj_CallbackRenderer type;
};


void worker_draw(renderer &r) {
    size_t chunks_x = (r.format.width + RENDER_CHUNK_SIZE - 1) / RENDER_CHUNK_SIZE;
    size_t chunks_y = (r.format.height + RENDER_CHUNK_SIZE - 1) / RENDER_CHUNK_SIZE;

    for(;;) {
        int chunk = r.chunk.fetch_add(1);
        size_t start_y = chunk/chunks_x;
        size_t start_x = chunk%chunks_x;
        if(start_y > chunks_y) break;
        start_x *= RENDER_CHUNK_SIZE;
        start_y *= RENDER_CHUNK_SIZE;
        
        for(size_t y = start_y; y < std::min(start_y+RENDER_CHUNK_SIZE,r.format.height); ++y) {
            byte *pixels = reinterpret_cast<byte*>(r.buffer.buf) + y * r.format.pitch + start_x * r.format.bytes_per_pixel;
            
            for(size_t x = start_x; x < std::min(start_x+RENDER_CHUNK_SIZE,r.format.width); ++x) {
                if(UNLIKELY(r.state != renderer::NORMAL)) return;

                color c = r.sc->calculate_color(x,y,r.format.width,r.format.height);
                int b_offset = 0;
                long temp[MAX_PIXELSIZE / sizeof(long)] = {0};
                for(auto &ch : r.format.channels) {
                    union {
                        float f;
                        uint32_t i;
                    } val;
                    
                    val.f = ch.f_r*c.r + ch.f_g*c.g + ch.f_b*c.b + ch.f_c;
                    if(val.f > 1) val.f = 1;
                    else if(val.f < 0) val.f = 0;
                    
                    unsigned long ival;
                    
                    if(ch.tfloat) {
                        assert(ch.bit_size == 32);
                        static_assert(sizeof(float) == 4,"A float is assumed to be 32 bits");
                        
                        ival = val.i;
                    } else {
                        assert(ch.bit_size < 32);
                        ival = std::round(val.f * (0xffffffffu >> (32 - ch.bit_size)));
                    }

                    int o = b_offset / (sizeof(long)*8);
                    int rm = b_offset % (sizeof(long)*8);
                    int s = sizeof(long)*8 - rm - ch.bit_size;
                    temp[o] |= s >= 0 ? ival << s : ival >> -s;
                    
                    if(rm + ch.bit_size > int(sizeof(long)*8))
                        temp[o+1] = ival << (sizeof(long)*16 - rm - ch.bit_size);
                    
                    b_offset += ch.bit_size;
                }

                if(r.format.reversed) {
                    for(int i = r.format.bytes_per_pixel-1; i >= 0; --i)
                        *pixels++ = (temp[i/sizeof(long)] >> ((sizeof(long) - 1 - (i % sizeof(long))) * 8)) & 0xff;
                } else {
                    for(int i = 0; i < r.format.bytes_per_pixel; ++i)
                        *pixels++ = (temp[i/sizeof(long)] >> ((sizeof(long) - 1 - (i % sizeof(long))) * 8)) & 0xff;
                }
            }
        }
    }
}

void callback_worker(obj_CallbackRenderer *self) {
    callback_renderer &r = self->base;
    
    if(!r.busy_threads) {
        std::unique_lock<std::mutex> lock(r.mut);
        
        do {
            if(UNLIKELY(r.state == renderer::QUIT)) return;

            // wait for the first job
            r.barrier.wait(lock);
        } while(!r.busy_threads);
    }

    for(;;) {
        worker_draw(r);

        {
            std::unique_lock<std::mutex> lock(r.mut);
            
            /* This needs to be set before calling the callback, in case the
               callback calls start_render. */
            unsigned int finished = r.job;

            if(--r.busy_threads == 0) {
                // when all the workers are finished
                
                r.sc->unlock();
                
                PyGILState_STATE gilstate = PyGILState_Ensure();
                
                PyBuffer_Release(&r.buffer);
                
                if(LIKELY(r.state == renderer::NORMAL)) {
                    // notify the main thread
                    
                    // in case the callback calls start_render/abort_render
                    lock.unlock();
                    try {
                        py::object(py::borrowed_ref(r.callback))(self);
                    } catch(py_error_set&) {
                        PyErr_Print();
                    } catch(std::exception &e) {
                        PySys_WriteStderr("error: %.500s\n",e.what());
                    }
                    lock.lock();
                } else if(r.state == renderer::CANCEL) {
                    /* If the job is being canceled, abort_render is waiting on 
                       this condition. The side-effect of waking the other 
                       workers is harmless. */
                    r.barrier.notify_all();
                }

                Py_DECREF(r.callback);
                Py_DECREF(self);
                
                PyGILState_Release(gilstate);
            }
            
            while(finished == r.job) {
                if(UNLIKELY(r.state == renderer::QUIT)) return;

                // wait for the next job
                r.barrier.wait(lock);
            }
        }
    }
}

callback_renderer::callback_renderer(obj_CallbackRenderer *self,unsigned int threads) {
    if(threads == 0) {
        threads = std::thread::hardware_concurrency();
        if(threads == 0) threads = 1;
    }
    workers.reserve(threads);
    for(unsigned int i=0; i<threads; ++i) workers.push_back(std::thread(callback_worker,self));
}

callback_renderer::~callback_renderer() {
    {
        py::allow_threads _;
        std::lock_guard<std::mutex> lock(mut);
        state = QUIT;
        barrier.notify_all();
    }
    
    for(auto &w : workers) w.join();
}


PyObject *obj_Scene_calculate_color(obj_Scene *self,PyObject *args,PyObject *kwds) {
    try {
        auto &sc = self->get_base();
        const char *names[] = {"x","y","width","height"};
        auto vals = get_arg::get_args<int,int,int,int>("Scene.calculate_color",names,args,kwds);
        
        color r;
        
        struct s_lock {
            scene &sc;
            s_lock(scene &sc) : sc(sc) { sc.lock(); }
            ~s_lock() { sc.unlock(); }
        } _(sc);
        
        {
            py::allow_threads __;
            r = apply(sc,&scene::calculate_color,vals);
        }
        
        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Scene_methods[] = {
    {"calculate_color",reinterpret_cast<PyCFunction>(&obj_Scene_calculate_color),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyTypeObject obj_Scene::_pytype = make_type_object(
    "render.Scene",
    sizeof(obj_Scene),
    tp_methods = obj_Scene_methods,
    tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the Scene type cannot be instantiated directly");
        return nullptr;
    });


template<typename T> void obj_Renderer_dealloc(wrapped_type<T> *self) {
    switch(self->mode) {
    case CONTAINS:
        self->~wrapped_type<T>();
        break;

    default:
        Py_XDECREF(self->idict);
        if(self->weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(self));
        break;
    }
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

void get_writable_buffer(PyObject *obj,Py_buffer &buff) {
    if(PyObject_GetBuffer(obj,&buff,PyBUF_WRITABLE))
#if PY_MAJOR_VERSION >= 3
        throw py_error_set();
#else
    {
        if(!PyErr_ExceptionMatches(PyExc_TypeError)) throw py_error_set();
        
        // try the old buffer protocol
        
        PyErr_Clear();
        
        void *bufptr;
        Py_ssize_t bufsize;
        
        if(PyObject_AsWriteBuffer(obj,&bufptr,&bufsize)) throw py_error_set();
        if(PyBuffer_FillInfo(&buff,obj,bufptr,bufsize,0,0)) throw py_error_set();
    }
#endif
}

PyObject *obj_CallbackRenderer_begin_render(obj_CallbackRenderer *self,PyObject *args,PyObject *kwds) {
    try {
        callback_renderer &r = self->get_base();
        
        const char *names[] = {"dest","format","scene","callback"};
        get_arg ga(args,kwds,names,"CallbackRenderer.begin_render");
        auto dest = ga(true);
        auto &format = get_base<image_format>(ga(true));
        auto &sc = get_base<scene>(ga(true));
        auto callback = ga(true);
        ga.finished();
        
        Py_buffer view;
        get_writable_buffer(dest,view);
        
        Py_INCREF(self);
        Py_INCREF(callback);
        
        try {
            im_check_buffer_size(format,view);
            
            py::allow_threads _; // without this, a dead-lock can occur
            std::lock_guard<std::mutex> lock(r.mut);
            
            if(r.busy_threads) throw already_running_error();
            
            assert(r.state == renderer::NORMAL);

            r.format = format;
            r.buffer = view;
            r.callback = callback;
            r.busy_threads = r.workers.size();
            r.chunk.store(0,std::memory_order_relaxed);
            r.sc = &sc;
            sc.lock();
            r.barrier.notify_all();
            ++r.job;
        } catch(...) {
            Py_DECREF(callback);
            Py_DECREF(self);
            PyBuffer_Release(&view);
            throw;
        }

        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_CallbackRenderer_abort_render(obj_CallbackRenderer *self,PyObject*) {
    try {
        callback_renderer &r = self->get_base();
        
        {
            py::allow_threads _; // without this, a dead-lock can occur
            std::unique_lock<std::mutex> lock(r.mut);
            
            if(r.busy_threads) {
                r.state = renderer::CANCEL;
                r.barrier.notify_all();
                do {
                    r.barrier.wait(lock);
                } while(r.busy_threads);
                r.state = renderer::NORMAL;
            }
        }
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_CallbackRenderer_methods[] = {
    {"begin_render",reinterpret_cast<PyCFunction>(&obj_CallbackRenderer_begin_render),METH_VARARGS|METH_KEYWORDS,NULL},
    {"abort_render",reinterpret_cast<PyCFunction>(&obj_CallbackRenderer_abort_render),METH_NOARGS,NULL},
    {NULL}
};

int obj_CallbackRenderer_init(obj_CallbackRenderer *self,PyObject *args,PyObject *kwds) {
    switch(self->mode) {
    case CONTAINS:
        self->base.~callback_renderer();
        break;
    default:
        assert(self->mode == UNINITIALIZED);
        self->mode = CONTAINS;
        break;
    }
    
    try {
        const char *names[] = {"threads"};
        get_arg ga(args,kwds,names,"CallbackRenderer.__init__");
        PyObject *temp = ga(false);
        unsigned int threads = temp ? from_pyobject<unsigned int>(temp) : 0;
        ga.finished();
        new(&self->base) callback_renderer(self,threads);
    } PY_EXCEPT_HANDLERS(-1)
    
    return 0;
}

PyTypeObject callback_renderer_obj_base::_pytype = make_type_object(
    "render.CallbackRenderer",
    sizeof(obj_CallbackRenderer),
    tp_dealloc = &obj_Renderer_dealloc<callback_renderer>,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &traverse_idict<obj_CallbackRenderer>,
    tp_clear = &clear_idict<obj_CallbackRenderer>,
    tp_weaklistoffset = offsetof(obj_CallbackRenderer,weaklist),
    tp_methods = obj_CallbackRenderer_methods,
    tp_dictoffset = offsetof(obj_CallbackRenderer,idict),
    tp_init = &obj_CallbackRenderer_init);


struct blocking_renderer : renderer {
    std::condition_variable start_cond, finish_cond;

    blocking_renderer(int threads=-1);
    ~blocking_renderer();
};

struct blocking_renderer_obj_base {
    typedef blocking_renderer type;
    
    CONTAINED_PYTYPE_DEF
};

typedef obj_Renderer<blocking_renderer_obj_base> obj_BlockingRenderer;

template<> struct _wrapped_type<blocking_renderer> {
    typedef obj_BlockingRenderer type;
};

bool wait_for_job(blocking_renderer &r,std::unique_lock<std::mutex> &lock) {
    unsigned int finished;
    do {
        if(UNLIKELY(r.state == renderer::QUIT)) return false;
        finished = r.job;

        r.start_cond.wait(lock);
    } while(finished == r.job);
    
    return true;
}

void blocking_worker(blocking_renderer &r) {
    if(!r.busy_threads) {
        std::unique_lock<std::mutex> lock(r.mut);
        if(!wait_for_job(r,lock)) return;
    }

    for(;;) {
        worker_draw(r);

        {
            std::unique_lock<std::mutex> lock(r.mut);

            if(--r.busy_threads == 0) {
                // when all the workers are finished, notify the main thread
                try {
                    r.finish_cond.notify_one();
                } catch(std::exception &e) {
                    PyGILState_STATE gilstate = PyGILState_Ensure();
                    PySys_WriteStderr("error: %.500s\n",e.what());
                    PyGILState_Release(gilstate);
                }
            }
            
            if(!wait_for_job(r,lock)) return;
        }
    }
}

blocking_renderer::blocking_renderer(int threads) : renderer() {
    if(threads < 0) {
        threads = int(std::thread::hardware_concurrency()) - 1;
        if(threads < 0) threads = 0;
    }
    if(threads) {
        workers.reserve(threads);
        for(int i=0; i<threads; ++i) workers.push_back(std::thread(blocking_worker,std::ref(*this)));
    }
}

blocking_renderer::~blocking_renderer() {
    if(!workers.empty()) {
        {
            py::allow_threads _;
            std::lock_guard<std::mutex> lock(mut);
            state = QUIT;
            start_cond.notify_all();
        }
        
        for(auto &w : workers) w.join();
    }
}

PyObject *obj_BlockingRenderer_render(obj_BlockingRenderer *self,PyObject *args,PyObject *kwds) {
    try {
        auto &r = self->get_base();
        
        const char *names[] = {"dest","format","scene"};
        get_arg ga(args,kwds,names,"BlockingRenderer.render");
        auto dest = ga(true);
        auto &fmt = get_base<image_format>(ga(true));
        auto &sc = get_base<scene>(ga(true));
        ga.finished();

        struct buffer {
            Py_buffer data;
            buffer(PyObject *dest) { get_writable_buffer(dest,data); }
            ~buffer() { PyBuffer_Release(&data); }
        } buff(dest);
        
        im_check_buffer_size(fmt,buff.data);
        
        bool finished;

        {
            py::allow_threads _;
            
            {
                std::lock_guard<std::mutex> lock(r.mut);
                
                if(r.busy_threads) throw already_running_error();
                
                r.format = fmt;
                r.buffer = buff.data;
                r.state = renderer::NORMAL;
                r.busy_threads = r.workers.size();
                r.chunk.store(0,std::memory_order_relaxed);
                r.sc = &sc;
                sc.lock();
                r.start_cond.notify_all();
                ++r.job;
            }
        
            worker_draw(r);
            
            {
                std::unique_lock<std::mutex> lock(r.mut);
                
                while(r.busy_threads) r.finish_cond.wait(lock);
                finished = r.state == renderer::NORMAL;
                sc.unlock();
            }
        }

        return to_pyobject(finished);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_BlockingRenderer_signal_abort(obj_BlockingRenderer *self,PyObject*) {
    try {
        auto &r = self->get_base();
        
        {
            py::allow_threads _;
            std::lock_guard<std::mutex> lock(r.mut);
            r.state = renderer::CANCEL;
        }
        
        Py_RETURN_NONE;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_BlockingRenderer_methods[] = {
    {"render",reinterpret_cast<PyCFunction>(&obj_BlockingRenderer_render),METH_VARARGS|METH_KEYWORDS,NULL},
    {"signal_abort",reinterpret_cast<PyCFunction>(&obj_BlockingRenderer_signal_abort),METH_NOARGS,NULL},
    {NULL}
};

int obj_BlockingRenderer_init(obj_BlockingRenderer *self,PyObject *args,PyObject *kwds) {
    switch(self->mode) {
    case CONTAINS:
        self->base.~blocking_renderer();
        break;
    default:
        assert(self->mode == UNINITIALIZED);
        self->mode = CONTAINS;
        break;
    }
    
    try {
        const char *names[] = {"threads"};
        get_arg ga(args,kwds,names,"BlockingRenderer.__init__");
        PyObject *temp = ga(false);
        int threads = temp ? from_pyobject<int>(temp) : -1;
        ga.finished();
        new(&self->base) blocking_renderer(threads);
    } PY_EXCEPT_HANDLERS(-1)
    
    return 0;
}

PyTypeObject blocking_renderer_obj_base::_pytype = make_type_object(
    "render.BlockingRenderer",
    sizeof(obj_BlockingRenderer),
    tp_dealloc = &obj_Renderer_dealloc<blocking_renderer>,
    tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_CHECKTYPES|Py_TPFLAGS_HAVE_GC,
    tp_traverse = &traverse_idict<obj_BlockingRenderer>,
    tp_clear = &clear_idict<obj_BlockingRenderer>,
    tp_weaklistoffset = offsetof(obj_BlockingRenderer,weaklist),
    tp_methods = obj_BlockingRenderer_methods,
    tp_dictoffset = offsetof(obj_BlockingRenderer,idict),
    tp_init = &obj_BlockingRenderer_init);


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


struct py_mem_deleter {
    void operator()(char *x) {
        PyMem_Free(x);
    }
};
std::unique_ptr<char,py_mem_deleter> f_to_s(float x) {
    char *r = PyOS_double_to_string(x,'r',0,0,nullptr);
    if(!r) throw py_error_set();
    return std::unique_ptr<char,py_mem_deleter>(r);
}

PyObject *obj_Material_repr(material *self) {
    try {
        return PYSTR(FromFormat)("Material((%s,%s,%s),%s,%s,%s,%s,(%s,%s,%s))",
            f_to_s(self->c.r).get(),
            f_to_s(self->c.g).get(),
            f_to_s(self->c.b).get(),
            f_to_s(self->opacity).get(),
            f_to_s(self->reflectivity).get(),
            f_to_s(self->specular_intensity).get(),
            f_to_s(self->specular_exp).get(),
            f_to_s(self->specular.r).get(),
            f_to_s(self->specular.g).get(),
            f_to_s(self->specular.b).get());
    } PY_EXCEPT_HANDLERS(nullptr)
}

void read_color(color &to,PyObject *from,const char *field) {
    if(PyTuple_Check(from)) {
        if(PyTuple_GET_SIZE(from) != 3) {
            if(field) PyErr_Format(PyExc_ValueError,"\"%s\" must have exactly 3 values",field);
            else PyErr_SetString(PyExc_ValueError,"object must have exactly 3 values");
            throw py_error_set();
        }
        to.r = from_pyobject<float>(PyTuple_GET_ITEM(from,0));
        to.g = from_pyobject<float>(PyTuple_GET_ITEM(from,1));
        to.b = from_pyobject<float>(PyTuple_GET_ITEM(from,2));
    } else {
        to = get_base<color>(from);
    }
}

PyObject *obj_Material_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        const char *names[] = {"color","opacity","reflectivity","specular_intensity","specular_exp","specular_color"};
        get_arg ga(args,kwds,names,"Material.__new__");
        auto obj_c = ga(true);
        float o = 1;
        float r = 0;
        float si = 1;
        float se = DEFAULT_SPECULAR_EXP;
        auto temp = ga(false);
        if(temp) o = from_pyobject<float>(temp);
        temp = ga(false);
        if(temp) r = from_pyobject<float>(temp);
        temp = ga(false);
        if(temp) si = from_pyobject<float>(temp);
        temp = ga(false);
        if(temp) se = from_pyobject<float>(temp);
        auto obj_s = ga(false);
        ga.finished();
        
        auto ptr = py::check_obj(type->tp_alloc(type,0));
        auto base = reinterpret_cast<material*>(ptr);
        
        read_color(base->c,obj_c,"color");
        if(obj_s) read_color(base->specular,obj_s,"specular_color");
        else base->specular = color(1,1,1);
        
        base->opacity = o;
        base->reflectivity = r;
        base->specular_intensity = si;
        base->specular_exp = se;
        
        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyGetSetDef obj_Material_getset[] = {
    {const_cast<char*>("color"),OBJ_GETTER(material,self->c),OBJ_BASE_SETTER(material,self->c),NULL,NULL},
    {const_cast<char*>("specular"),OBJ_GETTER(material,self->specular),OBJ_BASE_SETTER(material,self->specular),NULL,NULL},
    {NULL}
};

PyMemberDef obj_Material_members[] = {
    {const_cast<char*>("opacity"),T_FLOAT,offsetof(material,opacity),0,NULL},
    {const_cast<char*>("reflectivity"),T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {const_cast<char*>("specular_intensity"),T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {const_cast<char*>("specular_exp"),T_FLOAT,offsetof(material,reflectivity),0,NULL},
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

const package_common package_common_data = {
    &read_color
};

PyTypeObject *classes[] = {
    obj_Scene::pytype(),
    wrapped_type<channel>::pytype(),
    wrapped_type<image_format>::pytype(),
    obj_CallbackRenderer::pytype(),
    obj_BlockingRenderer::pytype(),
    wrapped_type<color>::pytype(),
    material::pytype()};


#if PY_MAJOR_VERSION >= 3
#define INIT_ERR_VAL nullptr

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

    obj_CallbackRenderer::pytype()->tp_new = PyType_GenericNew;
    obj_BlockingRenderer::pytype()->tp_new = PyType_GenericNew;

    for(auto cls : classes) {
        if(UNLIKELY(PyType_Ready(cls) < 0)) return INIT_ERR_VAL;
    }

#if PY_MAJOR_VERSION >= 3
    PyObject *m = PyModule_Create(&module_def);
#else
    PyObject *m = Py_InitModule3("render",func_table,0);
#endif
    if(UNLIKELY(!m)) return INIT_ERR_VAL;

    for(auto cls : classes) {
        add_class(m,cls->tp_name + sizeof("render"),cls);
    }
    
    auto cap = PyCapsule_New(const_cast<package_common*>(&package_common_data),"render._PACKAGE_COMMON",nullptr);
    if(!cap) return INIT_ERR_VAL;
    PyModule_AddObject(m,"_PACKAGE_COMMON",cap);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
