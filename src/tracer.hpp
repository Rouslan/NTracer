#ifndef tracer_hpp
#define tracer_hpp

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <deque>
#include <new>

#include "geometry.hpp"
#include "light.hpp"
#include "render.hpp"
#include "camera.hpp"
#include "pyobject.hpp"
#include "instrumentation.hpp"


#define ITR_RANGE(X) std::begin(X),std::end(X)


const real ROUNDING_FUZZ = std::numeric_limits<real>::epsilon() * 10;
const size_t QUICK_LIST_PREALLOC = 10;
const size_t ALL_HITS_LIST_PREALLOC = 20;

/* Checking if anything occludes a light is expensive, so if the light from a
   point light is going to be dimmer than this, don't bother. */
const real LIGHT_THRESHOLD = real(1)/512;

//#define NO_SIMD_BATCHES
#ifdef NO_SIMD_BATCHES
typedef simd::scalar<real> v_real;
#else
typedef simd::v_type<real> v_real;
#endif


const int KD_DEFAULT_MAX_DEPTH = v_real::size > 1 ? 25 : 18;

// only split nodes if there are more than this many primitives
const int KD_DEFAULT_SPLIT_THRESHOLD = 2;


template<typename Store> class ray {
public:
    ray(size_t d,geom_allocator *a=nullptr) : origin(d,a), direction(d,a) {}
    template<typename T1,typename T2> ray(T1 &&o,T2 &&d,geom_allocator *a=nullptr) : origin{std::forward<T1>(o),a}, direction{std::forward<T2>(d),a} {
        assert(origin.dimension() == direction.dimension());
    }

    size_t dimension() const { return origin.dimension(); }

    vector<Store> origin;
    vector<Store> direction;
};

template<typename Store> struct flat_origin_ray_source {
    real half_w;
    real half_h;
    real fovI;

    void set_params(int w,int h,real fov) {
        half_w = real(w)/real(2);
        half_h = real(h)/real(2);
        fovI = std::tan(fov/2) / half_w;
    }

    HOT_FUNC vector<Store> operator()(const camera<Store> &cam,real x,real y,geom_allocator *a=nullptr) const {
        return {
            (cam.forward() + cam.right() * (fovI * (x - half_w)) - cam.up() * (fovI * (y - half_h))).unit(),
            a};
    }
};

template<typename Store> real hypercube_intersects(
    const ray<Store> &target,
    ray<Store> &normal,
    real cutoff=std::numeric_limits<real>::max());

template<typename Store> class box_scene : public scene {
public:
    size_t locked;
    real fov;
    flat_origin_ray_source<Store> origin_source;

    camera<Store> cam;

    box_scene(size_t d) : locked(0), fov(real(0.8)), cam(d) {}

    void set_view_size(int w,int h) {
        origin_source.set_params(w,h,fov);
    }

    geom_allocator *new_allocator() const {
        return Store::new_allocator(dimension(),5);
    }

    color calculate_color(int x,int y,geom_allocator *a) const {
        const ray<Store> view{
            vector<Store>{cam.origin,shallow_copy},
            origin_source(cam,static_cast<real>(x),static_cast<real>(y),a)};
        ray<Store> normal{dimension(),a};
        if(hypercube_intersects<Store>(view,normal)) {
            real sine = dot(view.direction,normal.direction);
            return (sine <= 0 ? -sine : real(0)) * color(1,0.5,0.5);
        }

        real intensity = dot(view.direction,vector<Store>::axis(dimension(),0));
        return intensity > 0 ? color(intensity,intensity,intensity) :
            color(0,-intensity,-intensity);
    }

    size_t dimension() const { return cam.dimension(); }

    void lock() { ++locked; }
    void unlock() noexcept {
        assert(locked);
        --locked;
    }
};


template<typename Store> HOT_FUNC real hypercube_intersects(const ray<Store> &target,ray<Store> &normal,real cutoff)
{
    assert(target.dimension() == normal.dimension());
    INSTRUMENTATION_TIMER;

    for(size_t i=0; i<target.dimension(); ++i) {
        if(target.direction[i]) {
            normal.origin[i] = target.direction[i] < 0 ? real(1) : real(-1);
            real dist = (normal.origin[i] - target.origin[i]) / target.direction[i];
            if(dist > 0) {
                for(size_t j=0; j<target.dimension(); ++j) {
                    if(i != j) {
                        normal.origin[j] = target.direction[j] * dist + target.origin[j];
                        if(std::abs(normal.origin[j]) > (1+ROUNDING_FUZZ)) goto miss;
                    }
                }
                if(dist >= cutoff) return 0;
                normal.direction = vector<Store>::axis(target.dimension(),i,normal.origin[i]);
                return dist;

            miss: ;
            }
        }
    }

    return 0;
}

template<typename Store> HOT_FUNC real hypersphere_intersects(
    const ray<Store> &target,
    ray<Store> &normal,
    real cutoff=std::numeric_limits<real>::max())
{
    INSTRUMENTATION_TIMER;

    real a = target.direction.square();
    real b = 2 * dot(target.direction,target.origin);
    real c = target.origin.square() - 1;

    real discriminant = b*b - 4*a*c;
    if(discriminant < 0) return 0;

    real dist = (-b - std::sqrt(discriminant)) / (2 * a);
    if(dist <= 0 || dist >= cutoff) return 0;

    normal.direction = normal.origin = target.origin + target.direction * dist;
    return dist;
}


template<typename Store> struct primitive : py::pyobj_subclass {
    real intersects(
        const ray<Store> &target,
        ray<Store> &normal,
        real cutoff=std::numeric_limits<real>::max(),
        geom_allocator *a=nullptr) const;
    size_t dimension() const;

    PyObject_HEAD
    py::pyptr<material> m;

    bool opaque() const {
        return m->opacity >= 1;
    }

protected:
    primitive(material *m,PyTypeObject *t) : m(py::borrowed_ref(m)) {
        PyObject_Init(py::ref(this),t);
    }

    ~primitive() = default;
};

template<typename Store> struct primitive_batch : py::pyobj_subclass {
    real intersects(
        const ray<Store> &target,
        ray<Store> &normal,
        int &index,
        real cutoff=std::numeric_limits<real>::max(),
        geom_allocator *a=nullptr) const;
    size_t dimension() const;

    PyObject_HEAD
    py::pyptr<material> m[v_real::size];

    bool opaque(unsigned int index) const {
        return m[index]->opacity >= 1;
    }

protected:
    template<typename F> primitive_batch(F fm,PyTypeObject *t) {
        PyObject_Init(py::ref(this),t);
        for(size_t i=0; i<v_real::size; ++i) m[i] = fm(i);
    }

    ~primitive_batch() = default;
};


enum solid_type {CUBE=1,SPHERE};

struct solid_obj_common {
    CONTAINED_PYTYPE_DEF
};

template<typename Store> struct solid : solid_obj_common, primitive<Store> {
    solid_type type;

    matrix<Store> orientation;
    matrix<Store> inv_orientation;
    vector<Store> position;

    solid(solid_type type,const matrix<Store> &o,const matrix<Store> &io,const vector<Store> &p,material *m)
        : primitive<Store>(m,pytype()), type(type), orientation(o), inv_orientation(io), position(p) {
        assert(o.dimension() == p.dimension() && o.dimension() == io.dimension());
    }

    solid(solid_type type,const matrix<Store> &o,const vector<Store> &p,material *m) : solid(type,o,o.inverse(),p,m) {}

    solid(size_t dimension,solid_type type,material *m)
        : primitive<Store>(m,pytype()), type(type), orientation(dimension), inv_orientation(dimension), position(dimension) {}

    HOT_FUNC real intersects(
        const ray<Store> &target,
        ray<Store> &normal,
        real cutoff=std::numeric_limits<real>::max(),
        geom_allocator *a=nullptr) const
    {
        ray<Store> transformed{
            inv_orientation * target.origin - position,
            inv_orientation * target.direction,
            a};

        real dist;
        if(type == CUBE) {
            dist = hypercube_intersects(transformed,normal,cutoff);
            if(!dist) return 0;
        } else {
            assert(type == SPHERE);

            dist = hypersphere_intersects(transformed,normal,cutoff);
            if(!dist) return 0;
        }

        normal.origin = orientation * (normal.origin + position);
        normal.direction = orientation * normal.direction;
        return dist;
    }

    size_t dimension() const {
        return orientation.dimension();
    }

    impl::const_matrix_row<Store> cube_normal(size_t axis) const {
        return inv_orientation[axis];
    }

    impl::const_matrix_column<Store> cube_component(size_t axis) const {
        return orientation.column(axis);
    }
};

template<typename T,typename Item> struct flexible_struct {
    static const size_t item_offset;

    // PyTypeObject info:
    static const size_t base_size;
    static const size_t item_size;
    static const size_t alignment;

    static void *operator new(size_t size,size_t item_count);
    static void *operator new(size_t size,void *ptr) { return ptr; }
    static void operator delete(void *ptr);
    static void operator delete(void*,void*) {}
    static void operator delete(void *ptr,size_t) { operator delete(ptr); }

    template<typename U> struct item_array {
        using item_t = std::conditional_t<std::is_const<U>::value,const Item,Item>;
        using byte_t = std::conditional_t<std::is_const<U>::value,const char,char>;

        item_array(U *self) : self(self) {}

        item_t *begin() const;
        item_t *end() const { return begin() + size(); }

        item_t &front() const { return begin()[0]; }
        item_t &back() const { return begin()[size()-1]; }

        // a template so there is no ambiguity with "operator item_t*"
        template<typename V> item_t &operator[](V i) const { return begin()[i]; }

        size_t size() const { return static_cast<const T*>(self)->_item_size(); }

        operator item_t*() const { return begin(); }

    private:
        U *self;
    };

    item_array<flexible_struct<T,Item>> items() { return this; }
    item_array<const flexible_struct<T,Item>> items() const { return this; }

#if defined(__GNUC__) && !defined(NDEBUG)
    // a simpler version of "items" that GDB can evaluate
    const Item *_items() const __attribute__((used));
#endif

    /* a "size" parameter is required because T::_item_size might not work until
       after T's constructor returns */
    template<typename F> flexible_struct(size_t size,F f) {
        size_t i=0;

        try {
            for(; i<size; ++i) new(&items()[i]) Item(f(i));
        } catch(...) {
            while(i) items()[--i].~Item();
            throw;
        }
    }

    ~flexible_struct() {
        for(auto &item : items()) item.~Item();
    }
};

template<typename T,typename Item> const size_t flexible_struct<T,Item>::item_offset = aligned(sizeof(T),alignof(Item));

template<typename T,typename Item> const size_t flexible_struct<T,Item>::base_size = flexible_struct<T,Item>::item_offset;
template<typename T,typename Item> const size_t flexible_struct<T,Item>::item_size = sizeof(Item);
template<typename T,typename Item> const size_t flexible_struct<T,Item>::alignment = std::max(alignof(T),alignof(Item));

template<typename T,typename Item> void *flexible_struct<T,Item>::operator new(size_t size,size_t item_count) {
    assert(size == sizeof(T));
    return global_new(item_offset + sizeof(Item)*item_count,alignment);
}

template<typename T,typename Item> void flexible_struct<T,Item>::operator delete(void *ptr) {
    global_delete(
        ptr,
        item_offset + sizeof(Item)*reinterpret_cast<T*>(ptr)->_item_size(),
        alignment);
}

template<typename T,typename Item> template<typename U>
typename flexible_struct<T,Item>::item_array<U>::item_t *flexible_struct<T,Item>::item_array<U>::begin() const {
    return reinterpret_cast<item_t*>(reinterpret_cast<byte_t*>(self) + item_offset);
}

#if defined(__GNUC__) && !defined(NDEBUG)
template<typename T,typename Item> const Item *flexible_struct<T,Item>::_items() const {
    return reinterpret_cast<const Item*>(reinterpret_cast<const char*>(this) + item_offset);
}
#endif


struct triangle_obj_common {
    CONTAINED_PYTYPE_DEF
};

/* Despite being named triangle, this is actually a simplex with a dimension
   that is always one less than the dimension of the scene. This is only a
   triangle when the scene has a dimension of three. */
template<typename Store> struct triangle : triangle_obj_common, primitive<Store>, flexible_struct<triangle<Store>,vector<Store> > {
    typedef typename triangle::flexible_struct flex_base;

    real d;
    vector<Store> p1;
    vector<Store> face_normal;

    size_t dimension() const {
        return p1.dimension();
    }

    size_t _item_size() const {
        return dimension() - 1;
    }

    FORCE_INLINE real intersects(
        const ray<Store> &target,
        ray<Store> &normal,
        real cutoff=std::numeric_limits<real>::max(),
        geom_allocator *a=nullptr) const
    {
        real denom = dot(face_normal,target.direction);
        if(!denom) return 0;

        real t = -(dot(face_normal,target.origin) + d) / denom;
        if(t <= 0 || t >= cutoff) return 0;

        vector<Store> P{target.origin + t * target.direction,a};
        vector<Store> pside{p1 - P,a};

        real tot_area = 0;
        for(size_t i=0; i<dimension()-1; ++i) {
            real area = dot(this->items()[i],pside);
            if(area < -ROUNDING_FUZZ || area > (1+ROUNDING_FUZZ)) return 0;
            tot_area += area;
        }

        if(tot_area <= (1+ROUNDING_FUZZ)) {
            normal.origin = P;
            normal.direction = face_normal.unit();
            if(denom > 0) normal.direction = -normal.direction;
            return t;
        }
        return 0;
    }

    static triangle<Store> *from_points(const vector<Store> *points,material *mat) {
        const vector<Store> &P1 = points[0];
        size_t n = P1.dimension();
        smaller<matrix<Store>> tmp(n-1);

        typename Store::template smaller_init_array<vector<Store>> vsides(n-1,[&P1,points](size_t i) -> vector<Store> { return points[i+1] - P1; });
        vector<Store> N(n);
        cross_(N,tmp,static_cast<vector<Store>*>(vsides));
        real square = N.square();

        return create(P1,N,[&,square](size_t i) {
            vector<Store> old = vsides[i];
            vsides[i] = N;
            vector<Store> r(N.dimension());
            cross_(r,tmp,static_cast<vector<Store>*>(vsides));
            vsides[i] = old;
            r /= square;
            return r;
        },mat);
    }

    static triangle<Store> *create(size_t dimension,material *m) {
        return new(dimension-1) triangle<Store>(dimension,m);
    }

    template<typename F> static triangle<Store> *create(const vector<Store> &p1,const vector<Store> &face_normal,F edge_normals,material *m) {
        return new(p1.dimension()-1) triangle<Store>(p1,face_normal,edge_normals,m);
    }

    void recalculate_d() {
        d = -dot(face_normal,p1);
    }

private:
    triangle(size_t dimension,material *m)
        : primitive<Store>(m,pytype()), flex_base(dimension-1,[=](size_t i) { return dimension; }), p1(dimension), face_normal(dimension) {}

    template<typename F> triangle(const vector<Store> &p1,const vector<Store> &face_normal,F edge_normals,material *m)
        : primitive<Store>(m,pytype()), flex_base(p1.dimension()-1,edge_normals), p1(p1), face_normal(face_normal) {
        assert(p1.dimension() == face_normal.dimension() &&
            std::all_of(
                ITR_RANGE(this->items()),
                [&](const vector<Store> &e){ return e.dimension() == p1.dimension(); }));
        recalculate_d();
    }
};

template<typename Store> HOT_FUNC real primitive<Store>::intersects(const ray<Store> &target,ray<Store> &normal,real cutoff,geom_allocator *a) const {
    if(Py_TYPE(this) == triangle_obj_common::pytype()) {
        INSTRUMENTATION_TIMER;
        return static_cast<const triangle<Store>*>(this)->intersects(target,normal,cutoff,a);
    }

    assert(Py_TYPE(this) == solid_obj_common::pytype());
    return static_cast<const solid<Store>*>(this)->intersects(target,normal,cutoff,a);
}

template<typename Store> size_t primitive<Store>::dimension() const {
    if(Store::required_d) return Store::required_d;

    if(Py_TYPE(this) == triangle_obj_common::pytype()) return static_cast<const triangle<Store>*>(this)->dimension();

    assert(Py_TYPE(this) == solid_obj_common::pytype());
    return static_cast<const solid<Store>*>(this)->dimension();
}


struct triangle_batch_obj_common {
    CONTAINED_PYTYPE_DEF
};

template<typename Store> struct triangle_batch : triangle_batch_obj_common, primitive_batch<Store>, flexible_struct<triangle_batch<Store>,vector<Store,v_real> > {
    typedef typename triangle_batch::flexible_struct flex_base;

    v_real d;
    vector<Store,v_real> p1;
    vector<Store,v_real> face_normal;

    size_t dimension() const {
        return p1.dimension();
    }

    size_t _item_size() const {
        return dimension() - 1;
    }

    FORCE_INLINE real intersects(
        const ray<Store> &target,
        ray<Store> &normal,
        int &index,
        real cutoff=std::numeric_limits<real>::max(),
        geom_allocator *a=nullptr) const
    {
        INSTRUMENTATION_TIMER;
        auto zeros = v_real::zeros();

        auto denom = dot(face_normal,broadcast<Store,v_real::size>(target.direction));
        auto mask = denom != zeros;

        auto t = -(dot(face_normal,broadcast<Store,v_real::size>(target.origin)) + d) / denom;
        mask = mask && t >= zeros;

        vector<Store,v_real> P{broadcast<Store,v_real::size>(target.origin) + t * broadcast<Store,v_real::size>(target.direction),a};
        vector<Store,v_real> pside{p1 - P,a};

        auto a_min = v_real::repeat(-ROUNDING_FUZZ);
        auto tot_area = zeros;
        for(size_t i=0; i<dimension()-1; ++i) {
            v_real area = dot(this->items()[i],pside);
            mask = mask && area >= a_min;
            tot_area += area;
        }

        auto a_max = v_real::repeat(1+ROUNDING_FUZZ);
        mask = mask && tot_area <= a_max;

        t = simd::zfilter(mask,t);

        real min_t = cutoff;
        int r_index=-1;
        for(int i=0; i<static_cast<int>(v_real::size); ++i) {
            if(i != index && t[i] && t[i] < min_t) {
                min_t = t[i];
                r_index = i;
            }
        }

        if(r_index == -1) return 0;

        index = r_index;
        normal.origin = interleave1<Store,v_real::size>(P,r_index);
        normal.direction = interleave1<Store,v_real::size>(face_normal,r_index).unit();
        if(denom[r_index] > 0) normal.direction = -normal.direction;
        return min_t;
    }

    /* NOTE: multiple invocations of "triangles" with the same argument must
       return the same instance */
    template<typename F> static triangle_batch *from_triangles(F triangles) {
        size_t n = triangles(0)->dimension();

        return create(
            deinterleave<Store,v_real::size>(n,[=](size_t i){ return triangles(i)->p1; }),
            deinterleave<Store,v_real::size>(n,[=](size_t i){ return triangles(i)->face_normal; }),
            [=](size_t i) { return deinterleave<Store,v_real::size>(n,[=](size_t j){ return triangles(j)->items()[i]; }); },
            [=](size_t i) { return triangles(i)->m; });
    }

    template<typename Fe,typename Fm> static triangle_batch *create(const vector<Store,v_real> &p1,const vector<Store,v_real> &face_normal,Fe edge_normals,Fm m) {
        return new(p1.dimension()-1) triangle_batch(p1,face_normal,edge_normals,m);
    }

    void recalculate_d() {
        d = -dot(face_normal,p1);
    }

private:
    template<typename Fe,typename Fm> triangle_batch(const vector<Store,v_real> &p1,const vector<Store,v_real> &face_normal,Fe edge_normals,Fm m)
        : primitive_batch<Store>(m,pytype()), flex_base(p1.dimension()-1,edge_normals), p1(p1), face_normal(face_normal) {
        assert(p1.dimension() == face_normal.dimension() &&
            std::all_of(
                ITR_RANGE(this->items()),
                [&](const vector<Store,v_real> &e){ return e.dimension() == p1.dimension(); }));
        recalculate_d();
    }
};

template<typename Store> HOT_FUNC real primitive_batch<Store>::intersects(
    const ray<Store> &target,
    ray<Store> &normal,
    int &index,
    real cutoff,
    geom_allocator *a) const
{
    assert(Py_TYPE(this) == triangle_batch_obj_common::pytype());
    return static_cast<const triangle_batch<Store>*>(this)->intersects(target,normal,index,cutoff,a);
}

template<typename Store> size_t primitive_batch<Store>::dimension() const {
    if(Store::required_d) return Store::required_d;

    assert(Py_TYPE(this) == triangle_batch_obj_common::pytype());
    return static_cast<const triangle_batch<Store>*>(this)->dimension();
}


/* A dynamic array that stores items in a static buffer while it can */
template<typename T,size_t Prealloc=QUICK_LIST_PREALLOC> class quick_list {
    size_t _size;
    size_t alloc_size;

    alignas(T) char _members[Prealloc * sizeof(T)];
    T *_data;

    void check_capacity() {
        if(_size >= alloc_size) {
            T *new_buff = std::allocator<T>().allocate(alloc_size*2);
            memcpy(new_buff,_data,alloc_size);
            if(alloc_size > Prealloc) {
                std::allocator<T>().deallocate(_data,alloc_size);
            }
            _data = new_buff;
            alloc_size *= 2;
        }
    }

public:
    quick_list() : _size(0), alloc_size(Prealloc), _data(reinterpret_cast<T*>(_members)) {}
    ~quick_list() {
        clear();
        if(alloc_size > Prealloc) {
            std::allocator<T>().deallocate(_data,alloc_size);
        }
    }

    template<typename U=T> void add(U &&item) {
        check_capacity();
        new(&_data[_size]) T(std::forward<U>(item));
        ++_size;
    }

    void clear() {
        for(size_t i=0; i<_size; ++i) _data[i].~T();
        _size = 0;
    }

    size_t size() const { return _size; }

    operator bool() const { return _size != 0; }

    T *data() { return _data; }
    const T *data() const { return _data; }

    T *begin() { return data(); }
    const T *begin() const { return data(); }
    T *end() { return data() + _size; }
    const T *end() const { return data() + _size; }

    void sort_and_unique() {
        std::sort(begin(),end());
        size_t new_size = std::unique(begin(),end()) - _data;
        while(_size > new_size) {
            --_size;
            _data[_size].~T();
        }
    }

    void remove_at(size_t i) {
        assert(i < _size);
        --_size;
        if(i != _size) _data[i] = std::move(_data[_size]);
        _data[_size].~T();
    }
};


template<typename Store,bool=(v_real::size>1)> struct intersection_target {
    primitive<Store> *p;

    bool operator==(intersection_target b) const {
        return p == b.p;
    }

    material *mat() const {
        assert(p);
        return p->m.get();
    }
};
template<typename Store> struct intersection_target<Store,true> {
    PyObject *p;
    int index;

    bool operator==(intersection_target b) const {
        return p == b.p && index == b.index;
    }

    material *mat() const {
        assert(p);

        if(index >= 0) {
            assert(Py_TYPE(p) == triangle_batch<Store>::pytype());
            return reinterpret_cast<primitive_batch<Store>*>(p)->m[index].get();
        }

        assert(Py_TYPE(p) != triangle_batch<Store>::pytype());
        return reinterpret_cast<primitive<Store>*>(p)->m.get();
    }
};

template<typename Store> struct ray_intersection {
    real dist;
    intersection_target<Store> target;
    ray<Store> normal;

    ray_intersection(size_t dimension,geom_allocator *a=nullptr) : normal{dimension,a} {}
    template<typename Ntype> ray_intersection(real dist,intersection_target<Store> target,Ntype &&normal) : dist(dist), target(target), normal(std::forward<Ntype>(normal)) {}

    bool operator<(const ray_intersection &b) const {
        return dist < b.dist;
    }
    bool operator==(const ray_intersection &b) const {
        return target == b.target;
    }
};

template<typename Store> using ray_intersections = quick_list<ray_intersection<Store>>;
using prim_list = quick_list<void*,ALL_HITS_LIST_PREALLOC>;

template<typename Store> void trim_intersections(ray_intersections<Store> &hits,real dist,size_t from=0) {
    while(from < hits.size()) {
        if(hits.data()[from].dist >= dist) hits.remove_at(from);
        else ++from;
    }
}


enum node_type {LEAF=1,BRANCH};

template<typename Store> struct kd_node {
    /* instead of relying on a virtual method table, this member is checked
       manually */
    node_type type;

protected:
    kd_node(node_type type) : type(type) {}
    ~kd_node() = default;
};

template<typename Store> struct kd_node_deleter {
    constexpr kd_node_deleter() = default;
    void operator()(kd_node<Store> *ptr) const;
};

template<typename Store> using kd_node_unique_ptr = std::unique_ptr<kd_node<Store>,kd_node_deleter<Store>>;

template<typename Store> struct kd_branch : kd_node<Store> {
    size_t axis;
    real split;
    kd_node_unique_ptr<Store> left; // < split
    kd_node_unique_ptr<Store> right; // > split

    kd_branch(size_t axis,real split,kd_node<Store> *left=nullptr,kd_node<Store> *right=nullptr) :
        kd_node<Store>(BRANCH), axis(axis), split(split), left(left), right(right) {}

    kd_branch(size_t axis,real split,kd_node_unique_ptr<Store> &&left,kd_node_unique_ptr<Store> &&right) :
        kd_node<Store>(BRANCH), axis(axis), split(split), left(std::move(left)), right(std::move(right)) {}
};

bool has(const prim_list &hits,void *item) {
    return std::find(hits.begin(),hits.end(),item) != hits.end();
}

template<typename Store,bool Batched=(v_real::size>1)> struct kd_leaf : kd_node<Store>, flexible_struct<kd_leaf<Store,Batched>,py::pyptr<primitive<Store>>> {
    typedef typename kd_leaf::flexible_struct flex_base;

    using flex_base::operator delete;
    using flex_base::operator new;

    size_t size;

    size_t _item_size() const {
        return size;
    }

    template<typename F> static kd_leaf *create(size_t size,F f) {
        return new(size) kd_leaf(size,f);
    }

    HOT_FUNC bool intersects(
        const ray<Store> &target,
        intersection_target<Store> skip,
        ray_intersection<Store> &o_hit,
        ray_intersections<Store> &t_hits,
        prim_list &checked,
        geom_allocator *a=nullptr) const
    {
        assert(dimension() == target.dimension() && dimension() == o_hit.normal.dimension());

        size_t h_start = t_hits.size();

        real dist;
        size_t i=0;
        while(i<size) {
            auto item = this->items()[i++].get();
            if(item != skip.p && !has(checked,item)) {
                dist = item->intersects(target,o_hit.normal,o_hit.dist,a);

                if(dist) {
                    if(item->opaque()) {
                        o_hit.dist = dist;
                        o_hit.target.p = item;
                        goto hit;
                    } else {
                        t_hits.add({dist,{item},o_hit.normal});
                    }
                }
                checked.add(item);
            }
        }
        return false;

    hit:
        // is there anything closer?
        ray<Store> new_normal{target.dimension(),a};
        while(i<size) {
            auto item = this->items()[i++].get();
            if(item != skip.p && !has(checked,item)) {
                dist = item->intersects(target,new_normal,o_hit.dist,a);
                if(dist) {
                    if(item->opaque()) {
                        o_hit.dist = dist;
                        o_hit.normal = new_normal;
                        o_hit.target.p = item;
                    } else {
                        t_hits.add({dist,{item},new_normal});
                    }
                }
                checked.add(item);
            }
        }

        trim_intersections(t_hits,dist,h_start);
        return true;
    }

    HOT_FUNC bool occludes(
        const ray<Store> &target,
        real ldistance,
        intersection_target<Store> skip,
        ray_intersections<Store> &hits,
        geom_allocator *a=nullptr) const
    {
        assert(dimension() == target.dimension());

        real dist;
        ray<Store> normal{dimension(),a};
        for(size_t i=0; i<size; ++i) {
            auto item = this->items()[i].get();
            if(item != skip.p) {
                dist = item->intersects(target,normal,ldistance,a);

                if(dist) {
                    if(item->opaque()) return true;

                    hits.add({dist,{item},normal});
                }
            }
        }
        return false;
    }

    size_t dimension() const {
        assert(size);
        return this->items()[0]->dimension();
    }

private:
    template<typename F> kd_leaf(size_t size,F f) : kd_node<Store>(LEAF), flex_base(size,f), size(size) {}
};

template<typename Store> struct kd_leaf<Store,true> : kd_node<Store>, flexible_struct<kd_leaf<Store,true>,py::object> {
    typedef typename kd_leaf::flexible_struct flex_base;

    using flex_base::operator delete;
    using flex_base::operator new;

    size_t size;
    size_t batches;

    size_t _item_size() const {
        return size;
    }

    template<typename F> static kd_leaf *create(size_t size,size_t batches,F f) {
        return new(size) kd_leaf(size,batches,f);
    }
    template<typename F> static kd_leaf *create(size_t size,F f) {
        return new(size) kd_leaf(size,f);
    }

    HOT_FUNC bool intersects(
        const ray<Store> &target,
        intersection_target<Store> skip,
        ray_intersection<Store> &o_hit,
        ray_intersections<Store> &t_hits,
        prim_list &checked,
        geom_allocator *a=nullptr) const
    {
        assert(dimension() == target.dimension() && dimension() == o_hit.normal.dimension());

        size_t h_start = t_hits.size();

        real dist;
        size_t i=0;

        for(; i<size; ++i) {
            auto item = this->items()[i].ref();
            if(i < batches) {
                assert(Py_TYPE(item) == triangle_batch<Store>::pytype());

                if(!has(checked,item)) {
                    int index = skip.p == item ? skip.index : -1;
                    auto p = reinterpret_cast<triangle_batch<Store>*>(item);

                    dist = p->intersects(target,o_hit.normal,index,o_hit.dist,a);

                    if(dist) {
                        if(p->opaque(index)) {
                            o_hit.dist = dist;
                            o_hit.target.p = item;
                            o_hit.target.index = index;
                            goto hit;
                        }

                        t_hits.add({dist,{item,index},o_hit.normal});
                    }
                    checked.add(item);
                }
            } else if(item != skip.p && !has(checked,item)) {
                assert(Py_TYPE(item) != triangle_batch<Store>::pytype());

                auto p = reinterpret_cast<primitive<Store>*>(item);

                dist = p->intersects(target,o_hit.normal,o_hit.dist,a);

                if(dist) {
                    if(p->opaque()) {
                        o_hit.dist = dist;
                        o_hit.target.p = item;
                        o_hit.target.index = -1;
                        goto hit;
                    }

                    t_hits.add({dist,{item,-1},o_hit.normal});
                }
                checked.add(item);
            }
        }
        return false;

    hit:
        // is there anything closer?
        ray<Store> new_normal{target.dimension(),a};

        for(; i<size; ++i) {
            auto item = this->items()[i].ref();
            if(i < batches) {
                assert(Py_TYPE(item) == triangle_batch<Store>::pytype());

                if(!has(checked,item)) {
                    int index = skip.p == item ? skip.index : -1;
                    auto p = reinterpret_cast<triangle_batch<Store>*>(item);

                    dist = p->intersects(target,new_normal,index,o_hit.dist,a);

                    if(dist) {
                        if(p->opaque(index)) {
                            o_hit.dist = dist;
                            o_hit.normal = new_normal;
                            o_hit.target.p = item;
                            o_hit.target.index = index;
                        } else {
                            t_hits.add({dist,{item,index},new_normal});
                        }
                    }
                    checked.add(item);
                }
            } else if(item != skip.p && !has(checked,item)) {
                assert(Py_TYPE(item) != triangle_batch<Store>::pytype());

                auto p = reinterpret_cast<primitive<Store>*>(item);

                dist = p->intersects(target,new_normal,o_hit.dist,a);
                if(dist) {
                    if(p->opaque()) {
                        o_hit.dist = dist;
                        o_hit.normal = new_normal;
                        o_hit.target.p = item;
                        o_hit.target.index = -1;
                    } else {
                        t_hits.add({dist,{item,-1},new_normal});
                    }
                }
                checked.add(item);
            }
        }

        trim_intersections(t_hits,dist,h_start);
        return true;
    }

    HOT_FUNC bool occludes(const ray<Store> &target,real ldistance,intersection_target<Store> skip,ray_intersections<Store> &hits,geom_allocator *a=nullptr) const {
        assert(dimension() == target.dimension());

        real dist;
        ray<Store> normal{dimension(),a};
        for(size_t i=0; i<size; ++i) {
            auto item = this->items()[i];

            if(i < batches) {
                assert(item.type() == triangle_batch<Store>::pytype());

                int index = skip.p == item.ref() ? skip.index : -1;
                auto p = reinterpret_cast<triangle_batch<Store>*>(item.ref());

                dist = p->intersects(target,normal,index,ldistance,a);

                if(dist) {
                    if(p->opaque(index)) return true;

                    hits.add({dist,{item.ref(),index},normal});
                }
            } else if(item.ref() != skip.p) {
                assert(item.type() != triangle_batch<Store>::pytype());

                auto p = reinterpret_cast<primitive<Store>*>(item.ref());

                dist = p->intersects(target,normal,ldistance,a);

                if(dist) {
                    if(p->opaque()) return true;

                    hits.add({dist,{item.ref(),-1},normal});
                }
            }
        }
        return false;
    }

    size_t dimension() const {
        assert(size);
        auto item = this->items()[0].ref();
        return batches ?
            reinterpret_cast<primitive_batch<Store>*>(item)->dimension() :
            reinterpret_cast<primitive<Store>*>(item)->dimension();
    }

private:
    static bool is_batch(py::object x) {
        // this assumes triangle_batch is the only batch primitive
        assert(x.type() == solid<Store>::pytype() || x.type() == triangle<Store>::pytype() || x.type() == triangle_batch<Store>::pytype());

        return x.type() == triangle_batch<Store>::pytype();
    }

    template<typename F> kd_leaf(size_t size,size_t batches,F f) : kd_node<Store>(LEAF), flex_base(size,f), size(size), batches(batches) {
        assert(size > 0 && batches <= size && std::is_partitioned(ITR_RANGE(this->items()),is_batch));
    }

    template<typename F> kd_leaf(size_t size,F f) : kd_node<Store>(LEAF), flex_base(size,f), size(size) {
        assert(size > 0);

        batches = std::partition(ITR_RANGE(this->items()),is_batch) - this->items().begin();
    }
};

template<typename Store> struct kd_node_intersection {
    const ray<Store> &target;
    const vector<Store> invdir;
    intersection_target<Store> skip;
    ray_intersection<Store> &o_hit;
    ray_intersections<Store> &t_hits;
    prim_list checked;
    geom_allocator *a;

    kd_node_intersection(
            const ray<Store> &target,
            intersection_target<Store> skip,
            ray_intersection<Store> &o_hit,
            ray_intersections<Store> &t_hits,
            geom_allocator *a)
        : target{target}, invdir{1/target.direction}, skip{skip}, o_hit{o_hit}, t_hits{t_hits}, a{a} {}

    bool operator()(const kd_node<Store> *node,real t_near,real t_far);
};

template<typename Store> HOT_FUNC bool kd_node_intersection<Store>::operator()(const kd_node<Store> *node,real t_near,real t_far) {
    while(node) {
        if(node->type == LEAF) {
            bool r = static_cast<const kd_leaf<Store>*>(node)->intersects(target,skip,o_hit,t_hits,checked,a);
            assert(!r || o_hit.target.p);
            return r;
        }

        assert(node->type == BRANCH);
        assert(target.dimension() == o_hit.normal.dimension());
        auto bnode = static_cast<const kd_branch<Store>*>(node);

        if(target.direction[bnode->axis]) {
            if(target.origin[bnode->axis] == bnode->split) {
                node = (target.direction[bnode->axis] > 0 ? bnode->right : bnode->left).get();
                continue;
            }

            real t = (bnode->split - target.origin[bnode->axis]) * invdir[bnode->axis];

            auto n_near = target.origin[bnode->axis] > bnode->split ? bnode->right.get() : bnode->left.get();
            auto n_far = target.origin[bnode->axis] > bnode->split ? bnode->left.get() : bnode->right.get();

            if(t < 0 || t > t_far) {
                node = n_near;
                continue;
            }
            if(t < t_near) {
                node = n_far;
                continue;
            }

            if(n_near) {
                size_t h_start = t_hits.size();
                bool hit = operator()(n_near,t_near,t);
                if((hit && o_hit.dist <= t) || !n_far) return hit;

                if(hit) {
                    /* If dist is greater than t, the intersection was in a
                       farther division (a primitive can span multiple
                       divisions) and a closer primitive may exist, but if the
                       intersection is with a primitive that is embedded in the
                       split plane, dist can also be greater than t due to an
                       error from limited precision, so we can't assume the
                       primitive is also in t_far. */

                    if(operator()(n_far,t,t_far)) {
                        /* if there is a closer intersection, some of the
                        transparent hits from before, might be invalid */
                        trim_intersections(t_hits,o_hit.dist,h_start);
                    }
                    return true;
                }
            }

            assert(n_far);
            node = n_far;
            t_near = t;
            continue;
        }

        node = (target.origin[bnode->axis] >= bnode->split ? bnode->right : bnode->left).get();
    }
    return false;
}

template<typename Store> inline bool intersects(
    const kd_node<Store> *node,
    const ray<Store> &target,
    intersection_target<Store> skip,
    ray_intersection<Store> &o_hit,
    ray_intersections<Store> &t_hits,
    real t_near,
    real t_far,
    geom_allocator *a=nullptr)
{
    return kd_node_intersection<Store>{target,skip,o_hit,t_hits,a}(node,t_near,t_far);
}

template<typename Store> HOT_FUNC bool _occludes(const kd_node<Store> *node,const ray<Store> &target,const vector<Store> &invdir,real ldistance,intersection_target<Store> skip,ray_intersections<Store> &hits,real t_near,real t_far,geom_allocator *a) {
    while(node) {
        if(node->type == LEAF) return static_cast<const kd_leaf<Store>*>(node)->occludes(target,ldistance,skip,hits,a);

        assert(node->type == BRANCH);
        auto bnode = static_cast<const kd_branch<Store>*>(node);
        if(target.direction[bnode->axis]) {
            if(target.origin[bnode->axis] == bnode->split) {
                node = (target.direction[bnode->axis] > 0 ? bnode->right : bnode->left).get();
                continue;
            }

            real t = (bnode->split - target.origin[bnode->axis]) * invdir[bnode->axis];

            auto n_near = bnode->left.get();
            auto n_far = bnode->right.get();
            if(target.origin[bnode->axis] > bnode->split) {
                n_near = bnode->right.get();
                n_far = bnode->left.get();
            }

            if(t < 0 || t > t_far) {
                node = n_near;
                continue;
            }
            if(t < t_near) {
                node = n_far;
                continue;
            }

            if(n_near) {
                if(!n_far) {
                    t_far = t;
                    node = n_near;
                    continue;
                }
                if(_occludes(n_near,target,invdir,ldistance,skip,hits,t_near,t,a)) return true;
            }

            assert(n_far);
            if(t < ldistance) return false;
            t_near = t;
            node = n_far;
            continue;
        }

        node = (target.origin[bnode->axis] >= bnode->split ? bnode->right : bnode->left).get();
    }
    return false;
}

template<typename Store> inline bool occludes(const kd_node<Store> *node,const ray<Store> &target,real ldistance,intersection_target<Store> skip,ray_intersections<Store> &hits,real t_near,real t_far,geom_allocator *a=nullptr) {
    return _occludes<Store>(node,target,1/target.direction,ldistance,skip,hits,t_near,t_far,a);
}

template<typename Store> inline void kd_node_deleter<Store>::operator()(kd_node<Store> *ptr) const {
    if(ptr->type == LEAF) {
        delete static_cast<kd_leaf<Store>*>(ptr);
    } else {
        assert(ptr->type == BRANCH);
        delete static_cast<kd_branch<Store>*>(ptr);
    }
}


template<typename Store> struct solid_prototype;
template<typename Store> struct triangle_prototype;
template<typename Store> struct triangle_batch_prototype;

template<typename Store> struct aabb {
    explicit aabb(size_t dimension,geom_allocator *a=nullptr)
        : start{dimension,a}, end{dimension,a} {}
    template<typename T1,typename T2> aabb(T1 &&start,T2 &&end,geom_allocator *a=nullptr)
        : start{std::forward<T1>(start),a}, end{std::forward<T2>(end),a} {}

    aabb(const aabb&) = default;
    aabb(aabb&&) = default;

    size_t dimension() const { return start.dimension(); }

    vector<Store> start;
    vector<Store> end;

    auto center() const {
        return (start + end) * 0.5;
    }

    bool intersects(const triangle_prototype<Store> &tp,geom_allocator *a=nullptr) const;
    bool intersects_flat(const triangle_prototype<Store> &tp,size_t skip,geom_allocator *a=nullptr) const;
    bool intersects(const triangle_batch_prototype<Store> &tp,geom_allocator *a=nullptr) const;
    bool intersects_flat(const triangle_batch_prototype<Store> &tp,size_t skip,geom_allocator *a=nullptr) const;
    bool box_axis_test(const solid<Store> *c,const vector<Store> &axis) const;
    bool intersects(const solid_prototype<Store> &sp,geom_allocator *a=nullptr) const;

    void swap(aabb &b) {
        start.swap(b.start);
        end.swap(b.end);
    }
};

template<typename Store> void swap(aabb<Store> &a,aabb<Store> &b) {
    a.swap(b);
}


template<typename Store> struct primitive_prototype {
    aabb<Store> boundary;
    py::object p;

    size_t dimension() const {
        return boundary.dimension();
    }

    explicit primitive_prototype(size_t dimension) : boundary(dimension) {}
    template<typename T> primitive_prototype(const aabb<Store> &boundary,T &&p) : boundary(boundary), p(p) {}
};

template<typename Store> struct solid_prototype : primitive_prototype<Store> {
    solid<Store> *ps() {
        return reinterpret_cast<solid<Store>*>(this->p.ref());
    }
    const solid<Store> *ps() const {
        return reinterpret_cast<const solid<Store>*>(this->p.ref());
    }
};

template<typename Store,typename T> struct triangle_point {
    vector<Store,T> point;
    const vector<Store,T> &edge_normal;

    triangle_point(const vector<Store,T> &point,const vector<Store,T> &edge_normal) : point(point), edge_normal(edge_normal) {}
    triangle_point(vector<Store,T> &&point,const vector<Store,T> &edge_normal) : point(std::move(point)), edge_normal(edge_normal) {}
};

template<typename Store> struct triangle_prototype : primitive_prototype<Store>, flexible_struct<triangle_prototype<Store>,triangle_point<Store,real> > {
    triangle<Store> *pt() {
        return reinterpret_cast<triangle<Store>*>(this->p.ref());
    }
    const triangle<Store> *pt() const {
        return reinterpret_cast<const triangle<Store>*>(this->p.ref());
    }

    vector<Store> first_edge_normal;

    size_t _item_size() const {
        return this->dimension();
    }
};

template<typename Store> struct triangle_batch_prototype : primitive_prototype<Store>, flexible_struct<triangle_batch_prototype<Store>,triangle_point<Store,v_real>> {
    typedef typename triangle_batch_prototype::flexible_struct flexible_struct;

    triangle_batch<Store> *pt() {
        return reinterpret_cast<triangle_batch<Store>*>(this->p.ref());
    }
    const triangle_batch<Store> *pt() const {
        return reinterpret_cast<const triangle_batch<Store>*>(this->p.ref());
    }

    vector<Store,v_real> first_edge_normal;

    size_t _item_size() const {
        return this->dimension();
    }

    /* NOTE: multiple invocations of t_prototypes with the same argument must
       return the same instance */
    template<typename F> triangle_batch_prototype(size_t dimension,F t_prototypes) :
        primitive_prototype<Store>(t_prototypes(0)->boundary,py::new_ref(triangle_batch<Store>::from_triangles([=](size_t i){ return t_prototypes(i)->pt(); }))),
        flexible_struct(dimension,[=](size_t i) {
            return triangle_point<Store,v_real>(
                deinterleave<Store,v_real::size>(dimension,[=](size_t j){ return t_prototypes(j)->items()[i].point; }),
                i > 0 ? pt()->items()[i-1] : first_edge_normal);
        }),
        first_edge_normal(deinterleave<Store,v_real::size>(dimension,[=](size_t i){ return t_prototypes(i)->first_edge_normal; })) {
        for(size_t i=1; i<v_real::size; ++i) {
            const aabb<Store> &ibound = t_prototypes(i)->boundary;
            v_expr(this->boundary.start) = min(v_expr(this->boundary.start),v_expr(ibound.start));
            v_expr(this->boundary.end) = max(v_expr(this->boundary.end),v_expr(ibound.end));
        }
    }
};


real clamp(real x) {
    if(x > 1) return 1;
    if(x < -1) return -1;
    return x;
}

template<typename A,typename B> typename A::item_t skip_dot(const A &a,const B &b,size_t skip) {
    assert(a.size() == b.size());

    typename A::item_t tot = simd::zeros<typename A::item_t>();
    for(size_t i=0; i<a.size(); ++i) {
        if(i != skip) tot += a.template vec<1>(i).data * b.template vec<1>(i).data;
    }
    return tot;
}


/* All Prototype intersection tests should only return true if the intersection
   between the AABB and the Primitive has a non-zero volume. E.g. two cubes that
   share a face do not count as intersecting. This is important because k-d tree
   split positions are always at Primitive boundaries and those Primitives
   should end up on only one side of the split (hyper)plane. */

template<typename Store> bool aabb<Store>::intersects(const triangle_prototype<Store> &tp,geom_allocator *a) const {
    INSTRUMENTATION_TIMER;

    if((v_expr(tp.boundary.start) >= v_expr(end) || v_expr(tp.boundary.end) <= v_expr(start)).any()) return false;

    real n_offset = dot(tp.pt()->face_normal,tp.items()[0].point);
    vector<Store> origin{center(),a};

    real po = dot(origin,tp.pt()->face_normal);

    real b_max = (v_expr(end - start)/2 * v_expr(tp.pt()->face_normal)).abs().reduce_add();
    real b_min = po - b_max;
    b_max += po;

    if(b_max < n_offset || b_min > n_offset) return false;

    for(size_t i=0; i<dimension(); ++i) {
        const vector<Store> &axis = tp.items()[i].edge_normal;

        for(size_t j=0; j<dimension(); ++j) {
            /*real t_max = skip_dot(tp.items()[0].point,axis,j);
            real t_min = skip_dot(tp.items()[i ? i : 1].point,axis,j);
            if(t_min > t_max) std::swap(t_max,t_min);*/
            real t_min = std::numeric_limits<real>::max();
            real t_max = std::numeric_limits<real>::lowest();
            for(auto &pd : tp.items()) {
                real val = skip_dot(pd.point,axis,j);
                if(val < t_min) t_min = val;
                if(val > t_max) t_max = val;
            }

            po = skip_dot(origin,axis,j);

            real b_radius = 0;
            for(size_t k=0; k<dimension(); ++k) {
                if(k != j) b_radius += std::abs((end[k] - start[k])/2 * axis[k]);
            }
            b_min = po - b_radius;
            b_max = po + b_radius;

            /* if b_radius is 0 then the axis is parallel to the dimension we're
               not considering and the test is invalid */
            if(b_radius != 0 && (b_max <= t_min || b_min >= t_max)) return false;
        }
    }

    return true;
}

template<typename Store> bool aabb<Store>::intersects_flat(const triangle_prototype<Store> &tp,size_t skip,geom_allocator *a) const {
    for(size_t i=0; i<dimension(); ++i) {
        if(i != skip && (tp.boundary.start[i] >= end[i] || tp.boundary.end[i] <= start[i])) return false;
    }

    vector<Store> origin{center(),a};

    for(size_t i=0; i<dimension(); ++i) {
        const vector<Store> &axis = tp.items()[i].edge_normal;

        real t_max = skip_dot(tp.items()[0].point,axis,skip);
        real t_min = skip_dot(tp.items()[i ? i : 1].point,axis,skip);
        if(t_min > t_max) std::swap(t_max,t_min);

        real po = skip_dot(origin,axis,skip);

        real b_max = 0;
        for(size_t k=0; k<dimension(); ++k) {
            if(k != skip) b_max += std::abs((end[k] - start[k])/2 * axis[k]);
        }
        real b_min = po - b_max;
        b_max += po;

        if(b_max <= t_min || b_min >= t_max) return false;
    }

    return true;
}

template<typename Store> bool aabb<Store>::intersects(const triangle_batch_prototype<Store> &tp,geom_allocator *a) const {
    INSTRUMENTATION_TIMER;

    if((v_expr(tp.boundary.start) >= v_expr(end) || v_expr(tp.boundary.end) <= v_expr(start)).any()) return false;

    v_real n_offset = dot(tp.pt()->face_normal,tp.items()[0].point);
    vector<Store> origin{center(),a};

    v_real po = dot(broadcast<Store,v_real::size>(origin),tp.pt()->face_normal);

    v_real b_max = (v_expr(end - start)*0.5 * v_expr(tp.pt()->face_normal)).abs().reduce_add();
    v_real b_min = po - b_max;
    b_max += po;

    v_real::mask miss = b_max < n_offset || b_min > n_offset;

    if(miss.all()) return false;

    for(size_t i=0; i<dimension(); ++i) {
        const vector<Store,v_real> &axis = tp.items()[i].edge_normal;

        for(size_t j=0; j<dimension(); ++j) {
            v_real t_min = v_real::repeat(std::numeric_limits<real>::max());
            v_real t_max = v_real::repeat(std::numeric_limits<real>::lowest());
            for(auto &pd : tp.items()) {
                v_real val = skip_dot(pd.point,axis,j);
                simd::mask_set(t_min,val < t_min,val);
                simd::mask_set(t_max,val > t_max,val);
            }

            po = skip_dot(broadcast<Store,v_real::size>(origin),axis,j);

            v_real b_radius = v_real::zeros();
            for(size_t k=0; k<dimension(); ++k) {
                if(k != j) b_radius += simd::abs(((end[k] - start[k])/2) * axis[k]);
            }
            b_min = po - b_radius;
            b_max = po + b_radius;

            /* if b_radius is 0 then the axis is parallel to the dimension we're
               not considering and the test is invalid */
            miss = miss || (b_radius != v_real::zeros() && (b_max <= t_min || b_min >= t_max));

            if(miss.all()) return false;
        }
    }

    return true;
}

template<typename Store> bool aabb<Store>::intersects_flat(const triangle_batch_prototype<Store> &tp,size_t skip,geom_allocator *a) const {
    for(size_t i=0; i<dimension(); ++i) {
        if(i != skip && (tp.boundary.start[i] >= end[i] || tp.boundary.end[i] <= start[i])) return false;
    }

    vector<Store> origin{center(),a};

    v_real::mask miss = v_real::mask::zeros();

    for(size_t i=0; i<dimension(); ++i) {
        const vector<Store,v_real> &axis = tp.items()[i].edge_normal;

        v_real tmp1 = skip_dot(tp.items()[0].point,axis,skip);
        v_real tmp2 = skip_dot(tp.items()[i ? i : 1].point,axis,skip);

        v_real::mask cmp = tmp1 > tmp2;
        v_real t_max = simd::mask_blend(cmp,tmp1,tmp2);
        v_real t_min = simd::mask_blend(cmp,tmp2,tmp1);

        v_real po = skip_dot(broadcast<Store,v_real::size>(origin),axis,skip);

        v_real b_max = v_real::zeros();
        for(size_t k=0; k<dimension(); ++k) {
            if(k != skip) b_max += simd::abs(((end[k] - start[k])/2) * axis[k]);
        }
        v_real b_min = po - b_max;
        b_max += po;

        miss = miss || b_max <= t_min || b_min >= t_max;

        if(miss.all()) return false;
    }

    return true;
}

template<typename Store> bool aabb<Store>::box_axis_test(const solid<Store> *c,const vector<Store> &axis) const {
    real a_po = dot(c->position,axis);
    real b_po = dot(center(),axis);

    real a_max = 0;
    for(size_t i=0; i<dimension(); ++i) a_max += std::abs(dot(c->cube_component(i),axis));

    real b_max = (v_expr(end - start)/2 * v_expr(axis)).abs().reduce_add();

    return b_po+b_max < a_po-a_max || b_po-b_max > a_po+a_max;
}

template<typename Store> bool aabb<Store>::intersects(const solid_prototype<Store> &sp,geom_allocator *a) const {
    if(sp.ps()->type == CUBE) {
        if((v_expr(end) <= v_expr(sp.boundary.start) || v_expr(start) >= v_expr(sp.boundary.end)).any()) return false;

        for(size_t i=0; i<dimension(); ++i) {
            vector<Store> normal = sp.ps()->cube_normal(i);

            if(box_axis_test(sp.ps(),normal)) return false;

            // try projecting the normal onto each orthogonal hyperplane
            for(size_t j=0; j<dimension(); ++j) {
                vector<Store> axis = normal * -normal[j];
                axis[j] += normal.square();

                if(box_axis_test(sp.ps(),axis)) return false;
            }
        }

        return true;
    }

    assert(sp.ps()->type == SPHERE);

    vector<Store> box_p{sp.ps()->position - sp.ps()->inv_orientation * center(),a};

    vector<Store> closest{dimension(),real(0),a};

    for(size_t i=0; i<dimension(); ++i) {
        // equivalent to: sp.p->orientation.transpose() * vector<Store>::axis(dimension(),i,(end[i] - start[i])/2)
        vector<Store> component{sp.ps()->orientation[i] * ((end[i] - start[i])/2),a};
        closest += clamp(dot(box_p,component)/component.square()) * component;
    }

    return (sp.ps()->position - closest).square() < 1;
}


template<typename Store> struct point_light {
    vector<Store> position;
    color c;

    size_t dimension() const {
        return position.dimension();
    }

    real strength(real distance) const {
        return static_cast<real>(1/std::pow(distance,dimension()-1));
    }
};

template<typename Store> struct global_light {
    vector<Store> direction;
    color c;

    size_t dimension() const {
        return direction.dimension();
    }
};


template<typename Store> void append_specular(color &c,float &a,const material *m,const color &light_c,const vector<Store> &target,const vector<Store> &normal,const vector<Store> &light_dir) {
    // Blinn-Phong model
    float base = std::pow(dot(normal,(light_dir - target).unit()),m->specular_exp) * m->specular_intensity;
    c += m->specular * light_c * base * (1 - a);
    a += base * (1 - a);
    c *= a;
}


template<typename Store> struct composite_scene final : scene {
    static constexpr size_t default_bg_gradient_axis = 1;

    size_t locked;
    bool shadows;
    bool camera_light;
    real fov;
    flat_origin_ray_source<Store> origin_source;
    int max_reflect_depth;
    size_t bg_gradient_axis;
    color ambient, bg1, bg2, bg3;
    camera<Store> cam;
    aabb<Store> boundary;
    kd_node_unique_ptr<Store> root;
    std::vector<point_light<Store>> point_lights;
    std::vector<global_light<Store>> global_lights;

    template<typename T> composite_scene(const aabb<Store> &boundary,T &&data)
        : locked(0),
          shadows(false),
          camera_light(true),
          fov(real(0.8)),
          max_reflect_depth(4),
          bg_gradient_axis(default_bg_gradient_axis),
          ambient(0,0,0),
          bg1(1,1,1),
          bg2(0,0,0),
          bg3(0,1,1),
          cam(boundary.dimension()),
          boundary(boundary),
          root{std::forward<T>(data)} {}

    geom_allocator *new_allocator() const {
        return Store::new_allocator(dimension(),10);
    }

    void set_view_size(int w,int h) {
        origin_source.set_params(w,h,fov);
    }

    HOT_FUNC bool light_reaches(const ray<Store> &target,real ldistance,intersection_target<Store> skip,color &filtered,geom_allocator *a=nullptr) const {
        ray_intersections<Store> transparent_hits;

        if(occludes(root.get(),target,ldistance,skip,transparent_hits,0,std::numeric_limits<real>::max(),a)) return false;

        if(transparent_hits) {
            transparent_hits.sort_and_unique();

            auto data = transparent_hits.data();
            for(auto itr = data + (transparent_hits.size()-1); itr >= data; --itr) {
                assert(itr->target.mat()->opacity != 1);
                filtered *= 1 - itr->target.mat()->opacity;
            }
        }

        return true;
    }

    HOT_FUNC color base_color(const ray<Store> &target,const ray<Store> &normal,intersection_target<Store> source,int depth,geom_allocator *a=nullptr) const {
        auto m = source.mat();

        auto light = color(0,0,0);

        auto specular = color(0,0,0);
        float spec_a = 0;

        for(auto &pl : point_lights) {
            vector<Store> lv{normal.origin - pl.position,a};
            real dist = lv.absolute();
            lv /= dist;

            real sine = dot(normal.direction,lv);
            if(sine > 0) {
                real strength = pl.strength(dist);
                if(shadows) {
                    if(std::max(pl.c.r(),std::max(pl.c.g(),pl.c.b())) * strength * sine > LIGHT_THRESHOLD) {
                        color filtered = pl.c;
                        if(light_reaches(
                            ray<Store>(
                                vector<Store>{normal.origin,shallow_copy},
                                vector<Store>{lv,shallow_copy}),
                            dist,
                            source,
                            filtered,
                            a))
                        {
                            filtered *= strength;
                            light += filtered * sine;
                            if(m->specular_intensity) append_specular(specular,spec_a,m,filtered,target.direction,normal.direction,lv);
                        }
                    }
                } else {
                    light += pl.c * strength * sine;
                }
            }
        }
        for(auto &gl : global_lights) {
            real sine = -dot(normal.direction,gl.direction);
            if(sine > 0) {
                if(shadows) {
                    color filtered = gl.c;
                    if(light_reaches(
                        ray<Store>(
                            vector<Store>{normal.origin,shallow_copy},
                            -gl.direction,
                            a),
                        std::numeric_limits<real>::max(),
                        source,
                        filtered))
                    {
                        light += filtered * sine;
                        if(m->specular_intensity) append_specular<Store>(specular,spec_a,m,filtered,target.direction,normal.direction,-gl.direction);
                    }
                } else {
                    light += gl.c * sine;
                }
            }
        }

        real sine = -dot(target.direction,normal.direction);
        if(camera_light && sine > 0) {
            light += color(sine,sine,sine);
            if(m->specular_intensity) {
                float base = std::pow(sine,m->specular_exp) * m->specular_intensity;
                specular += m->specular * base * (1 - spec_a);
                spec_a += base * (1 - spec_a);
                specular *= spec_a;
            }
        }

        auto r = ambient + m->c * light;

        if(m->reflectivity && depth < max_reflect_depth) {
            r = m->c * ray_color(
                ray<Store>{
                    vector<Store>{normal.origin,shallow_copy},
                    target.direction - normal.direction * (-2 * sine),
                    a},
                depth+1,
                source,
                a) * m->reflectivity + r * (1 - m->reflectivity);
        }

        return specular + r * (1 - spec_a);
    }

    HOT_FUNC color ray_color(const ray<Store> &target,int depth,intersection_target<Store> source,geom_allocator *a) const {
        ray_intersection<Store> hit{target.dimension(),a};
        ray_intersections<Store> transparent_hits;
        color r;

        real dist = aabb_distance(target);
        hit.dist = std::numeric_limits<real>::max();
        if(dist >= 0 && intersects(root.get(),target,source,hit,transparent_hits,dist,std::numeric_limits<real>::max(),a)) {
            r = base_color(target,hit.normal,hit.target,depth,a);
        } else {
            real intensity = target.direction[bg_gradient_axis];
            r = intensity >= 0 ? bg1 * intensity + bg2 * (1 - intensity) : bg3 * -intensity + bg2 * (1 + intensity);
        }

        if(transparent_hits) {
            transparent_hits.sort_and_unique();

            auto data = transparent_hits.data();
            for(auto itr = data + (transparent_hits.size()-1); itr >= data; --itr) {
                assert(itr->target.mat()->opacity != 1);
                auto base = base_color(target,itr->normal,itr->target,depth,a);

                r = base * itr->target.mat()->opacity + r * (1 - itr->target.mat()->opacity);
            }
        }

        return r;
    }

    HOT_FUNC color calculate_color(int x,int y,geom_allocator *a) const {
        return ray_color({
                vector<Store>{cam.origin,shallow_copy},
                origin_source(cam,static_cast<real>(x),static_cast<real>(y),a)},
            0,{},a);
    }

    HOT_FUNC real aabb_distance(const ray<Store> &target) const {
        INSTRUMENTATION_TIMER;

        for(size_t i=0; i<dimension(); ++i) {
            if(target.direction[i]) {
                real o = target.direction[i] > 0 ? boundary.start[i] : boundary.end[i];
                real dist = (o - target.origin[i]) / target.direction[i];
                long skip = static_cast<long>(i);
                if(dist < 0) {
                    dist = 0;
                    skip = -1;
                }

                for(size_t j=0; j<dimension(); ++j) {
                    if(static_cast<long>(j) != skip) {
                        o = target.direction[j] * dist + target.origin[j];
                        if(o >= boundary.end[j] || o <= boundary.start[j]) goto miss;
                    }
                }
                return dist;

                miss: ;
            }
        }

        return -1;
    }

    size_t dimension() const { return cam.dimension(); }

    void lock() { ++locked; }
    void unlock() noexcept {
        assert(locked);
        --locked;
    }
};


/* These values were found through experimentation, although the scenes used
   were rather primitive, so further fine-tuning will likely help. */

real default_cost_traversal(size_t d) {
    switch(d) {
    case 3: return 0;
    case 4: return 1;
    case 5: return 8;
    case 6: return 500;
    default: return 700;
    }
}

real default_cost_intersection(size_t d) {
    switch(d) {
    case 3: return real(0.5);
    default: return real(0.1);
    }
}

struct kd_tree_params {
    int max_depth;
    int split_threshold;
    real traversal;
    real intersection;

    kd_tree_params(size_t dimension) :
        max_depth(KD_DEFAULT_MAX_DEPTH),
        split_threshold(KD_DEFAULT_SPLIT_THRESHOLD),
        traversal(default_cost_traversal(dimension)),
        intersection(default_cost_intersection(dimension)) {}
};

template<typename Store> using proto_array = std::vector<primitive_prototype<Store>*>;

template<typename Store> bool find_split(const aabb<Store> &boundary,size_t axis,const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p,real &pos,const kd_tree_params &params) {
    real best_cost = std::numeric_limits<real>::max();

    vector<Store> cube_range{boundary.end - boundary.start};
    real side_area = 1;
    for(size_t i=0; i<boundary.dimension(); ++i) {
        if(i != axis) side_area *= cube_range[i];
    }

    real shaft_area_factor = 0;
    for(size_t i=0; i<boundary.dimension(); ++i) {
        if(i != axis) {
            real tmp = 1;
            for(size_t j=0; j<boundary.dimension(); ++j) {
                if(j != i && j != axis) tmp *= cube_range[j];
            }
            shaft_area_factor += tmp;
        }
    }

    /* we actually only compute a value that is one half the surface area of
       each box, but since we only need the ratios between areas, it doesn't
       make any difference */
    real area = side_area + shaft_area_factor * cube_range[axis];

    auto split_cost = [=,&boundary](size_t l_count,size_t r_count,real split) -> real {
        real shaft_area = shaft_area_factor * (split - boundary.start[axis]);
        real l_area = side_area + shaft_area;
        real r_area = area - shaft_area;

        return (params.traversal + params.intersection
            * (l_area/area * static_cast<real>(l_count) + r_area/area * static_cast<real>(r_count)));
    };

    proto_array<Store> search_l;
    search_l.reserve(contain_p.size()+overlap_p.size());
    search_l.insert(search_l.end(),ITR_RANGE(contain_p));
    search_l.insert(search_l.end(),ITR_RANGE(overlap_p));
    std::sort(ITR_RANGE(search_l),[=](const primitive_prototype<Store> *a,const primitive_prototype<Store> *b){ return a->boundary.start[axis] < b->boundary.start[axis]; });

    proto_array<Store> search_r{search_l};
    std::sort(ITR_RANGE(search_r),[=](const primitive_prototype<Store> *a,const primitive_prototype<Store> *b){ return a->boundary.end[axis] < b->boundary.end[axis]; });

    size_t il = 1;
    size_t ir = 0;
    real last_split = search_l[0]->boundary.start[axis];
    size_t last_il = 0;
    while(il < search_l.size()) {
        real split = std::min(search_l[il]->boundary.start[axis],search_r[ir]->boundary.end[axis]);

        /* Note: this test is not an optimization. Removing it will produce
           incorrect values for the l_count and r_count parameters. */
        if(split != last_split) {
            if(boundary.end[axis] > last_split && last_split > boundary.start[axis]) {
                real cost = split_cost(last_il,search_l.size()-ir,last_split);
                if(cost < best_cost) {
                    best_cost = cost;
                    pos = last_split;
                }
            }

            last_il = il;
            last_split = split;
        }

        if(search_l[il]->boundary.start[axis] <= search_r[ir]->boundary.end[axis]) ++il;
        else ++ir;
    }

    assert(il == search_l.size());

    while(ir < search_l.size()) {
        real split = search_r[ir]->boundary.end[axis];
        if(split != last_split) {
            if(boundary.end[axis] > last_split && last_split > boundary.start[axis]) {
                real cost = split_cost(search_l.size(),search_l.size()-ir,last_split);
                if(cost < best_cost) {
                    best_cost = cost;
                    pos = last_split;
                }
            }
            last_split = split;
        }
        ++ir;
    }

    real compare = static_cast<real>(search_l.size());
    for(size_t i=0; i<boundary.dimension(); ++i) compare *= boundary.end[i] - boundary.start[i];
    return best_cost < compare;
}

template<typename Store> size_t best_axis(const aabb<Store> &boundary) {
    vector<Store> widths = boundary.end - boundary.start;
    real width = widths[0];
    size_t axis = 0;

    for(size_t i=1; i<boundary.dimension(); ++i) {
        if(widths[i] > width) {
            width = widths[i];
            axis = i;
        }
    }
    return axis;
}

template<typename Store> bool overlap_intersects(const aabb<Store> &bound,const primitive_prototype<Store> *pp,long skip,size_t axis,bool right) {
    if(skip < 0) {
        if(pp->p.type() == triangle_obj_common::pytype()) return bound.intersects(*static_cast<const triangle_prototype<Store>*>(pp));
        if(pp->p.type() == solid_obj_common::pytype()) return bound.intersects(*static_cast<const solid_prototype<Store>*>(pp));

        assert(pp->p.type() == triangle_batch_obj_common::pytype());
        return bound.intersects(*static_cast<const triangle_batch_prototype<Store>*>(pp));
    }

    if(static_cast<size_t>(skip) == axis) {
        return right ? pp->boundary.start[axis] >= bound.start[axis] : pp->boundary.start[axis] < bound.end[axis];
    }

    if(pp->p.type() == triangle_obj_common::pytype()) return bound.intersects_flat(*static_cast<const triangle_prototype<Store>*>(pp),static_cast<size_t>(skip));

    assert(pp->p.type() == triangle_batch_obj_common::pytype());
    return bound.intersects_flat(*static_cast<const triangle_batch_prototype<Store>*>(pp),static_cast<size_t>(skip));
}

template<typename Store> class split_boundary {
    aabb<Store> &boundary;
    const size_t axis;
    const real split;
    const real original_s;
    const real original_e;

public:
    split_boundary(aabb<Store> &boundary,size_t axis,real split)
        : boundary(boundary), axis(axis), split(split), original_s(boundary.start[axis]), original_e(boundary.end[axis]) {}
    ~split_boundary() {
        boundary.start[axis] = original_s;
        boundary.end[axis] = original_e;
    }

    aabb<Store> &left() {
        boundary.start[axis] = original_s;
        boundary.end[axis] = split;
        return boundary;
    }

    aabb<Store> &right() {
        boundary.start[axis] = split;
        boundary.end[axis] = original_e;
        return boundary;
    }
};


template<typename Store> class kd_node_worker_pool;

/* The primitives are divided into the lists: contain_p and overlap_p.
   Primitives in contain_p are entirely inside boundary, and are much easier to
   partition. The rest of the primitives are in overlap_p.

   Primitives should only be part of a side (left or right) if some point within
   the primitive exists where the distance between the plane and the point is
   greater than zero. The exception is if a primitive is completely inside the
   split (hyper)plane, in which case it should be on the right side. */
template<typename Store> kd_node_unique_ptr<Store> create_node(kd_node_worker_pool<Store> &wpool,int depth,aabb<Store> &boundary,const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p,const kd_tree_params &params);


template<typename Store> class kd_node_worker_pool {
    std::vector<std::thread> threads;
    unsigned int max_threads;
    volatile unsigned int busy_threads;
    volatile enum {NORMAL,FINISHING,QUITTING} state;

    std::mutex mut;
    std::condition_variable start;

    typedef std::tuple<kd_node_unique_ptr<Store>*,int,aabb<Store>,proto_array<Store>,proto_array<Store>,const kd_tree_params&> job_values;
    std::deque<job_values> jobs;

    std::exception_ptr exc;

    void worker() {
        std::unique_lock<std::mutex> lock{mut};

        for(;;) {
            --busy_threads;

            while(jobs.empty()) {
                if(state == QUITTING) return;
                /* we wait until all threads are idle in case a thread will add
                   another job */
                else if(state == FINISHING && !busy_threads) {
                    lock.unlock();
                    start.notify_all();
                    return;
                }

                start.wait(lock);
            }

            if(state == QUITTING) return;

            ++busy_threads;

            auto values = std::move(jobs.front());
            jobs.pop_front();

            lock.unlock();
            try {
                *std::get<0>(values) = ::create_node(
                    *this,
                    std::get<1>(values),
                    std::get<2>(values),
                    std::get<3>(values),
                    std::get<4>(values),
                    std::get<5>(values));
            } catch(...) {
                lock.lock();

                if(!exc) exc = std::current_exception();
                state = QUITTING;
                --busy_threads;

                lock.unlock();

                start.notify_all();
                return;
            }
            lock.lock();
        }
    }

public:
    kd_node_worker_pool(int _max_threads=-1)
        : max_threads(_max_threads >= 0 ? _max_threads : (std::thread::hardware_concurrency()-1)), busy_threads(0), state(NORMAL) {
        if(max_threads) threads.reserve(max_threads);
    }

    ~kd_node_worker_pool() {
        if(!threads.empty()) finish(true);
        assert(!busy_threads);
    }

    bool create_node(kd_node_unique_ptr<Store> &dest,int depth,aabb<Store> &boundary,proto_array<Store> &&contain_p,proto_array<Store> &&overlap_p,const kd_tree_params &params) {
        {
            std::lock_guard<std::mutex> lock{mut};

            if(state == QUITTING) return false;

            jobs.emplace_back(&dest,depth,aabb<Store>{boundary.start,boundary.end,nullptr},std::move(contain_p),std::move(overlap_p),params);

            if(threads.size() < max_threads && busy_threads == threads.size()) {
                ++busy_threads;
                threads.push_back(std::thread(&kd_node_worker_pool::worker,this));
            }
        }
        start.notify_one();

        return true;
    }

    void finish(bool quit=false) {
        {
            std::unique_lock<std::mutex> lock{mut};

            if(quit) state = QUITTING;
            else if(state == NORMAL) state = FINISHING;

            start.notify_all();

            while((busy_threads || !jobs.empty()) && state != QUITTING) {
                if(jobs.empty()) {
                    start.wait(lock);
                } else {
                    auto values = std::move(jobs.front());
                    jobs.pop_front();

                    lock.unlock();
                    try {
                        *std::get<0>(values) = ::create_node(
                            *this,
                            std::get<1>(values),
                            std::get<2>(values),
                            std::get<3>(values),
                            std::get<4>(values),
                            std::get<5>(values));
                    } catch(...) {
                        lock.lock();

                        if(!exc) exc = std::current_exception();
                        state = QUITTING;
                        break;
                    }
                    lock.lock();
                }
            }
        }

        for(auto &t : threads) t.join();
        threads.clear();

        if(exc) std::rethrow_exception(exc);
    }

    operator bool() const {
        return max_threads != 0;
    }
};




template<typename Store> kd_node_unique_ptr<Store> create_leaf(const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p) {
    py::acquire_gil gil;

    return kd_node_unique_ptr<Store>(kd_leaf<Store>::create(
        contain_p.size() + overlap_p.size(),
        [&](size_t i){ return py::borrowed_ref((i < contain_p.size() ? contain_p[i] : overlap_p[i-contain_p.size()])->p.ref()); }));
}

template<typename Store> kd_node_unique_ptr<Store> create_node(
    kd_node_worker_pool<Store> &wpool,
    int depth,
    aabb<Store> &boundary,
    const proto_array<Store> &contain_p,
    const proto_array<Store> &overlap_p,
    const kd_tree_params &params)
{
    ++depth;
    size_t axis = best_axis(boundary);

    if(contain_p.empty() && overlap_p.empty()) return nullptr;

    real split;
    if(depth >= params.max_depth
        || contain_p.size() + overlap_p.size() <= size_t(params.split_threshold)
        || !find_split(boundary,axis,contain_p,overlap_p,split,params))
        return create_leaf(contain_p,overlap_p);

    proto_array<Store> l_contain_p, r_contain_p;
    proto_array<Store> l_overlap_p, r_overlap_p;

    for(auto p : contain_p) {
        if(p->boundary.start[axis] < split) {
            if(p->boundary.end[axis] <= split) {
                l_contain_p.push_back(p);
            } else {
                l_overlap_p.push_back(p);
                r_overlap_p.push_back(p);
            }
        } else {
            r_contain_p.push_back(p);
        }
    }

    split_boundary<Store> sb{boundary,axis,split};

    for(auto p : overlap_p) {
        /* If p is flat along any axis, p could be embedded in the hull of
           "boundary" and intersect neither b_left nor b_right. Thus, an
           alternate algorithm is used when p is flat along an axis other than
           "axis", that disregards that axis. */
        long skip = -1;
        if(Py_TYPE(p->p.ref()) == triangle_obj_common::pytype() || Py_TYPE(p->p.ref()) == triangle_batch_obj_common::pytype()) {
            for(size_t i=0; i<boundary.dimension(); ++i) {
                if(p->boundary.start[i] == p->boundary.end[i]) {
                    skip = static_cast<long>(i);
                    break;
                }
            }
        }

        if(overlap_intersects(sb.left(),p,skip,axis,false)) {
            l_overlap_p.push_back(p);
            if(overlap_intersects(sb.right(),p,skip,axis,true)) r_overlap_p.push_back(p);
        } else {
            r_overlap_p.push_back(p);
        }
    }

    auto branch = new kd_branch<Store>(axis,split);
    kd_node_unique_ptr<Store> r{branch};

    if(wpool) {
        if(!wpool.create_node(branch->left,depth,sb.left(),std::move(l_contain_p),std::move(l_overlap_p),params)) return nullptr;
    } else branch->left = create_node<Store>(wpool,depth,sb.left(),l_contain_p,l_overlap_p,params);

    branch->right = create_node<Store>(wpool,depth,sb.right(),r_contain_p,r_overlap_p,params);

    return r;
}


template<typename Store> using primitive_prototype_py_ptr = py::pyptr<wrapped_type<primitive_prototype<Store>>>;

template<typename Store> real grouping_metric(const primitive_prototype<Store> *a,const primitive_prototype<Store> *b) {
    v_array<Store,real> combined = max(v_expr(a->boundary.end),v_expr(b->boundary.end)) - min(v_expr(a->boundary.start),v_expr(b->boundary.start));
    real m = 0;

    for(size_t i=0; i<a->dimension(); ++i) {
        real surface = 1;
        for(size_t j=0; j<a->dimension(); ++j) {
            if(i != j) surface *= combined[j];
        }
        m += surface;
    }

    return m;
}

template<typename Store> struct batch_candidate {
    typename std::vector<primitive_prototype_py_ptr<Store>>::iterator itr;
    real metric;
};

template<typename Store> void add_sorted(std::vector<batch_candidate<Store>> &batch,const batch_candidate<Store> &bc) {
    auto bitr = std::begin(batch);
    ++bitr;
    for(; bitr != std::end(batch); ++bitr) {
        if(bc.metric < bitr->metric) {
            assert(batch.size() <= v_real::size);
            if(batch.size() == v_real::size) batch.pop_back();
            batch.insert(bitr,bc);
            return;
        }
    }
    if(batch.size() < v_real::size) {
        batch.push_back(bc);
    }
}

template<typename Store> void group_primitives(std::vector<primitive_prototype_py_ptr<Store>> &primitives,size_t axis) {
    if constexpr(v_real::size > 1) {
        size_t dimension = primitives[0]->get_base().dimension();

        std::sort(ITR_RANGE(primitives),[=](const primitive_prototype_py_ptr<Store> &a,const primitive_prototype_py_ptr<Store> &b){
            return a->get_base().boundary.center()[axis] < b->get_base().boundary.center()[axis];
        });

        std::vector<batch_candidate<Store>> batch;
        batch.reserve(v_real::size);

        for(auto pitr = std::begin(primitives); pitr != std::end(primitives); ++pitr) {
            if(!*pitr || (*pitr)->get_base().p.type() != triangle_obj_common::pytype()) continue;

            batch.push_back({pitr,0});

            for(auto pnitr = pitr+1; pnitr != std::end(primitives); ++pnitr) {
                if(pnitr == pitr || !*pnitr || (*pnitr)->get_base().p.type() != triangle_obj_common::pytype()) continue;
                add_sorted(batch,{pnitr,grouping_metric(&(*pitr)->get_base(),&(*pnitr)->get_base())});
            }

            if(batch.size() < v_real::size) break;

            batch[0].itr->reset(py::new_ref(new(dimension) wrapped_type<triangle_batch_prototype<Store>>(dimension,[&](size_t i){ return static_cast<triangle_prototype<Store>*>(&(*(batch[i].itr))->get_base()); })));
            for(size_t i=1; i<v_real::size; ++i) {
                *(batch[i].itr) = {};
            }
            batch.clear();
        }

        primitives.resize(std::remove(ITR_RANGE(primitives),primitive_prototype_py_ptr<Store>{}) - primitives.begin());
    }
}

template<typename T> auto get_base_ptr(const T *x) { return &x->get_base(); }

template<typename Store> std::tuple<aabb<Store>,kd_node_unique_ptr<Store>> build_kdtree(std::vector<primitive_prototype_py_ptr<Store>> &p_objs,int max_threads,const kd_tree_params &params) {
    assert(p_objs.size());

    aabb<Store> boundary = p_objs[0]->get_base().boundary;
    for(size_t i=1; i<p_objs.size(); ++i) {
        v_expr(boundary.start) = min(v_expr(boundary.start),v_expr(p_objs[i]->get_base().boundary.start));
        v_expr(boundary.end) = max(v_expr(boundary.end),v_expr(p_objs[i]->get_base().boundary.end));
    }

    group_primitives<Store>(p_objs,best_axis(boundary));
    proto_array<Store> primitives;
    primitives.reserve(p_objs.size());
    for(auto &p : p_objs) primitives.push_back(&p->get_base());

    kd_node_unique_ptr<Store> node;

    {
        py::allow_threads _;
        kd_node_worker_pool<Store> wpool(max_threads);
        node = create_node(wpool,-1,boundary,primitives,{},params);
        wpool.finish();
    }

    return std::tuple<aabb<Store>,kd_node_unique_ptr<Store>>(boundary,std::move(node));
}

#endif
