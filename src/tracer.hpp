#ifndef tracer_hpp
#define tracer_hpp

#include <memory>

#include "geometry.hpp"
#include "light.hpp"
#include "scene.hpp"
#include "camera.hpp"


template<typename Repr> class ray {
public:
    ray(int d) : origin(d), direction(d) {}
    ray(const typename Repr::vector_t &o,const typename Repr::vector_t &d) : origin(o), direction(d) {
        assert(o.dimension() == d.dimension());
    }

    int dimension() const { return origin.dimension(); }
    
    typename Repr::vector_t origin;
    typename Repr::vector_t direction;
};

//Ray operator*(const Matrix &mat,const Ray &ray);

template<typename Repr> bool hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal);
template<typename Repr> color background_color(const typename Repr::vector_t &dir);

template<typename Repr> class BoxScene : public Scene {
public:
    typedef typename Repr::vector_t vector_t;
    
    bool locked;
    REAL fov;
    
    typename Repr::camera_t camera;

    BoxScene(int d) : locked(false), fov(0.8), camera(d) {}
    
    color calculate_color(int x,int y,int w,int h) const {
        REAL fovI = (2 * std::tan(fov/2)) / w;

        ray<Repr> view = ray<Repr>(
            camera.origin(),
            (camera.forward() + camera.right() * (fovI * (x - w/2)) - camera.up() * (fovI * (y - h/2))).unit());
        ray<Repr> normal(dimension());
        if(hypercube_intersects<Repr>(view,normal)) {
            REAL sine = dot(view.direction,normal.direction);
            return (sine <= REAL(0) ? -sine : REAL(0)) * color(1.0f,0.5f,0.5f);
        }
        return background_color<Repr>(view.direction);
    }

    int dimension() const { return camera.dimension(); }
    
    void lock() { locked = true; }
    void unlock() throw() { locked = false; }
};

template<typename Repr> bool hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal) {
    assert(target.dimension() == normal.dimension());
    
    for(int i=0; i<target.dimension(); ++i) {
        if(target.direction[i]) {
            if(target.direction[i]) {
                normal.origin[i] = target.direction[i] < REAL(0) ? REAL(1) : REAL(-1);
                REAL m = (normal.origin[i] - target.origin[i]) / target.direction[i];
                if(m > REAL(0)) {
                    for(int j=0; j<target.dimension(); ++j) {
                        if(i != j) {
                            normal.origin[j] = target.direction[j] * m + target.origin[j];
                            if(std::abs(normal.origin[j]) > REAL(1)) goto miss;
                        }
                    }
                    normal.direction = Repr::vector_t::axis(target.dimension(),i,normal.origin[i]);
                    return true;

                miss: ;
                }
            }
        }
    }
        
    return false;
}

template<typename Repr> bool hypersphere_intersects(const ray<Repr> &target,ray<Repr> &normal) {
    REAL a = target.direction.square();
    REAL b = 2 * dot(target.direction,target.origin);
    REAL c = target.origin.square() - 1;
    
    REAL discriminant = b*b - 4*a*c;
    if(discriminant < REAL(0)) return false;
    
    REAL distance = (-b - std::sqrt(discriminant)) / (2 * a);
    if(distance <= REAL(0)) return false;
    
    normal.direction = normal.origin = target.origin + target.direction * distance;
    return true;
}

enum primitive_type {TRIANGLE=1,CUBE,SPHERE};

template<typename Repr> struct primitive {
    /* instead of relying on a virtual method table, this member is checked
       manually */
    primitive_type type;
    
    void *operator new(size_t size) {
        return py::malloc(size);
    }
    
    void operator delete(void *ptr) {
        return py::free(ptr);
    }
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal) const;
    int dimension() const;
    
    static void destroy(primitive<Repr> *ptr);
    
protected:
    primitive(primitive_type type) : type(type) {}
    ~primitive() = default;
};

template<typename Repr> struct solid : primitive<Repr> {
    typedef typename Repr::py_matrix_t matrix_t;
    typedef typename Repr::py_vector_t vector_t;
    
    matrix_t orientation;
    matrix_t inv_orientation;
    vector_t position;
    
    solid(primitive_type type,matrix_t o,vector_t p) : primitive<Repr>(type), orientation(o), inv_orientation(o.inverse()), position(p) {
        assert(o.dimension() == p.dimension());
    }
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        ray<Repr> transformed(target.origin - position,inv_orientation * target.direction);
        
        switch(this->type) {
        case CUBE:
            if(!hypercube_intersects(transformed,normal)) return false;
            break;
        case SPHERE:
            if(!hypersphere_intersects(transformed,normal)) return false;
            break;
        default:
            assert(false);
        }
        
        normal.origin += position;
        normal.direction = orientation * normal.direction;
        return true;
    }
    
    int dimension() const {
        return orientation.dimension();
    }
};

constexpr size_t aligned(size_t size,size_t alignment) {
    return ((size + alignment - 1) / alignment) * alignment;
}

template<typename T,typename Item> struct flexible_struct {
    static const size_t item_offset = aligned(sizeof(T),alignof(Item));
    
    void *operator new(size_t size,size_t items) {
        assert(size == sizeof(T));
        return py::malloc(item_offset + sizeof(Item)*items);
    }
    void operator delete(void *ptr) {
        py::free(ptr);
    }
    
    Item *flex_array() {
        return reinterpret_cast<Item*>(reinterpret_cast<char*>(this) + item_offset);
    }
    const Item *flex_array() const {
        return reinterpret_cast<const Item*>(reinterpret_cast<const char*>(this) + item_offset);
    }
};

/* Despite being named triangle, this is actually a simplex with a dimension
   that is always one less than the dimension of the scene. This is only a
   triangle when the scene has a dimension of three. */
template<typename Repr> struct triangle : primitive<Repr>, flexible_struct<triangle<Repr>,typename Repr::py_vector_t> {
    typedef typename Repr::py_vector_t vector_t;
    
    using flexible_struct<triangle<Repr>,vector_t>::operator delete;
    using flexible_struct<triangle<Repr>,vector_t>::operator new;
    
    vector_t normal;
    
    vector_t *points() { return this->flex_array(); }
    const vector_t *points() const { return this->flex_array(); }
    
    template<typename F> static triangle<Repr> *create(const vector_t &n,F f) {
        return new(n.dimension()) triangle(n);
    }
    
    triangle(const triangle&) = delete;
    ~triangle() {
        for(int i=0; i<dimension()-1; ++i) points()[i].~vector_t();
    }
    
    int dimension() const { return normal.dimension(); }
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        return false;
    }
    
private:
    template<typename F> triangle(const vector_t normal,F f) : primitive<Repr>(TRIANGLE), normal(normal) {
        int i=0;

        try {
            vector_t tmp = f(i);
            assert(tmp.dimension() == normal.dimension());
            for(; i<normal.dimension()-1; ++i) new(&points()[i]) vector_t(tmp);
        } catch(...) {
            while(i) points()[--i].~vector_t();
            throw;
        }
    }
};

template<typename Repr> bool primitive<Repr>::intersects(const ray<Repr> &target,ray<Repr> &normal) const {
    if(type == TRIANGLE) return static_cast<const triangle<Repr>*>(this)->intersects(target,normal);
    
    assert(type == CUBE || type == SPHERE);
    return static_cast<const solid<Repr>*>(this)->intersects(target,normal);
}

template<typename Repr> int primitive<Repr>::dimension() const {
    if(Repr::required_d) return Repr::required_d;
    
    if(type == TRIANGLE) return static_cast<const triangle<Repr>*>(this)->dimension();
    
    assert(type == CUBE || type == SPHERE);
    return static_cast<const solid<Repr>*>(this)->dimension();
}

template<typename Repr> void primitive<Repr>::destroy(primitive<Repr> *ptr) {
    if(ptr->type == TRIANGLE) {
        delete static_cast<triangle<Repr>*>(ptr);
    } else {
        assert(ptr->type == CUBE || ptr->type == SPHERE);
        delete static_cast<solid<Repr>*>(ptr);
    }
}


enum node_type {LEAF=1,BRANCH};

template<typename Repr> struct kd_node {
    /* instead of relying on a virtual method table, this member is checked
       manually */
    node_type type;
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal,REAL t_near,REAL t_far,int cur_d) const;
    
    void *operator new(size_t size) {
        return py::malloc(size);
    }
    void operator delete(void *ptr) {
        py::free(ptr);
    }
    
protected:
    kd_node(node_type type) : type(type) {}
    ~kd_node() = default;
};

template<typename Repr> struct kd_branch : kd_node<Repr> {
    REAL split;
    std::unique_ptr<kd_node<Repr> > left; // < split
    std::unique_ptr<kd_node<Repr> > right; // > split
    
    kd_branch(REAL split,kd_node<Repr> *left,kd_node<Repr> *right) : kd_node<Repr>(BRANCH), split(split), left(left), right(right) {}
    kd_branch(REAL split,std::unique_ptr<kd_node<Repr> > &&left,std::unique_ptr<kd_node<Repr> > &&right) : kd_node<Repr>(BRANCH), split(split), left(left), right(right) {}
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal,REAL t_near,REAL t_far,int cur_d) const {
        assert(target.dimension() == normal.dimension());
        
        cur_d = (cur_d + 1) % target.dimension();
        
        if(target.direction[cur_d]) {
            REAL t = (split - target.origin[cur_d]) / target.direction[cur_d];
            
            auto n_near = left.get();
            auto n_far = right.get();
            if(target.origin[cur_d] > split) {
                n_near = right.get();
                n_far = left.get();
            }

            if(t < REAL(0) || t >= t_far) return n_near->intersects(target,normal,t_near,t_far,cur_d);
            if(t <= t_near) return n_far->intersects(target,normal,t_near,t_far,cur_d);
        
            if(n_near->intersects(target,normal,t_near,t,cur_d)) return true;
            return n_far->intersects(target,normal,t,t_far,cur_d);
        }

        return (target.origin[cur_d] > split ? right : left)->intersects(target,normal,t_near,t_far,cur_d);
    }
};

template<typename Repr> struct kd_leaf : kd_node<Repr>, flexible_struct<kd_leaf<Repr>,primitive<Repr>*> {
    using flexible_struct<kd_leaf<Repr>,primitive<Repr>*>::operator delete;
    using flexible_struct<kd_leaf<Repr>,primitive<Repr>*>::operator new;

    size_t size;
    
    primitive<Repr> **items() { return this->flex_array(); }
    primitive<Repr> *const *items() const { return this->flex_array(); }
    
    primitive<Repr> **begin() { return this->flex_array(); }
    primitive<Repr> *const *begin() const { return this->flex_array(); }
    
    primitive<Repr> **end() { return this->flex_array() + size; }
    primitive<Repr> *const *end() const { return this->flex_array() + size; }
    
    template<typename F> static kd_leaf<Repr> *create(size_t size,F f) {
        return new(size) kd_leaf<Repr>(size,f);
    }
    
    ~kd_leaf() {
        for(auto ptr : *this) primitive<Repr>::destroy(ptr);
    }
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        assert(dimension() == target.dimension() && dimension() == normal.dimension());
        
        for(auto ptr : *this) {
            if(ptr->intersects(target,normal)) return true;
        }
        return false;
    }
    
    int dimension() const {
        return items()[0]->dimension();
    }
    
private:
    template<typename F> kd_leaf(size_t size,F f) : kd_node<Repr>(LEAF), size(size) {        
        int i=0;
        try {
            for(size_t i=0; i<size; ++i) items()[i] = f(i);
        } catch(...) {
            while(i) primitive<Repr>::destroy(items()[--i]);
            throw;
        }
    }
};

template<typename Repr> bool kd_node<Repr>::intersects(const ray<Repr> &target,ray<Repr> &normal,REAL t_near,REAL t_far,int cur_d) const {
    if(type == LEAF) return static_cast<const kd_leaf<Repr>*>(this)->intersects(target,normal);
    
    assert(type == BRANCH);
    return static_cast<const kd_branch<Repr>*>(this)->intersects(target,normal,t_near,t_far,cur_d);
}


template<typename Repr> struct composite_scene : Scene {
    typedef typename Repr::vector_t vector_t;
    
    bool locked;
    REAL fov;
    typename Repr::camera_t camera;
    std::unique_ptr<kd_node<Repr> > root;

    composite_scene(int d,kd_node<Repr> *data) : locked(false), fov(0.8), camera(d), root(data) {}
    
    color calculate_color(int x,int y,int w,int h) const {
        REAL fovI = (2 * std::tan(fov/2)) / w;

        ray<Repr> view = ray<Repr>(
            camera.origin(),
            (camera.forward() + camera.right() * (fovI * (x - w/2)) - camera.up() * (fovI * (y - h/2))).unit());
        ray<Repr> normal(dimension());
        if(root->intersects(view,normal,-INFINITY,INFINITY,-1)) {
            REAL sine = dot(view.direction,normal.direction);
            return (sine <= REAL(0) ? -sine : REAL(0)) * color(1.0f,0.5f,0.5f);
        }
        return background_color<Repr>(view.direction);
    }

    int dimension() const { return camera.dimension(); }
    
    void lock() { locked = true; }
    void unlock() throw() { locked = false; }
};

template<typename Repr> color background_color(const typename Repr::vector_t &dir) {
    REAL intensity = dot(dir,Repr::vector_t::axis(dir.dimension(),0));
    return intensity > REAL(0) ? color(intensity,intensity,intensity) :
        color(0.0f,-intensity,-intensity);
}

#endif
