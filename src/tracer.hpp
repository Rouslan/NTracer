#ifndef tracer_hpp
#define tracer_hpp

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <deque>

#include "geometry.hpp"
#include "light.hpp"
#include "render.hpp"
#include "camera.hpp"
#include "pyobject.hpp"
#include "instrumentation.hpp"


#define ITR_RANGE(X) X.begin(),X.end()


const real ROUNDING_FUZZ = std::numeric_limits<real>::epsilon() * 10;
const size_t QUICK_LIST_PREALLOC = 10;

/* Checking if anything occludes a light is expensive, so if the light from a
   point light is going to be dimmer than this, don't bother. */
const real LIGHT_THRESHOLD = real(1)/512;

const int KD_MAX_DEPTH = 18;

// only split nodes if there are more than this many primitives
const int KD_SPLIT_THRESHHOLD = 2;


template<typename Store> class ray {
public:
    ray(int d) : origin(d), direction(d) {}
    template<typename T1,typename T2> ray(T1 &&o,T2 &&d) : origin(std::forward<T1>(o)), direction(std::forward<T2>(d)) {
        assert(origin.dimension() == direction.dimension());
    }

    int dimension() const { return origin.dimension(); }
    
    vector<Store> origin;
    vector<Store> direction;
};

//Ray operator*(const Matrix &mat,const Ray &ray);

template<typename Store> real hypercube_intersects(const ray<Store> &target,ray<Store> &normal);

template<typename Store> class box_scene : public scene {
public:
    size_t locked;
    real fov;
    
    camera<Store> cam;

    box_scene(int d) : locked(0), fov(0.8), cam(d) {}
    
    color calculate_color(int x,int y,int w,int h) const {
        real fovI = (2 * std::tan(fov/2)) / w;

        ray<Store> view = ray<Store>(
            cam.origin,
            (cam.forward() + cam.right() * (fovI * (x - w/2)) - cam.up() * (fovI * (y - h/2))).unit());
        ray<Store> normal(dimension());
        if(hypercube_intersects<Store>(view,normal)) {
            real sine = dot(view.direction,normal.direction);
            return (sine <= 0 ? -sine : real(0)) * color(1.0f,0.5f,0.5f);
        }
        
        real intensity = dot(view.direction,vector<Store>::axis(dimension(),0));
        return intensity > 0 ? color(intensity,intensity,intensity) :
            color(0.0f,-intensity,-intensity);
    }

    int dimension() const { return cam.dimension(); }
    
    void lock() { ++locked; }
    void unlock() throw() {
        assert(locked);
        --locked;
    }
};


template<typename Store> real hypercube_intersects(const ray<Store> &target,ray<Store> &normal) {
    assert(target.dimension() == normal.dimension());
    INSTRUMENTATION_TIMER;
    
    for(int i=0; i<target.dimension(); ++i) {
        if(target.direction[i]) {
            normal.origin[i] = target.direction[i] < 0 ? 1 : -1;
            real dist = (normal.origin[i] - target.origin[i]) / target.direction[i];
            if(dist > 0) {
                for(int j=0; j<target.dimension(); ++j) {
                    if(i != j) {
                        normal.origin[j] = target.direction[j] * dist + target.origin[j];
                        if(std::abs(normal.origin[j]) > (1+ROUNDING_FUZZ)) goto miss;
                    }
                }
                normal.direction = vector<Store>::axis(target.dimension(),i,normal.origin[i]);
                return dist;

            miss: ;
            }
        }
    }
    
    return 0;
}

template<typename Store> real hypersphere_intersects(const ray<Store> &target,ray<Store> &normal) {
    INSTRUMENTATION_TIMER;
    
    real a = target.direction.square();
    real b = 2 * dot(target.direction,target.origin);
    real c = target.origin.square() - 1;
    
    real discriminant = b*b - 4*a*c;
    if(discriminant < 0) return 0;
    
    real dist = (-b - std::sqrt(discriminant)) / (2 * a);
    if(dist <= 0) return 0;
    
    normal.direction = normal.origin = target.origin + target.direction * dist;
    return dist;
}


typedef simd::v_type<real> v_real;

template<typename Store> struct primitive {
    real intersects(const ray<Store> &target,ray<Store> &normal) const;
    int dimension() const;
    
    PyObject_HEAD
    py::pyptr<material> m;
    
    bool opaque() const {
        return m->opacity >= 1;
    }
    
protected:
    primitive(material *m,PyTypeObject *t) : m(py::borrowed_ref(reinterpret_cast<PyObject*>(m))) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),t);
    }
    
    ~primitive() = default;
};

template<typename Store> struct primitive_batch {
    real intersects(const ray<Store> &target,ray<Store> &normal,unsigned int &index) const;
    int dimension() const;
    
    PyObject_HEAD
    py::pyptr<material> m[v_real::size];
    
    bool opaque(unsigned int index) const {
        return m[index]->opacity >= 1;
    }
    
protected:
    template<typename F> primitive_batch(F m,PyTypeObject *t) {
        PyObject_Init(reinterpret_cast<PyObject*>(this),t);
        for(size_t i=0; i<v_real::size; ++i) m[i].reset(py::borrowed_ref(reinterpret_cast<PyObject*>(m(i))));
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
    py::pyptr<material> m;
    
    solid(solid_type type,const matrix<Store> &o,const matrix<Store> &io,const vector<Store> &p,material *m)
        : primitive<Store>(m,pytype()), type(type), orientation(o), inv_orientation(io), position(p), m(py::borrowed_ref(reinterpret_cast<PyObject*>(m))) {
        assert(o.dimension() == p.dimension() && o.dimension() == io.dimension());
    }
    
    solid(solid_type type,const matrix<Store> &o,const vector<Store> &p,material *m) : solid(type,o,o.inverse(),p,m) {}
    
    solid(int dimension,solid_type type,material *m)
        : primitive<Store>(m,pytype()), type(type), orientation(dimension), inv_orientation(dimension), position(dimension), m(py::borrowed_ref(reinterpret_cast<PyObject*>(m))) {}
    
    real intersects(const ray<Store> &target,ray<Store> &normal) const {
        ray<Store> transformed(inv_orientation * target.origin - position,inv_orientation * target.direction);
        
        real dist;
        if(type == CUBE) {
            dist = hypercube_intersects(transformed,normal);
            if(!dist) return 0;
        } else {
            assert(type == SPHERE);
            
            dist = hypersphere_intersects(transformed,normal);
            if(!dist) return 0;
        }
        
        normal.origin = orientation * (normal.origin + position);
        normal.direction = orientation * normal.direction;
        return dist;
    }
    
    int dimension() const {
        return orientation.dimension();
    }
    
    impl::const_matrix_row<Store> cube_normal(int axis) const {
        return inv_orientation[axis];
    }
    
    impl::const_matrix_column<Store> cube_component(int axis) const {
        return orientation.column(axis);
    }
    
    void *operator new(size_t size) {
        return (alignof(solid<Store>) <= PYOBJECT_ALIGNMENT) ?
            ::operator new(size) :
            simd::aligned_alloc(alignof(solid<Store>),size);
    }
    void operator delete(void *ptr) {
        if(alignof(solid<Store>) <= PYOBJECT_ALIGNMENT) py::free(ptr);
        else simd::aligned_free(ptr);
    }
};

template<typename T,typename Item> struct flexible_struct {
    static const size_t item_offset;
    
    // PyTypeObject info:
    static const size_t base_size;
    static const size_t item_size;
    
    void *operator new(size_t size,size_t item_count) {
        assert(size == sizeof(T));
        return (std::max(alignof(T),alignof(Item)) <= PYOBJECT_ALIGNMENT) ?
            ::operator new(item_offset + sizeof(Item)*item_count) :
            simd::aligned_alloc(std::max(alignof(T),alignof(Item)),item_offset + sizeof(Item)*item_count);
    }
    void operator delete(void *ptr) {
        if(alignof(T) <= PYOBJECT_ALIGNMENT) py::free(ptr);
        else simd::aligned_free(ptr);
    }
    
    template<typename U> struct item_array {
        typedef typename std::conditional<std::is_const<U>::value,const Item,Item>::type item_t;
        
        item_array(U *self) : self(self) {}
        
        item_t *begin() const { return reinterpret_cast<item_t*>(const_cast<char*>(reinterpret_cast<const char*>(self)) + item_offset); }
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
    
    item_array<flexible_struct<T,Item> > items() { return this; }
    item_array<const flexible_struct<T,Item> > items() const { return this; }
    
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
    
    /*template<typename=typename std::enable_if<std::is_default_constructible<Item>::value>::type> flexible_struct(size_t size) {
        size_t i=0;

        try {
            for(; i<size; ++i) new(&items()[i]) Item();
        } catch(...) {
            while(i) items()[--i].~Item();
            throw;
        }
    }*/
    
    ~flexible_struct() {
        for(auto &item : items()) item.~Item();
    }
};

template<typename T,typename Item> const size_t flexible_struct<T,Item>::item_offset = aligned(sizeof(T),alignof(Item));
template<typename T,typename Item> const size_t flexible_struct<T,Item>::base_size = flexible_struct<T,Item>::item_offset;
template<typename T,typename Item> const size_t flexible_struct<T,Item>::item_size = sizeof(Item);


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
    
    int dimension() const {
        return p1.dimension();
    }
    
    size_t _item_size() const {
        return size_t(dimension()-1);
    }
    
    FORCE_INLINE real intersects(const ray<Store> &target,ray<Store> &normal) const {
        real denom = dot(face_normal,target.direction);
        if(!denom) return 0;
        
        real t = -(dot(face_normal,target.origin) + d) / denom;
        if(t < 0) return 0;
        
        vector<Store> P = target.origin + t * target.direction;
        vector<Store> pside = p1 - P;
        
        real tot_area = 0;
        for(int i=0; i<dimension()-1; ++i) {
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
        int n = P1.dimension();
        smaller<matrix<Store> > tmp(n-1);
        
        typename Store::template smaller_init_array<vector<Store> > vsides(n-1,[&P1,points](int i) -> vector<Store> { return points[i+1] - P1; });
        vector<Store> N(n);
        cross_(N,tmp,static_cast<vector<Store>*>(vsides));
        real square = N.square();
        
        return create(P1,N,[&,square](int i) -> vector<Store> {
            vector<Store> old = vsides[i];
            vsides[i] = N;
            vector<Store> r(N.dimension());
            cross_(r,tmp,static_cast<vector<Store>*>(vsides));
            vsides[i] = old;
            r /= square;
            return r;
        },mat);
    }
    
    static triangle<Store> *create(int dimension,material *m) {
        return new(dimension-1) triangle<Store>(dimension,m);
    }
    
    template<typename F> static triangle<Store> *create(const vector<Store> &p1,const vector<Store> &face_normal,F edge_normals,material *m) {
        return new(p1.dimension()-1) triangle<Store>(p1,face_normal,edge_normals,m);
    }
    
    void recalculate_d() {
        d = -dot(face_normal,p1);
    }
    
private:
    triangle(int dimension,material *m)
        : primitive<Store>(m,pytype()), flex_base(dimension-1,[=](int i) { return dimension; }), p1(dimension), face_normal(dimension) {}
    
    template<typename F> triangle(const vector<Store> &p1,const vector<Store> &face_normal,F edge_normals,material *m)
        : primitive<Store>(m,pytype()), flex_base(p1.dimension()-1,edge_normals), p1(p1), face_normal(face_normal) {
        assert(p1.dimension() == face_normal.dimension() &&
            std::all_of(
                ITR_RANGE(this->items()),
                [&](const vector<Store> &e){ return e.dimension() == p1.dimension(); }));
        recalculate_d();
    }
};

struct triangle_batch_obj_common {
    CONTAINED_PYTYPE_DEF
};

template<typename Store> struct triangle_batch : triangle_batch_obj_common, primitive_batch<Store>, flexible_struct<triangle_batch<Store>,vector_batch<Store> > {
    typedef typename triangle_batch::flexible_struct flex_base;
    
    v_real d;
    vector_batch<Store> p1;
    vector_batch<Store> face_normal;
    
    int dimension() const {
        return p1.dimension();
    }
    
    size_t _item_size() const {
        return size_t(dimension()-1);
    }
    
    FORCE_INLINE real intersects(const ray<Store> &target,ray<Store> &normal) const {
        auto zeros = v_real::zeros();
        
        auto denom = dot(face_normal,target.direction);
        auto mask = denom != zeros;
        
        auto t = -(dot(face_normal,target.origin) + d) / denom;
        mask &= t >= zeros;
        
        if(!mask.any()) return 0;
        
        vector_batch<Store> P = target.origin + t * target.direction;
        vector_batch<Store> pside = p1 - P;
        
        auto a_min = v_real::repeat(-ROUNDING_FUZZ);
        auto tot_area = zeros;
        for(int i=0; i<dimension()-1; ++i) {
            v_real area = dot(this->items()[i],pside);
            mask &= area >= a_min;
            tot_area += area;
        }
        
        auto a_max = v_real::repeat(1+ROUNDING_FUZZ);
        mask &= tot_area <= a_max;
        
        if(!mask.any()) return 0;
        
 
        t = t.zfilter(mask);
        
        real min_t = std::numeric_limits<real>::max();
        unsigned int index=0;
        for(unsigned int i=0; i<v_real::size; ++i) {
            if(t[i] < min_t) {
                min_t = t[i];
                index = i;
            }
        }
        
        normal.origin = interleave1(P,index);
        normal.direction = interleave1(face_normal,index).unit();
        if(denom > 0) normal.direction = -normal.direction;
        return min_t;
    }
    
    static triangle_batch *from_triangles(const triangle<Store> **triangles) {
        int n = triangles[0].dimension();
        
        return create(
            deinterleave(n,[=](int i){ return triangles[i]->p1; }),
            deinterleave(n,[=](int i){ return triangles[i]->face_normal; }),
            [=](int i) { deinterleave(n,[=](int j){ return triangles[j]->items()[i]; }); },
            [=](int i) { return triangles[i]->m; });
    }
    
    template<typename Fe,typename Fm> static triangle_batch *create(const vector_batch<Store> &p1,const vector_batch<Store> &face_normal,Fe edge_normals,Fm m) {
        return new(p1.dimension()-1) triangle_batch(p1,face_normal,edge_normals,m);
    }
    
    void recalculate_d() {
        d = -dot(face_normal,p1);
    }
    
private:
    template<typename Fe,typename Fm> triangle_batch(const vector<Store> &p1,const vector<Store> &face_normal,Fe edge_normals,Fm m)
        : primitive_batch<Store>(m,pytype()), flex_base(p1.dimension()-1,edge_normals), p1(p1), face_normal(face_normal) {
        assert(p1.dimension() == face_normal.dimension() &&
            std::all_of(
                ITR_RANGE(this->items()),
                [&](const vector<Store> &e){ return e.dimension() == p1.dimension(); }));
        recalculate_d();
    }
};

template<typename Store> real primitive<Store>::intersects(const ray<Store> &target,ray<Store> &normal) const {
    if(Py_TYPE(this) == triangle_obj_common::pytype()) {
        INSTRUMENTATION_TIMER;
        return static_cast<const triangle<Store>*>(this)->intersects(target,normal);
    }
    
    assert(Py_TYPE(this) == solid_obj_common::pytype());
    return static_cast<const solid<Store>*>(this)->intersects(target,normal);
}

template<typename Store> int primitive<Store>::dimension() const {
    if(Store::required_d) return Store::required_d;
    
    if(Py_TYPE(this) == triangle_obj_common::pytype()) return static_cast<const triangle<Store>*>(this)->dimension();
    
    assert(Py_TYPE(this) == solid_obj_common::pytype());
    return static_cast<const solid<Store>*>(this)->dimension();
}


/* A dynamic array that stores items in a static buffer while it can */
template<typename T> class quick_list {
    size_t _size;
    size_t alloc_size;

    alignas(T) char _members[QUICK_LIST_PREALLOC * sizeof(T)];
    T *members_extra;
    
    T *members() {
        return reinterpret_cast<T*>(_members);
    }
    const T *members() const {
        return reinterpret_cast<const T*>(_members);
    }
    
    void check_capacity() {
        if(_size >= alloc_size) {
            T *new_buff = simd::allocator<T>().allocate(alloc_size*2);
            if(members_extra) {
                memcpy(new_buff,members_extra,alloc_size);
                simd::allocator<T>().destroy(members_extra);
            } else {
                memcpy(new_buff,_members,alloc_size);
            }
            members_extra = new_buff;
            alloc_size *= 2;
        }
    }
    
public:
    quick_list() : _size(0), alloc_size(QUICK_LIST_PREALLOC), members_extra(nullptr) {}
    ~quick_list() {
        if(members_extra) {
            for(size_t i=0; i<_size; ++i) members_extra[i].~T();
            simd::allocator<T>().destroy(members_extra);
        } else {
            for(size_t i=0; i<_size; ++i) members()[i].~T();
        }
    }
    
    template<typename U=T> void add(U &&item) {
        if(_size >= QUICK_LIST_PREALLOC) {
            check_capacity();
            new(&members_extra[_size]) T(std::forward<U>(item));
        } else {
            new(&members()[_size]) T(std::forward<U>(item));
        }
        ++_size;
    }
    
    void clear() {
        if(members_extra) {
            for(size_t i=0; i<_size; ++i) members_extra[i].~T();
        } else {
            for(size_t i=0; i<_size; ++i) members()[i].~T();
        }
        _size = 0;
    }
    
    size_t size() const { return _size; }
    
    operator bool() const { return _size != 0; }
    
    T *data() {
        return members_extra ? members_extra : members();
    }
    const T *data() const {
        return members_extra ? members_extra : members();
    }
    
    T *begin() { return data(); }
    const T *begin() const { return data(); }
    T *end() { return data() + _size; }
    const T *end() const { return data() + _size; }
    
    void sort_and_unique() {
        T *d = data();
        std::sort(d,d+_size);
        std::unique(d,d+_size);
    }
    
    void remove_at(size_t i) {
        assert(i < _size);
        T *d = data();
        --_size;
        if(i != _size) d[i] = std::move(d[_size]);
        d[_size].~T();
    }
};


template<typename Store> struct ray_intersection {
    real dist;
    primitive<Store> *p;
    ray<Store> normal;
    
    template<typename Ntype> ray_intersection(real dist,primitive<Store> *p,Ntype &&normal) : dist(dist), p(p), normal(std::forward<Ntype>(normal)) {}

    ray_intersection(const ray_intersection &b) = default;
    
    bool operator<(const ray_intersection &b) const {
        return dist < b.dist;
    }
    bool operator==(const ray_intersection &b) const {
        return p == b.p;
    }
};
template<typename Store> using ray_intersections = quick_list<ray_intersection<Store> >;

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
    
    real intersects(const ray<Store> &target,ray<Store> &normal,primitive<Store> *&p,ray_intersections<Store> &hits,real t_near,real t_far) const;
    real occludes(const ray<Store> &target,real ldistance,const primitive<Store> *source,ray_intersections<Store> &hits,real t_near,real t_far) const;
    
protected:
    kd_node(node_type type) : type(type) {}
    ~kd_node() = default;
};

template<typename Store> struct kd_node_deleter {
    constexpr kd_node_deleter() = default;
    void operator()(kd_node<Store> *ptr) const;
};

template<typename Store> using kd_node_unique_ptr = std::unique_ptr<kd_node<Store>,kd_node_deleter<Store> >;

template<typename Store> struct kd_branch : kd_node<Store> {
    int axis;
    real split;
    kd_node_unique_ptr<Store> left; // < split
    kd_node_unique_ptr<Store> right; // > split
    
    kd_branch(int axis,real split,kd_node<Store> *left=nullptr,kd_node<Store> *right=nullptr) :
        kd_node<Store>(BRANCH), axis(axis), split(split), left(left), right(right) {}
    
    kd_branch(int axis,real split,kd_node_unique_ptr<Store> &&left,kd_node_unique_ptr<Store> &&right) :
        kd_node<Store>(BRANCH), axis(axis), split(split), left(std::move(left)), right(std::move(right)) {}
    
    real intersects(const ray<Store> &target,ray<Store> &normal,primitive<Store> *&p,ray_intersections<Store> &hits,real t_near,real t_far) const {
        assert(target.dimension() == normal.dimension());
        
        if(target.direction[axis]) {
            if(target.origin[axis] == split) {
                auto node = (target.direction[axis] > 0 ? right : left).get();
                return node ? node->intersects(target,normal,p,hits,t_near,t_far) : 0;
            }
            
            real t = (split - target.origin[axis]) / target.direction[axis];
            
            auto n_near = left.get();
            auto n_far = right.get();
            if(target.origin[axis] > split) {
                n_near = right.get();
                n_far = left.get();
            }

            if(t < 0 || t > t_far) return n_near ? n_near->intersects(target,normal,p,hits,t_near,t_far) : 0;
            if(t < t_near) return n_far ? n_far->intersects(target,normal,p,hits,t_near,t_far) : 0;

            if(n_near) {
                size_t h_start = hits.size();
                primitive<Store> *new_p = p;
                real dist = n_near->intersects(target,normal,p,hits,t_near,t);
                if((dist && dist <= t) || !n_far) return dist;
                
                if(dist) {
                    /* If dist is greater than t, the intersection was in a
                       farther division (a primitive can span multiple
                       divisions) and a closer primitive may exist, but if the
                       intersection is with a primitive that is embedded in the
                       split plane, dist can also be greater than t due to an
                       error from limited precision, so we can't assume the
                       primitive is also in t_far. */
                    
                    ray<Store> new_normal(target.dimension());
                    real new_dist = n_far->intersects(target,new_normal,new_p,hits,t,t_far);
                    if(new_dist && new_dist < dist) {
                        dist = new_dist;
                        normal = new_normal;
                        p = new_p;
                    }
                    trim_intersections(hits,dist,h_start);
                    return dist;
                }
            }
            
            assert(n_far);
            return n_far->intersects(target,normal,p,hits,t,t_far);
        }

        auto node = (target.origin[axis] >= split ? right : left).get();
        return node ? node->intersects(target,normal,p,hits,t_near,t_far) : 0;
    }
    
    bool occludes(const ray<Store> &target,real ldistance,const primitive<Store> *source,ray_intersections<Store> &hits,real t_near,real t_far) const {
        if(target.direction[axis]) {
            if(target.origin[axis] == split) {
                auto node = (target.direction[axis] > 0 ? right : left).get();
                return node ? node->occludes(target,ldistance,source,hits,t_near,t_far) : false;
            }
            
            real t = (split - target.origin[axis]) / target.direction[axis];
            
            auto n_near = left.get();
            auto n_far = right.get();
            if(target.origin[axis] > split) {
                n_near = right.get();
                n_far = left.get();
            }

            if(t < 0 || t > t_far) return n_near ? n_near->occludes(target,ldistance,source,hits,t_near,t_far) : false;
            if(t < t_near) return n_far ? n_far->occludes(target,ldistance,source,hits,t_near,t_far) : false;

            if(n_near) {
                if(n_near->occludes(target,ldistance,source,hits,t_near,t)) return true;
                if(!n_far) return false;
            }
            
            assert(n_far);
            return t < ldistance ? n_far->occludes(target,ldistance,source,hits,t,t_far) : false;
        }

        auto node = (target.origin[axis] >= split ? right : left).get();
        return node ? node->occludes(target,ldistance,source,hits,t_near,t_far) : false;
    }
};

#if 0
template<typename Store> struct nodes_by_dist {
    const kd_node<Store> *near;
    const kd_node<Store> *far;
};

template<typename Store> real kd_branch_intersects(
        const kd_branch<Store> **branches,
        const ray<Store,simd::v_type<real> > &targets,
        ray<Store,simd::v_type<real> > &normals,
        primitive<Store> *&p,
        ray_intersections<Store> *hits,
        simd::v_type<real> t_near,
        simd::v_type<real> t_far) const {
    assert(target.dimension() == normal.dimension());
    
    if(target.direction[axis]) {
        if(target.origin[axis] == split) {
            auto node = (target.direction[axis] > 0 ? right : left).get();
            return node ? node->intersects(target,normal,p,hits,t_near,t_far) : 0;
        }
        
        real t = (split - target.origin[axis]) / target.direction[axis];
        
        typename Store::init_array<nodes_by_dist> nodes{[](int i) -> nodes_by_dist {
            auto n_near = branches[i]->left.get();
            auto n_far = branches[i]->right.get();

            if(targets.origin[branches[i]->axis][i] > branches[i]->split) {
                n_near = branches[i]->right.get();
                n_far = branches[i]->left.get();
            }

            return {n_near,n_far};
        }};

        if(t < 0 || t > t_far) return n_near ? n_near->intersects(target,normal,p,hits,t_near,t_far) : 0;
        if(t < t_near) return n_far ? n_far->intersects(target,normal,p,hits,t_near,t_far) : 0;

        if(n_near) {
            size_t h_start = hits.size();
            primitive<Store> *new_p = p;
            real dist = n_near->intersects(target,normal,p,hits,t_near,t);
            if((dist && dist <= t) || !n_far) return dist;
            
            if(dist) {
                /* If dist is greater than t, the intersection was in a
                   farther division (a primitive can span multiple
                   divisions) and a closer primitive may exist, but if the
                   intersection is with a primitive that is embedded in the
                   split plane, dist can also be greater than t due to an
                   error from limited precision, so we can't assume the
                   primitive is also in t_far. */
                
                ray<Store> new_normal(target.dimension());
                real new_dist = n_far->intersects(target,new_normal,new_p,hits,t,t_far);
                if(new_dist && new_dist < dist) {
                    dist = new_dist;
                    normal = new_normal;
                    p = new_p;
                }
                trim_intersections(hits,dist,h_start);
                return dist;
            }
        }
        
        assert(n_far);
        return n_far->intersects(target,normal,p,hits,t,t_far);
    }

    auto node = (target.origin[axis] >= split ? right : left).get();
    return node ? node->intersects(target,normal,p,hits,t_near,t_far) : 0;
}
#endif

template<typename Store> struct kd_leaf : kd_node<Store>, flexible_struct<kd_leaf<Store>,py::pyptr<primitive<Store> > > {
    typedef typename kd_leaf::flexible_struct flex_base;
    
    using flex_base::operator delete;
    using flex_base::operator new;

    size_t size;
    
    size_t _item_size() const {
        return size;
    }
    
    template<typename F> static kd_leaf<Store> *create(size_t size,F f) {
        return new(size) kd_leaf<Store>(size,f);
    }
    
    real intersects(const ray<Store> &target,ray<Store> &normal,primitive<Store> *&p,ray_intersections<Store> &hits) const {
        assert(dimension() == target.dimension() && dimension() == normal.dimension());
        
        size_t h_start = hits.size();
        
        real dist;
        size_t i=0;
        primitive<Store> *old_p = p;
        while(i<size) {
            auto item = this->items()[i++].get();
            if(item != p) {
                dist = item->intersects(target,normal);
                
                if(dist) {
                    if(item->opaque()) {
                        p = item;
                        goto hit;
                    } else {
                        hits.add({dist,item,normal});
                    }
                }
            }
        }
        return 0;
        
    hit:
        // is there anything closer?
        real new_dist;
        ray<Store> new_normal(target.dimension());
        while(i<size) {
            auto item = this->items()[i++].get();
            if(item != old_p) {
                new_dist = item->intersects(target,new_normal);
                if(new_dist && new_dist < dist) {
                    if(item->opaque()) {
                        dist = new_dist;
                        normal = new_normal;
                        p = item;
                    } else {
                        hits.add({new_dist,item,new_normal});
                    }
                }
            }
        }
        
        trim_intersections(hits,dist,h_start);
        return dist;
    }
    
    bool occludes(const ray<Store> &target,real ldistance,const primitive<Store> *source,ray_intersections<Store> &hits) const {
        assert(dimension() == target.dimension());

        real dist;
        ray<Store> normal(dimension());
        for(size_t i=0; i<size; ++i) {
            auto item = this->items()[i].get();
            if(item != source) {
                dist = item->intersects(target,normal);
                
                if(dist && dist < ldistance) {
                    if(item->opaque()) return true;

                    hits.add({dist,item,normal});
                }
            }
        }
        return false;
    }
    
    int dimension() const {
        assert(size);
        return this->items()[0]->dimension();
    }
    
private:
    template<typename F> kd_leaf(size_t size,F f) : kd_node<Store>(LEAF), flex_base(size,f), size(size) {}
};

template<typename Store> real kd_node<Store>::intersects(const ray<Store> &target,ray<Store> &normal,primitive<Store> *&p,ray_intersections<Store> &hits,real t_near,real t_far) const {
    if(type == LEAF) return static_cast<const kd_leaf<Store>*>(this)->intersects(target,normal,p,hits);
    
    assert(type == BRANCH);
    return static_cast<const kd_branch<Store>*>(this)->intersects(target,normal,p,hits,t_near,t_far);
}

template<typename Store> real kd_node<Store>::occludes(const ray<Store> &target,real ldistance,const primitive<Store> *source,ray_intersections<Store> &hits,real t_near,real t_far) const {
    if(type == LEAF) return static_cast<const kd_leaf<Store>*>(this)->occludes(target,ldistance,source,hits);
    
    assert(type == BRANCH);
    return static_cast<const kd_branch<Store>*>(this)->occludes(target,ldistance,source,hits,t_near,t_far);
}

template<typename Store> inline void kd_node_deleter<Store>::operator()(kd_node<Store> *ptr) const {
    if(ptr->type == LEAF) {
        delete static_cast<const kd_leaf<Store>*>(ptr);
    } else {
        assert(ptr->type == BRANCH);
        delete static_cast<const kd_branch<Store>*>(ptr);
    }
}

/*
struct p_instance_obj_common {
    CONTAINED_PYTYPE_DEF
    PyObject_HEAD
};

template<typename Store> struct p_instance : p_instance_obj_common, primitive<Store> {
    vector<Store> aabb_min, aabb_max;
    std::unique_ptr<kd_node<Store>,kd_node_deleter<Store> > root;
    
    int dimension() const {
        return aabb_min.dimension();
    }
    
    real intersects(const ray<Store> &target,ray<Store> &normal) const {
        for(int i=0; i<dimension(); ++i) {
            if(target.direction[i]) {
                real o = target.direction[i] > 0 ? aabb_min[i] : aabb_max[i];
                real dist = (o - target.origin[i]) / target.direction[i];
                int skip = i;
                if(dist < 0) {
                    dist = 0;
                    skip = -1;
                }

                for(int j=0; j<dimension(); ++j) {
                    if(j != skip) {
                        o = target.direction[j] * dist + target.origin[j];
                        if(o >= aabb_max[j] || o <= aabb_min[j]) goto miss;
                    }
                }
                return root->intersects(target,normal,dist,std::numeric_limits<real>::max());

                miss: ;
            }
        }
        
        return 0;
    }
};*/


template<typename Store> struct solid_prototype;
template<typename Store> struct triangle_prototype;

template<typename Store> struct aabb {
    aabb(int dimension) : start(dimension), end(dimension) {}
    aabb(const vector<Store> &start,const vector<Store> &end) : start(start), end(end) {}

    int dimension() const { return start.dimension(); }

    vector<Store> start;
    vector<Store> end;


    /* All Prototype intersection tests should only return true if the
       intersection between the AABB and the Primitive has a non-zero volume.
       E.g. two cubes that share a face do not count as intersecting. This is
       important because k-d tree split positions are always at Primitive
       boundaries and a Primitive should end up on only one side of the split
       (hyper)plane. */

    bool intersects(const triangle_prototype<Store> &tp) const;
    bool intersects_flat(const triangle_prototype<Store> &tp,int skip) const;
    bool box_axis_test(const solid<Store> *c,const vector<Store> &axis) const;
    bool intersects(const solid_prototype<Store> &sp) const;
    
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
    py::pyptr<primitive<Store> > p;
    
    int dimension() const {
        return boundary.dimension();
    }
};

template<typename Store> struct solid_prototype : primitive_prototype<Store> {
    solid<Store> *ps() {
        return static_cast<solid<Store>*>(this->p.get());
    }
    const solid<Store> *ps() const {
        return static_cast<const solid<Store>*>(this->p.get());
    }
};

template<typename Store> struct triangle_point {
    vector<Store> point;
    const vector<Store> &edge_normal;
    
    triangle_point(const vector<Store> &point,const vector<Store> &edge_normal) : point(point), edge_normal(edge_normal) {}
};

template<typename Store> struct triangle_prototype : primitive_prototype<Store>, flexible_struct<triangle_prototype<Store>,triangle_point<Store> > {
    triangle<Store> *pt() {
        return static_cast<triangle<Store>*>(this->p.get());
    }
    const triangle<Store> *pt() const {
        return static_cast<const triangle<Store>*>(this->p.get());
    }

    vector<Store> first_edge_normal;
    
    size_t _item_size() const {
        return this->dimension();
    }
};


real clamp(real x) {
    if(x > 1) return 1;
    if(x < -1) return -1;
    return x;
}

template<typename Store> real skip_dot(const vector<Store> &a,const vector<Store> &b,int skip) {
    assert(a.dimension() == b.dimension());
    
    real tot = 0;
    for(int i=0; i<a.dimension(); ++i) {
        if(i != skip) tot += a[i] * b[i];
    }
    return tot;
}


/* All Prototype intersection tests should only return true if the intersection
   between the AABB and the Primitive has a non-zero volume. E.g. two cubes that
   share a face do not count as intersecting. This is important because k-d tree
   split positions are always at Primitive boundaries and a Primitive should end
   up on only one side of the split (hyper)plane. */

template<typename Store> bool aabb<Store>::intersects(const triangle_prototype<Store> &tp) const {
    INSTRUMENTATION_TIMER;
    
    if((v_expr(tp.boundary.start) >= v_expr(end) || v_expr(tp.boundary.end) <= v_expr(start)).any()) return false;
    
    real n_offset = dot(tp.pt()->face_normal,tp.items()[0].point);
    vector<Store> origin = (start + end) * 0.5;
    
    real po = dot(origin,tp.pt()->face_normal);
    
    real b_max = (v_expr(end - start)/2 * v_expr(tp.pt()->face_normal)).abs().reduce_add();
    real b_min = po - b_max;
    b_max += po;
    
    if(b_max < n_offset || b_min > n_offset) return false;
    
    for(int i=0; i<dimension(); ++i) {
        const vector<Store> &axis = tp.items()[i].edge_normal;
        
        for(int j=0; j<dimension(); ++j) {
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
            
            b_max = 0;
            for(int k=0; k<dimension(); ++k) {
                if(k != j) b_max += std::abs((end[k] - start[k])/2 * axis[k]);
            }
            b_min = po - b_max;
            b_max += po;
            
            if(b_max <= t_min || b_min >= t_max) return false;
        }
    }
    
    return true;
}

template<typename Store> bool aabb<Store>::intersects_flat(const triangle_prototype<Store> &tp,int skip) const {
    for(int i=0; i<dimension(); ++i) {
        if(i != skip && (tp.boundary.start[i] >= end[i] || tp.boundary.end[i] <= start[i])) return false;
    }
    
    vector<Store> origin = (start + end) * 0.5;

    for(int i=0; i<dimension(); ++i) {
        const vector<Store> &axis = tp.items()[i].edge_normal;
        
        real t_max = skip_dot(tp.items()[0].point,axis,skip);
        real t_min = skip_dot(tp.items()[i ? i : 1].point,axis,skip);
        if(t_min > t_max) std::swap(t_max,t_min);
        
        real po = skip_dot(origin,axis,skip);
        
        real b_max = 0;
        for(int k=0; k<dimension(); ++k) {
            if(k != skip) b_max += std::abs((end[k] - start[k])/2 * axis[k]);
        }
        real b_min = po - b_max;
        b_max += po;
        
        if(b_max <= t_min || b_min >= t_max) return false;
    }
    
    return true;
}

template<typename Store> bool aabb<Store>::box_axis_test(const solid<Store> *c,const vector<Store> &axis) const {
    real a_po = dot(c->position,axis);
    real b_po = dot((start + end) * 0.5,axis);
    
    real a_max = 0;
    for(int i=0; i<dimension(); ++i) a_max += std::abs(dot(c->cube_component(i),axis));
    
    real b_max = (v_expr(end - start)/2 * v_expr(axis)).abs().reduce_add();
    
    return b_po+b_max < a_po-a_max || b_po-b_max > a_po+a_max;
}

template<typename Store> bool aabb<Store>::intersects(const solid_prototype<Store> &sp) const {
    if(sp.ps()->type == CUBE) {
        if((v_expr(end) <= v_expr(sp.boundary.start) || v_expr(start) >= v_expr(sp.boundary.end)).any()) return false;
        
        for(int i=0; i<dimension(); ++i) {
            vector<Store> normal = sp.ps()->cube_normal(i);
            
            if(box_axis_test(sp.ps(),normal)) return false;
            
            // try projecting the normal onto each orthogonal hyperplane
            for(int j=0; j<dimension(); ++j) {
                vector<Store> axis = normal * -normal[j];
                axis[j] += normal.square();
                
                if(box_axis_test(sp.ps(),axis)) return false;
            }
        }
        
        return true;
    }
    
    assert(sp.ps()->type == SPHERE);

    vector<Store> box_p = sp.ps()->position - sp.ps()->inv_orientation * ((start + end) * 0.5);
    
    vector<Store> closest(dimension(),0);
    
    for(int i=0; i<dimension(); ++i) {
        // equivalent to: sp.p->orientation.transpose() * vector<Store>::axis(dimension(),i,(end[i] - start[i])/2)
        vector<Store> component = sp.ps()->orientation[i] * ((end[i] - start[i])/2);
        closest += clamp(dot(box_p,component)/component.square()) * component;
    }
    
    return (sp.ps()->position - closest).square() < 1;
}


template<typename Store> struct point_light {
    vector<Store> position;
    color c;
    
    int dimension() const {
        return position.dimension();
    }
    
    real strength(real distance) const {
        return 1/std::pow(distance,dimension()-1);
    }
};

template<typename Store> struct global_light {
    vector<Store> direction;
    color c;
    
    int dimension() const {
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


template<typename Store> struct composite_scene : scene {
    static const int default_bg_gradient_axis = 1;
    
    size_t locked;
    bool shadows;
    bool camera_light;
    real fov;
    int max_reflect_depth;
    int bg_gradient_axis;
    color ambient, bg1, bg2, bg3;
    camera<Store> cam;
    aabb<Store> boundary;
    std::unique_ptr<kd_node<Store>,kd_node_deleter<Store> > root;
    std::vector<point_light<Store> > point_lights;
    std::vector<global_light<Store> > global_lights;

    composite_scene(const aabb<Store> &boundary,kd_node<Store> *data)
        : locked(0),
          shadows(false),
          camera_light(true),
          fov(0.8),
          max_reflect_depth(4),
          bg_gradient_axis(default_bg_gradient_axis),
          ambient(0,0,0),
          bg1(1,1,1),
          bg2(0,0,0),
          bg3(0,1,1),
          cam(boundary.dimension()),
          boundary(boundary),
          root(data) {}
    
    bool light_reaches(const ray<Store> &target,real ldistance,const primitive<Store> *source,color &filtered) const {
        ray_intersections<Store> transparent_hits;

        if(root->occludes(target,ldistance,source,transparent_hits,0,std::numeric_limits<real>::max())) return false;
        
        if(transparent_hits) {
            transparent_hits.sort_and_unique();
            
            auto data = transparent_hits.data();
            for(auto itr = data + (transparent_hits.size()-1); itr >= data; --itr) {
                assert(itr->p->m->opacity != 1);
                filtered *= 1 - itr->p->m->opacity;
            }
        }
        
        return true;
    }
    
    color base_color(const ray<Store> &target,const ray<Store> &normal,primitive<Store> *p,int depth) const {
        auto m = p->m.get();
        
        auto light = color(0,0,0);
        
        auto specular = color(0,0,0);
        float spec_a = 0;
        
        for(auto &pl : point_lights) {
            vector<Store> lv = normal.origin - pl.position;
            real dist = lv.absolute();
            lv /= dist;
            
            real sine = dot(normal.direction,lv);
            if(sine > 0) {
                real strength = pl.strength(dist);
                if(shadows) {
                    if(std::max(pl.c.r(),std::max(pl.c.g(),pl.c.b())) * strength * sine > LIGHT_THRESHOLD) {
                        color filtered = pl.c;
                        if(light_reaches(ray<Store>(normal.origin,lv),dist,p,filtered)) {
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
                    if(light_reaches(ray<Store>(normal.origin,-gl.direction),std::numeric_limits<real>::max(),p,filtered)) {
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
                ray<Store>(normal.origin,target.direction - normal.direction * (-2 * sine)),
                depth+1,
                p) * m->reflectivity + r * (1 - m->reflectivity);
        }

        return specular + r * (1 - spec_a);
    }
    
    color ray_color(const ray<Store> &target,int depth=0,primitive<Store> *source=nullptr) const {
        ray<Store> normal{target.dimension()};
        ray_intersections<Store> transparent_hits;
        color r;
        
        real dist = aabb_distance(target);
        if(dist >= 0 && (dist = root->intersects(target,normal,source,transparent_hits,dist,std::numeric_limits<real>::max()))) {
            r = base_color(target,normal,source,depth);
        } else {
            real intensity = target.direction[bg_gradient_axis];
            r = intensity >= 0 ? bg1 * intensity + bg2 * (1 - intensity) : bg3 * -intensity + bg2 * (1 + intensity);
        }
        
        if(transparent_hits) {
            transparent_hits.sort_and_unique();
            
            auto data = transparent_hits.data();
            for(auto itr = data + (transparent_hits.size()-1); itr >= data; --itr) {
                assert(itr->p->m->opacity != 1);
                auto base = base_color(target,itr->normal,itr->p,depth);
                
                r = base * itr->p->m->opacity + r * (1 - itr->p->m->opacity);
            }
        }
        
        return r;
    }
    
    color calculate_color(int x,int y,int w,int h) const {
        real fovI = (2 * std::tan(fov/2)) / w;

        return ray_color({
            cam.origin,
            (cam.forward() + cam.right() * (fovI * (x - w/2)) - cam.up() * (fovI * (y - h/2))).unit()});
    }
    
    real aabb_distance(const ray<Store> &target) const {
        INSTRUMENTATION_TIMER;
        
        for(int i=0; i<dimension(); ++i) {
            if(target.direction[i]) {
                real o = target.direction[i] > 0 ? boundary.start[i] : boundary.end[i];
                real dist = (o - target.origin[i]) / target.direction[i];
                int skip = i;
                if(dist < 0) {
                    dist = 0;
                    skip = -1;
                }

                for(int j=0; j<dimension(); ++j) {
                    if(j != skip) {
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

    int dimension() const { return cam.dimension(); }
    
    void lock() { ++locked; }
    void unlock() throw() {
        assert(locked);
        --locked;
    }
};



/* These values were found through experimentation, although the scenes used
   were rather primitive, so further fine-tuning will likely help. */

real cost_traversal(int d) {
    switch(d) {
    case 3: return 0;
    case 4: return 1;
    case 5: return 8;
    case 6: return 500;
    default: return 700;
    }
}

real cost_intersection(int d) {
    switch(d) {
    case 3: return 0.5;
    default: return 0.1;
    }
}

template<typename Store> using proto_array = std::vector<primitive_prototype<Store>*>;

template<typename Store> bool find_split(const aabb<Store> &boundary,int axis,const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p,real &pos) {
    real best_cost = std::numeric_limits<real>::max();
    
    vector<Store> cube_range = boundary.end - boundary.start;
    real side_area = 1;
    for(int i=0; i<boundary.dimension(); ++i) {
        if(i != axis) side_area *= cube_range[i];
    }
    
    real shaft_area_factor = 0;
    for(int i=0; i<boundary.dimension(); ++i) {
        if(i != axis) {
            real tmp = 1;
            for(int j=0; j<boundary.dimension(); ++j) {
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

        return (cost_traversal(boundary.dimension()) + cost_intersection(boundary.dimension())
            * (l_area/area * l_count + r_area/area * r_count));
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

    real compare = search_l.size();
    for(int i=0; i<boundary.dimension(); ++i) compare *= boundary.end[i] - boundary.start[i];
    return best_cost < compare;
}

template<typename Store> int best_axis(const aabb<Store> &boundary) {
    vector<Store> widths = boundary.end - boundary.start;
    real width = widths[0];
    int axis = 0;
    
    for(int i=1; i<boundary.dimension(); ++i) {
        if(widths[i] > width) {
            width = widths[i];
            axis = i;
        }
    }
    return axis;
}

template<typename Store> bool overlap_intersects(const aabb<Store> &bound,const primitive_prototype<Store> *pp,int skip,int axis,bool right) {
    if(skip < 0) {
        if(Py_TYPE(pp->p.ref()) == triangle_obj_common::pytype()) return bound.intersects(*static_cast<const triangle_prototype<Store>*>(pp));
        assert(Py_TYPE(pp->p.ref()) == solid_obj_common::pytype());
        return bound.intersects(*static_cast<const solid_prototype<Store>*>(pp));
    }
    
    if(skip == axis) return right ? pp->boundary.start[axis] >= bound.start[axis] : pp->boundary.start[axis] < bound.end[axis];
    
    assert(Py_TYPE(pp->p.ref()) == triangle_obj_common::pytype());
    return bound.intersects_flat(*static_cast<const triangle_prototype<Store>*>(pp),skip);
}

template<typename Store> class split_boundary {
    aabb<Store> &boundary;
    const int axis;
    const real split;
    const real original_s;
    const real original_e;
    
public:
    split_boundary(aabb<Store> &boundary,int axis,real split)
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
template<typename Store> kd_node_unique_ptr<Store> create_node(kd_node_worker_pool<Store> &wpool,int depth,aabb<Store> &boundary,const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p);


template<typename Store> class kd_node_worker_pool {
    std::vector<std::thread> threads;
    unsigned int max_threads;
    volatile unsigned int busy_threads;
    volatile enum {NORMAL,FINISHING,QUITTING} state;
    
    std::mutex mut;
    std::condition_variable start;
    
    typedef std::tuple<kd_node_unique_ptr<Store>*,int,aabb<Store>,proto_array<Store>,proto_array<Store> > job_values;
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
                *std::get<0>(values) = ::create_node(*this,std::get<1>(values),std::get<2>(values),std::get<3>(values),std::get<4>(values));
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
    
    bool create_node(kd_node_unique_ptr<Store> &dest,int depth,aabb<Store> &boundary,proto_array<Store> &&contain_p,proto_array<Store> &&overlap_p) {
        {
            std::lock_guard<std::mutex> lock{mut};
            
            if(state == QUITTING) return false;
            
            jobs.emplace_back(&dest,depth,boundary,std::move(contain_p),std::move(overlap_p));

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
                        *std::get<0>(values) = ::create_node(*this,std::get<1>(values),std::get<2>(values),std::get<3>(values),std::get<4>(values));
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


template<typename Store> kd_node_unique_ptr<Store> create_node(kd_node_worker_pool<Store> &wpool,int depth,aabb<Store> &boundary,const proto_array<Store> &contain_p,const proto_array<Store> &overlap_p) {
    ++depth;
    int axis = best_axis(boundary);
    
    if(contain_p.empty() && overlap_p.empty()) return nullptr;
    
    real split;
    if(depth >= KD_MAX_DEPTH
        || contain_p.size() + overlap_p.size() <= KD_SPLIT_THRESHHOLD
        || !find_split(boundary,axis,contain_p,overlap_p,split))
        return kd_node_unique_ptr<Store>(kd_leaf<Store>::create(
            contain_p.size() + overlap_p.size(),
            [&](size_t i){ return py::borrowed_ref((i < contain_p.size() ? contain_p[i] : overlap_p[i-contain_p.size()])->p.ref()); }));
    
    proto_array<Store> l_contain_p, r_contain_p, l_overlap_p, r_overlap_p;
    
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
        int skip = -1;
        if(Py_TYPE(p->p.ref()) == triangle_obj_common::pytype()) {
            for(int i=0; i<boundary.dimension(); ++i) {
                if(p->boundary.start[i] == p->boundary.end[i]) {
                    skip = i;
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
        if(!wpool.create_node(branch->left,depth,sb.left(),std::move(l_contain_p),std::move(l_overlap_p))) return nullptr;
    } else branch->left = create_node<Store>(wpool,depth,sb.left(),l_contain_p,l_overlap_p);

    branch->right = create_node<Store>(wpool,depth,sb.right(),r_contain_p,r_overlap_p);

    return r;
}


template<typename Store> std::tuple<aabb<Store>,kd_node<Store>*> build_kdtree(const proto_array<Store> &primitives,int max_threads) {
    assert(primitives.size());
    
    aabb<Store> boundary = primitives[0]->boundary;
    for(size_t i=1; i<primitives.size(); ++i) {
        v_expr(boundary.start) = min(v_expr(boundary.start),v_expr(primitives[i]->boundary.start));
        v_expr(boundary.end) = max(v_expr(boundary.end),v_expr(primitives[i]->boundary.end));
    }
    
    kd_node_worker_pool<Store> wpool(max_threads);
    auto n = create_node(wpool,-1,boundary,primitives,{});
    wpool.finish();

    return std::tuple<aabb<Store>,kd_node<Store>*>(boundary,n.release());
}

#endif
