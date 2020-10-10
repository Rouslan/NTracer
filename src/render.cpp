
#include "py_common.hpp"

#include <structmember.h>
#include <exception>
#include <assert.h>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <unordered_map>

#include "pyobject.hpp"
#define RENDER_MODULE
#include "render.hpp"


#define FULL_MODULE_STR "ntracer.render"

#define SIMPLE_WRAPPER(ROOT) \
struct ROOT ## _obj_base { \
    CONTAINED_PYTYPE_DEF \
    PyObject_HEAD \
}; \
template<> struct _wrapped_type<ROOT> { \
    typedef simple_py_wrapper<ROOT,ROOT ## _obj_base> type; \
}

#define GET_TRACERN_PREFIX "ntracer.tracer"


typedef char py_bool;
typedef unsigned char byte;


const int RENDER_CHUNK_SIZE = 32;
const int DEFAULT_SPECULAR_EXP = 8;

/* this is number of bits of the largest number that can be stored in a "long"
   across all platforms */
const byte MAX_BITSIZE = 31;

const byte MAX_PIXELSIZE = 16;

const char tracerx_capsule_name[] = "_CONSTRUCTORS";


void encode_float_ieee754(char *str,int length,const float *data);
py::bytes encode_float_ieee754(int length,const float *data);


struct tracerx_cache_item {
    const tracerx_constructors *ctrs;

    /* This is a non-owning reference. The module will call invalidate_reference
       upon destruction to remove this. */
    PyObject *mod;
};

struct instance_data_t {
    std::unordered_map<int,tracerx_cache_item> tracerx_cache;
    PyObject *color_unpickle;
    PyObject *material_unpickle;
    PyObject *vector_unpickle;
    PyObject *matrix_unpickle;
    PyObject *triangle_unpickle;
    PyObject *solid_unpickle;
};

inline instance_data_t &get_instance_data(PyObject *mod) {
    return *reinterpret_cast<instance_data_t*>(PyModule_GetState(mod));
}

instance_data_t &get_instance_data();


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
    {"f_r",T_FLOAT,offsetof(wrapped_type<channel>,base.f_r),READONLY,NULL},
    {"f_g",T_FLOAT,offsetof(wrapped_type<channel>,base.f_g),READONLY,NULL},
    {"f_b",T_FLOAT,offsetof(wrapped_type<channel>,base.f_b),READONLY,NULL},
    {"f_c",T_FLOAT,offsetof(wrapped_type<channel>,base.f_c),READONLY,NULL},
    {"bit_size",T_UBYTE,offsetof(wrapped_type<channel>,base.bit_size),READONLY,NULL},
    {"tfloat",T_BOOL,offsetof(wrapped_type<channel>,base.tfloat),READONLY,NULL},
    {NULL}
};

PyTypeObject channel_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Channel",
    .tp_basicsize = sizeof(wrapped_type<channel>),
    .tp_dealloc = destructor_dealloc<wrapped_type<channel> >::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_members = obj_Channel_members,
    .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
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
    }};


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
    {"channels",OBJ_GETTER(
        wrapped_type<image_format>,
        reinterpret_cast<PyObject*>(new obj_ChannelList(self))),NULL,NULL,NULL},
    {NULL}
};

PyMemberDef obj_ImageFormat_members[] = {
    {"width",member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.width),0,NULL},
    {"height",member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.height),0,NULL},
    {"pitch",member_macro<size_t>::value,offsetof(wrapped_type<image_format>,base.pitch),0,NULL},
    {"reversed",T_BOOL,offsetof(wrapped_type<image_format>,base.reversed),0,NULL},
    {"bytes_per_pixel",T_UBYTE,offsetof(wrapped_type<image_format>,base.bytes_per_pixel),READONLY,NULL},
    {NULL}
};

PyTypeObject image_format_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".ImageFormat",
    .tp_basicsize = sizeof(wrapped_type<image_format>),
    .tp_dealloc = destructor_dealloc<wrapped_type<image_format> >::value,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_ImageFormat_methods,
    .tp_members = obj_ImageFormat_members,
    .tp_getset = obj_ImageFormat_getset,
    .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
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
    }};


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
    .sq_length = reinterpret_cast<lenfunc>(&obj_ChannelList_len),
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_ChannelList_getitem)
};

PyTypeObject obj_ChannelList::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".ChannelList",
    .tp_basicsize = sizeof(obj_ChannelList),
    .tp_dealloc = destructor_dealloc<obj_ChannelList>::value,
    .tp_as_sequence = &obj_ChannelList_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = [](PyTypeObject*,PyObject*,PyObject*) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the ChannelList type cannot be instantiated directly");
        return nullptr;
    }};


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

                    val.f = ch.f_r*c.r() + ch.f_g*c.g() + ch.f_b*c.b() + ch.f_c;
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

        auto vals = get_arg::get_args("Scene.calculate_color",args,kwds,
            param<int>("x"),
            param<int>("y"),
            param<int>("width"),
            param<int>("height"));

        color r;

        struct s_lock {
            scene &sc;
            s_lock(scene &sc) : sc(sc) { sc.lock(); }
            ~s_lock() { sc.unlock(); }
        } _(sc);

        {
            py::allow_threads __;
            r = sc.calculate_color(std::get<0>(vals),std::get<1>(vals),std::get<2>(vals),std::get<3>(vals));
        }

        return to_pyobject(r);
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Scene_methods[] = {
    {"calculate_color",reinterpret_cast<PyCFunction>(&obj_Scene_calculate_color),METH_VARARGS|METH_KEYWORDS,NULL},
    {NULL}
};

PyTypeObject obj_Scene::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Scene",
    .tp_basicsize = sizeof(obj_Scene),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_Scene_methods,
    .tp_new = [](PyTypeObject *type,PyObject *args,PyObject *kwds) -> PyObject* {
        PyErr_SetString(PyExc_TypeError,"the Scene type cannot be instantiated directly");
        return nullptr;
    }};


template<typename T> void obj_Renderer_dealloc(wrapped_type<T> *self) {
    switch(self->mode) {
    case CONTAINS:
        std::destroy_at(self);
        break;

    default:
        Py_XDECREF(self->idict);
        if(self->weaklist) PyObject_ClearWeakRefs(reinterpret_cast<PyObject*>(self));
        break;
    }
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

void get_writable_buffer(PyObject *obj,Py_buffer &buff) {
    if(PyObject_GetBuffer(obj,&buff,PyBUF_WRITABLE)) throw py_error_set();
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

PyTypeObject callback_renderer_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".CallbackRenderer",
    .tp_basicsize = sizeof(obj_CallbackRenderer),
    .tp_dealloc = reinterpret_cast<destructor>(&obj_Renderer_dealloc<callback_renderer>),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &traverse_idict<obj_CallbackRenderer>,
    .tp_clear = &clear_idict<obj_CallbackRenderer>,
    .tp_weaklistoffset = offsetof(obj_CallbackRenderer,weaklist),
    .tp_methods = obj_CallbackRenderer_methods,
    .tp_dictoffset = offsetof(obj_CallbackRenderer,idict),
    .tp_init = reinterpret_cast<initproc>(&obj_CallbackRenderer_init)};


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

PyTypeObject blocking_renderer_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".BlockingRenderer",
    .tp_basicsize = sizeof(obj_BlockingRenderer),
    .tp_dealloc = reinterpret_cast<destructor>(&obj_Renderer_dealloc<blocking_renderer>),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE|Py_TPFLAGS_HAVE_GC,
    .tp_traverse = &traverse_idict<obj_BlockingRenderer>,
    .tp_clear = &clear_idict<obj_BlockingRenderer>,
    .tp_weaklistoffset = offsetof(obj_BlockingRenderer,weaklist),
    .tp_methods = obj_BlockingRenderer_methods,
    .tp_dictoffset = offsetof(obj_BlockingRenderer,idict),
    .tp_init = reinterpret_cast<initproc>(&obj_BlockingRenderer_init)};


PyObject *obj_Color_repr(wrapped_type<color> *self) {
    auto &base = self->get_base();
    PyObject *r = nullptr;
    char *cr;
    char *cg = nullptr;
    char *cb = nullptr;

    if((cr = PyOS_double_to_string(base.r(),'r',0,0,nullptr))) {
        if((cg = PyOS_double_to_string(base.g(),'r',0,0,nullptr))) {
            if((cb = PyOS_double_to_string(base.b(),'r',0,0,nullptr))) {
                r = PyUnicode_FromFormat("Color(%s,%s,%s)",cr,cg,cb);
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
        assert(PyObject_TypeCheck(b,wrapped_type<color>::pytype()));
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
    .nb_add = &obj_Color___add__,
    .nb_subtract = &obj_Color___sub__,
    .nb_multiply = &obj_Color___mul__,
    .nb_negative = reinterpret_cast<unaryfunc>(&obj_Color___neg__),
    .nb_true_divide = &obj_Color___div__
};

Py_ssize_t obj_Color___sequence_len__(PyObject *self) {
    return 3;
}

void c_index_check(Py_ssize_t index) {
    if(index < 0 || index >= 3) THROW_PYERR_STRING(IndexError,"color index out of range");
}

PyObject *obj_Color___sequence_getitem__(wrapped_type<color> *self,Py_ssize_t index) {
    try {
        auto &c = self->get_base();
        c_index_check(index);
        return to_pyobject(c.vals[index]);
    } PY_EXCEPT_HANDLERS(NULL)
}

PySequenceMethods obj_Color_sequence_methods = {
    .sq_length = [](PyObject*) { return Py_ssize_t(3); },
    .sq_item = reinterpret_cast<ssizeargfunc>(&obj_Color___sequence_getitem__)
};

PyObject *obj_Color_apply(wrapped_type<color> *self,PyObject *_func) {
    try {
        auto &base = self->get_base();
        py::object func = py::borrowed_ref(_func);

        return to_pyobject(color(
            from_pyobject<float>(func(base.r())),
            from_pyobject<float>(func(base.g())),
            from_pyobject<float>(func(base.b()))));
    } PY_EXCEPT_HANDLERS(NULL)
}

PyObject *obj_Color_reduce(wrapped_type<color> *self,PyObject*) {
    try {
        return py::make_tuple(
            get_instance_data().color_unpickle,
            py::make_tuple(encode_float_ieee754(3,self->get_base().vals))).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMethodDef obj_Color_methods[] = {
    {"apply",reinterpret_cast<PyCFunction>(&obj_Color_apply),METH_O,NULL},
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Color_reduce),METH_NOARGS,NULL},
    immutable_copy,
    immutable_deepcopy,
    {NULL}
};

PyObject *obj_Color_new(PyTypeObject *type,PyObject *args,PyObject *kwds) {
    try {
        auto vals = get_arg::get_args("Color.__new__",args,kwds,
            param<float>("r"),
            param<float>("g"),
            param<float>("b"));

        auto ptr = py::check_obj(type->tp_alloc(type,0));
        new(&reinterpret_cast<wrapped_type<color>*>(ptr)->alloc_base()) color(
            std::get<0>(vals),
            std::get<1>(vals),
            std::get<2>(vals));

        return ptr;
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyMemberDef obj_Color_members[] = {
    {"r",T_FLOAT,offsetof(wrapped_type<color>,base.vals[0]),READONLY,NULL},
    {"g",T_FLOAT,offsetof(wrapped_type<color>,base.vals[1]),READONLY,NULL},
    {"b",T_FLOAT,offsetof(wrapped_type<color>,base.vals[2]),READONLY,NULL},
    {NULL}
};

PyTypeObject color_obj_base::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Color",
    .tp_basicsize = sizeof(wrapped_type<color>),
    .tp_dealloc = destructor_dealloc<wrapped_type<color> >::value,
    .tp_repr = reinterpret_cast<reprfunc>(&obj_Color_repr),
    .tp_as_number = &obj_Color_number_methods,
    .tp_as_sequence = &obj_Color_sequence_methods,
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_richcompare = reinterpret_cast<richcmpfunc>(&obj_Color_richcompare),
    .tp_methods = obj_Color_methods,
    .tp_members = obj_Color_members,
    .tp_new = &obj_Color_new};


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
        return PyUnicode_FromFormat("Material((%s,%s,%s),%s,%s,%s,%s,(%s,%s,%s))",
            f_to_s(self->c.r()).get(),
            f_to_s(self->c.g()).get(),
            f_to_s(self->c.b()).get(),
            f_to_s(self->opacity).get(),
            f_to_s(self->reflectivity).get(),
            f_to_s(self->specular_intensity).get(),
            f_to_s(self->specular_exp).get(),
            f_to_s(self->specular.r()).get(),
            f_to_s(self->specular.g()).get(),
            f_to_s(self->specular.b()).get());
    } PY_EXCEPT_HANDLERS(nullptr)
}

void read_color(color &to,PyObject *from,const char *field) {
    if(PyTuple_Check(from)) {
        if(PyTuple_GET_SIZE(from) != 3) {
            if(field) PyErr_Format(PyExc_ValueError,"\"%s\" must have exactly 3 values",field);
            else PyErr_SetString(PyExc_ValueError,"object must have exactly 3 values");
            throw py_error_set();
        }
        to.r() = from_pyobject<float>(PyTuple_GET_ITEM(from,0));
        to.g() = from_pyobject<float>(PyTuple_GET_ITEM(from,1));
        to.b() = from_pyobject<float>(PyTuple_GET_ITEM(from,2));
    } else {
        to = get_base<color>(from);
    }
}

PyObject *obj_Material_reduce(material *self,PyObject*) {
    try {
        py::bytes data(10 * sizeof(float));
        encode_float_ieee754(data.data(),3,self->c.vals);
        encode_float_ieee754(data.data()+3*sizeof(float),3,self->specular.vals);
        encode_float_ieee754(data.data()+6*sizeof(float),1,&self->opacity);
        encode_float_ieee754(data.data()+7*sizeof(float),1,&self->reflectivity);
        encode_float_ieee754(data.data()+8*sizeof(float),1,&self->specular_intensity);
        encode_float_ieee754(data.data()+9*sizeof(float),1,&self->specular_exp);

        return py::make_tuple(get_instance_data().material_unpickle,py::make_tuple(data)).new_ref();
    } PY_EXCEPT_HANDLERS(nullptr)
}

PyObject *obj_Material_copy(material *self,PyObject*) {
    material *r;
    try {
        r = new material();
    } catch(std::bad_alloc&) {
        PyErr_NoMemory();
        return nullptr;
    }

    r->c = self->c;
    r->specular = self->specular;
    r->opacity = self->opacity;
    r->reflectivity = self->reflectivity;
    r->specular_intensity = self->specular_intensity;
    r->specular_exp = self->specular_exp;

    return reinterpret_cast<PyObject*>(r);
}

PyMethodDef obj_Material_methods[] = {
    {"__reduce__",reinterpret_cast<PyCFunction>(&obj_Material_reduce),METH_NOARGS,NULL},
    {"__copy__",reinterpret_cast<PyCFunction>(&obj_Material_copy),METH_NOARGS,NULL},
    {"__deepcopy__",reinterpret_cast<PyCFunction>(&obj_Material_copy),METH_O,NULL},
    {NULL}
};

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
    {"color",OBJ_GETTER(material,self->c),OBJ_BASE_SETTER(material,self->c),NULL,NULL},
    {"specular",OBJ_GETTER(material,self->specular),OBJ_BASE_SETTER(material,self->specular),NULL,NULL},
    {NULL}
};

PyMemberDef obj_Material_members[] = {
    {"opacity",T_FLOAT,offsetof(material,opacity),0,NULL},
    {"reflectivity",T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {"specular_intensity",T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {"specular_exp",T_FLOAT,offsetof(material,reflectivity),0,NULL},
    {NULL}
};

PyTypeObject material::_pytype = {
    PyVarObject_HEAD_INIT(nullptr,0)
    .tp_name = FULL_MODULE_STR ".Material",
    .tp_basicsize = sizeof(material),
    .tp_dealloc = destructor_dealloc<material>::value,
    .tp_repr = reinterpret_cast<reprfunc>(&obj_Material_repr),
    .tp_flags = Py_TPFLAGS_DEFAULT|Py_TPFLAGS_BASETYPE,
    .tp_methods = obj_Material_methods,
    .tp_members = obj_Material_members,
    .tp_getset = obj_Material_getset,
    .tp_new = &obj_Material_new};


// like tracerx_cache_item except owns reference to mod
struct held_cache_item {
    const tracerx_constructors *ctrs;
    py::object mod;
};

held_cache_item get_tracerx_cache_item(PyObject *mod,int dim) {
    auto &cache = get_instance_data(mod).tracerx_cache;

    held_cache_item item;

    auto itr = cache.find(dim);
    if(itr != cache.end()) {
        item.mod = py::borrowed_ref(std::get<1>(*itr).mod);
        item.ctrs = std::get<1>(*itr).ctrs;
        return item;
    }

    constexpr int blen = sizeof(GET_TRACERN_PREFIX) + std::numeric_limits<int>::digits10 + 1;
    char buffer[blen];
    const char *mod_name = buffer;

#ifndef NDEBUG
    int printed =
#endif
    PyOS_snprintf(buffer,blen,GET_TRACERN_PREFIX "%d",dim);
    assert(printed > 0 && printed < blen);

    try {
        item.mod = py::import_module(buffer);
    } catch(py_error_set&) {
        if(!PyErr_ExceptionMatches(PyExc_ImportError)) throw;

        PyErr_Clear();
        mod_name = GET_TRACERN_PREFIX "n";
        item.mod = py::import_module(mod_name);
    }

    item.ctrs = reinterpret_cast<const tracerx_constructors*>(PyCapsule_GetPointer(static_cast<py::object>(item.mod.attr(tracerx_capsule_name)).ref(),tracerx_capsule_name));
    if(!item.ctrs) throw py_error_set();

    cache[dim] = {item.ctrs,item.mod.ref()};
    return item;
}


int get_dimension(PyObject *o) {
    int dim = from_pyobject<int>(o);
    if(dim < 3) THROW_PYERR_STRING(ValueError,"dimension cannot be less than 3");
    return dim;
}

inline void copy_byteswap_dwords(char *dest,const char *src,int length) {
    for(int i=0; i<length; ++i) {
        dest[i*4] = src[i*4+3];
        dest[i*4+1] = src[i*4+2];
        dest[i*4+2] = src[i*4+1];
        dest[i*4+3] = src[i*4];
    }
}

void encode_float_ieee754(char *str,int length,const float *data) {
#if FLOAT_NATIVE_FORMAT == FORMAT_IEEE_BIG
    for(int i=0; i<length; ++i) reinterpret_cast<float*>(str)[i] = data[i];
#elif FLOAT_NATIVE_FORMAT == FORMAT_IEEE_LITTLE
    copy_byteswap_dwords(str,reinterpret_cast<const char*>(data),length);
#else
    for(int i=0; i<length; ++i) {
        uint32_t buff = 0;
        if(std::signbit(data[i])) buff = 1 << 31;

        if(std::isinf(data[i])) buff |= 0xff << 23;
        else if(std::isnan(data[i]) THROW_PYERR_STRING(ValueError,"cannot pack value of NaN on non-IEEE platform");
        else if(data[i] != 0) {
            int exp;
            float sig = std::abs(std:frexp(data[i],&exp));
            exp += 126;
            assert(exp >= 0 && exp < 256);
            buff |= exp << 23;
            buff |= static_cast<uint32_t>(std::ldexp(sig,24)) & 0x7fffff;
        }

  #if NATIVE_BYTEORDER == BYTEORDER_BIG
        reinterpret_cast<uint32_t*>(str)[i] = buff;
  #else
        str[i*4] = buff >> 24;
        str[i*4+1] = (buff >> 16) & 0xff;
        str[i*4+2] = (buff >> 8) & 0xff;
        str[i*4+3] = buff & 0xff;
  #endif
    }
#endif
}

py::bytes encode_float_ieee754(int length,const float *data) {
    py::bytes encoded{length*4};
    encode_float_ieee754(encoded.data(),length,data);
    return encoded;
}

void decode_float_ieee754(const char *str,int length,float *data) {
#if FLOAT_NATIVE_FORMAT == FORMAT_IEEE_BIG
    for(i=0; i<length; ++i) data[i] = reinterpret_cast<const float*>(str)[i];
#elif FLOAT_NATIVE_FORMAT == FORMAT_IEEE_LITTLE
    copy_byteswap_dwords(reinterpret_cast<char*>(data),str,length);
#else
    typedef std::numeric_limits<float> limits;

    for(int i=0; i<length; ++i) {
        uint32_t buff;

  #if NATIVE_BYTEORDER == BYTEORDER_BIG
        buff = reinterpret_cast<const uint32_t*>(str)[i];
  #else
        auto bytes = reinterpret_cast<const unsigned char*>(str);
        buff = bytes[i*4] << 24;
        buff |= bytes[i*4+1] << 16;
        buff |= bytes[i*4+2] << 8;
        buff |= bytes[i*4+3];
  #endif

        int exp = (buff >> 23) & 0xff;
        int sig = buff & 0x7fffff;
        float val = 0;
        if(exp == 0xff) {
            if(sig) {
                THROW_PYERR_STRING(ValueError,"cannot unpack value of NaN on non-IEEE platform");
            } else {
                if(limits::has_infinity) val = limits::infinity();
                else THROW_PYERR_STRING(ValueError,"cannot unpack value of infinity because the float on this system cannot represent it");
            }
        } else if(exp) {
            val = std::ldexp(static_cast<float>(sig | 0x800000),exp - 150);
        }
        if(buff & (1 << 31)) val = -val;
        data[i] = val;
    }
#endif
}


PyMethodDef func_table[] = {
    {"get_optimized_tracern",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                return get_tracerx_cache_item(mod,get_dimension(arg)).mod.new_ref();
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_O,NULL},
    {"_color_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto str = from_pyobject<py::bytes>(arg);
                if(str.size() != 3 * sizeof(float)) {
                    PyErr_SetString(PyExc_ValueError,"color data is malformed");
                    return nullptr;
                }

                color c;
                decode_float_ieee754(str.data(),3,c.vals);
                return to_pyobject(c);
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_O,NULL},
    {"_material_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto str = from_pyobject<py::bytes>(arg);
                if(str.size() != 10 * sizeof(float)) {
                    PyErr_SetString(PyExc_ValueError,"material data is malformed");
                    return nullptr;
                }

                float vals[10];
                decode_float_ieee754(str.data(),10,vals);
                auto m = new material();
                m->c.r() = vals[0];
                m->c.g() = vals[1];
                m->c.b() = vals[2];
                m->specular.r() = vals[3];
                m->specular.g() = vals[4];
                m->specular.b() = vals[5];
                m->opacity = vals[6];
                m->reflectivity = vals[7];
                m->specular_intensity = vals[8];
                m->specular_exp = vals[9];
                return reinterpret_cast<PyObject*>(m);
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_O,NULL},
    {"_vector_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto args = from_pyobject<py::tuple>(arg);
                if(args.size() != 2) THROW_PYERR_STRING(TypeError,"_vector_unpickle takes exactly 2 arguments");

                int dim = get_dimension(args[0].ref());
                auto str = from_pyobject<py::bytes>(args[1]);
                if(str.size() != dim * Py_ssize_t(sizeof(float))) {
                    PyErr_SetString(PyExc_ValueError,"vector data is malformed");
                    return nullptr;
                }

                auto item = get_tracerx_cache_item(mod,dim);
                auto objdata = item.ctrs->vector(dim);
                decode_float_ieee754(str.data(),dim,objdata.data);
                return objdata.obj.new_ref();
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_VARARGS,NULL},
    {"_matrix_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto args = from_pyobject<py::tuple>(arg);
                if(args.size() != 2) THROW_PYERR_STRING(TypeError,"_matrix_unpickle takes exactly 2 arguments");

                int dim = get_dimension(args[0].ref());
                auto str = from_pyobject<py::bytes>(args[1]);
                if(str.size() != dim * dim * Py_ssize_t(sizeof(float))) {
                    PyErr_SetString(PyExc_ValueError,"matrix data is malformed");
                    return nullptr;
                }

                auto item = get_tracerx_cache_item(mod,dim);
                auto objdata = item.ctrs->matrix(dim);
                decode_float_ieee754(str.data(),dim*dim,objdata.data);
                return objdata.obj.new_ref();
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_VARARGS,NULL},
    {"_triangle_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto args = from_pyobject<py::tuple>(arg);
                if(args.size() != 3) THROW_PYERR_STRING(TypeError,"_triangle_unpickle takes exactly 3 arguments");

                int dim = get_dimension(args[0].ref());
                auto str = from_pyobject<py::bytes>(args[1]);
                if(str.size() != dim * (dim+1) * Py_ssize_t(sizeof(float))) {
                    PyErr_SetString(PyExc_ValueError,"triangle data is malformed");
                    return nullptr;
                }
                auto mat = checked_py_cast<material>(args[2].ref());

                auto item = get_tracerx_cache_item(mod,dim);
                auto objdata = item.ctrs->triangle(dim,mat);
                for(int i=0; i<dim+1; ++i) decode_float_ieee754(str.data() + sizeof(float)*dim*i,dim,objdata.data[i]);
                item.ctrs->triangle_extra(objdata.obj.ref());

                return objdata.obj.new_ref();
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_VARARGS,NULL},
    {"_solid_unpickle",[](PyObject *mod,PyObject *arg) -> PyObject* {
            try {
                auto args = from_pyobject<py::tuple>(arg);
                if(args.size() != 3) THROW_PYERR_STRING(TypeError,"_solid_unpickle takes exactly 3 arguments");

                int dim = get_dimension(args[0].ref());
                auto str = from_pyobject<py::bytes>(args[1]);
                if(str.size() != dim * (dim + 1) * Py_ssize_t(sizeof(float)) + 1) {
                    PyErr_SetString(PyExc_ValueError,"solid data is malformed");
                    return nullptr;
                }
                if(str.data()[0] != 1 && str.data()[1] != 2) {
                    PyErr_SetString(PyExc_ValueError,"solid data is corrupt");
                    return nullptr;
                }
                auto mat = checked_py_cast<material>(args[2].ref());

                auto item = get_tracerx_cache_item(mod,dim);
                auto objdata = item.ctrs->solid(dim,str.data()[0],mat);
                decode_float_ieee754(str.data() + 1,dim*dim,objdata.orientation);
                decode_float_ieee754(str.data() + dim*dim*sizeof(float) + 1,dim,objdata.position);
                item.ctrs->solid_extra(objdata.obj.ref());

                return objdata.obj.new_ref();
            } PY_EXCEPT_HANDLERS(nullptr)
        },METH_VARARGS,NULL},
    {NULL}
};


PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    "render",
    NULL,
    sizeof(instance_data_t),
    func_table,
    NULL,
    NULL,
    NULL,
    [](void *ptr) {
        get_instance_data(reinterpret_cast<PyObject*>(ptr)).~instance_data_t();
    }
};

inline instance_data_t &get_instance_data() {
    return get_instance_data(PyState_FindModule(&module_def));
}


const package_common package_common_data = {
    &read_color,
    [](int dim,const float *data) {
        return py::make_tuple(
            get_instance_data().vector_unpickle,
            py::make_tuple(dim,encode_float_ieee754(dim,data))).new_ref();
    },
    [](int dim,const float *data) {
        return py::make_tuple(
            get_instance_data().matrix_unpickle,
            py::make_tuple(dim,encode_float_ieee754(dim*dim,data))).new_ref();
    },
    [](int dim,const float *const *data,material *m) -> PyObject* {
        py::bytes values{Py_ssize_t(sizeof(float))*dim*(dim+1)};

        for(int i=0; i<dim+1; ++i) encode_float_ieee754(values.data() + sizeof(float)*dim*i,dim,data[i]);

        return py::make_tuple(
            get_instance_data().triangle_unpickle,
            py::make_tuple(dim,values,reinterpret_cast<PyObject*>(m))).new_ref();
    },
    [](int dim,char type,const float *orientation,const float *position,material *m) -> PyObject* {
        py::bytes values{Py_ssize_t(sizeof(float))*dim*(dim+1)+1};

        values.data()[0] = type;
        encode_float_ieee754(values.data() + 1,dim*dim,orientation);
        encode_float_ieee754(values.data() + sizeof(float)*dim*dim + 1,dim,position);

        return py::make_tuple(
            get_instance_data().solid_unpickle,
            py::make_tuple(dim,values,reinterpret_cast<PyObject*>(m))).new_ref();
    },
    [](PyObject *mod) -> void {
        assert(mod);

        auto &idata = get_instance_data();

        auto itr = idata.tracerx_cache.begin();
        while(itr != idata.tracerx_cache.end()) {
            if(std::get<1>(*itr).mod == mod) itr = idata.tracerx_cache.erase(itr);
            else ++itr;
        }
    }
};

PyTypeObject *classes[] = {
    obj_Scene::pytype(),
    wrapped_type<channel>::pytype(),
    wrapped_type<image_format>::pytype(),
    obj_CallbackRenderer::pytype(),
    obj_BlockingRenderer::pytype(),
    wrapped_type<color>::pytype(),
    material::pytype()};


extern "C" SHARED(PyObject) * PyInit_render(void) {
    if(!PyEval_ThreadsInitialized()) PyEval_InitThreads();

    obj_CallbackRenderer::pytype()->tp_new = PyType_GenericNew;
    obj_BlockingRenderer::pytype()->tp_new = PyType_GenericNew;

    for(auto cls : classes) {
        if(UNLIKELY(PyType_Ready(cls) < 0)) return nullptr;
    }

    PyObject *m = PyModule_Create(&module_def);
    if(UNLIKELY(!m)) return nullptr;

    instance_data_t *idata;

    try {
        idata = new(PyModule_GetState(m)) instance_data_t();
    } catch(std::bad_alloc&) {
        PyErr_NoMemory();
        return nullptr;
    } catch(std::exception &e) {
        PyErr_SetString(PyExc_RuntimeError,e.what());
        return nullptr;
    }

#define LOAD_IDATA(NAME) \
    idata->NAME = PyObject_GetAttrString(m,"_" #NAME); \
    if(!idata->NAME) return nullptr; \
    Py_DECREF(idata->NAME)

    LOAD_IDATA(color_unpickle);
    LOAD_IDATA(material_unpickle);
    LOAD_IDATA(vector_unpickle);
    LOAD_IDATA(matrix_unpickle);
    LOAD_IDATA(triangle_unpickle);
    LOAD_IDATA(solid_unpickle);

    for(auto cls : classes) {
        add_class(m,cls->tp_name + sizeof(FULL_MODULE_STR),cls);
    }

    auto cap = PyCapsule_New(const_cast<package_common*>(&package_common_data),"render._PACKAGE_COMMON",nullptr);
    if(!cap) return nullptr;
    PyModule_AddObject(m,"_PACKAGE_COMMON",cap);

    return m;
}
