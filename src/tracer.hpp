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

template<typename Repr> real hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal);
template<typename Repr> color background_color(const typename Repr::vector_t &dir);

template<typename Repr> class BoxScene : public Scene {
public:
    typedef typename Repr::vector_t vector_t;
    
    bool locked;
    real fov;
    
    typename Repr::camera_t camera;

    BoxScene(int d) : locked(false), fov(0.8), camera(d) {}
    
    color calculate_color(int x,int y,int w,int h) const {
        real fovI = (2 * std::tan(fov/2)) / w;

        ray<Repr> view = ray<Repr>(
            camera.origin(),
            (camera.forward() + camera.right() * (fovI * (x - w/2)) - camera.up() * (fovI * (y - h/2))).unit());
        ray<Repr> normal(dimension());
        if(hypercube_intersects<Repr>(view,normal)) {
            real sine = dot(view.direction,normal.direction);
            return (sine <= 0 ? -sine : real(0)) * color(1.0f,0.5f,0.5f);
        }
        return background_color<Repr>(view.direction);
    }

    int dimension() const { return camera.dimension(); }
    
    void lock() { locked = true; }
    void unlock() throw() { locked = false; }
};

template<typename Repr> real hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal) {
    assert(target.dimension() == normal.dimension());
    
    for(int i=0; i<target.dimension(); ++i) {
        if(target.direction[i]) {
            if(target.direction[i]) {
                normal.origin[i] = target.direction[i] < 0 ? 1 : -1;
                real dist = (normal.origin[i] - target.origin[i]) / target.direction[i];
                if(dist > 0) {
                    for(int j=0; j<target.dimension(); ++j) {
                        if(i != j) {
                            normal.origin[j] = target.direction[j] * dist + target.origin[j];
                            if(std::abs(normal.origin[j]) > 1) goto miss;
                        }
                    }
                    normal.direction = Repr::vector_t::axis(target.dimension(),i,normal.origin[i]);
                    return dist;

                miss: ;
                }
            }
        }
    }
        
    return 0;
}

template<typename Repr> real hypersphere_intersects(const ray<Repr> &target,ray<Repr> &normal) {
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

template<typename Repr> typename Repr::py_vector_t cross(int d,const typename Repr::py_vector_t *vs) {
    typename Repr::py_vector_t r(d);
    smaller<typename Repr::py_matrix_t> tmp(d-1);
    impl::cross(r,tmp,vs);
    return r;
}

/*template<typename Repr> typename Repr::vector_t cross(int d,const typename Repr::vector_t *vs) {
    typename Repr::vector_t r(d);
    smaller<typename Repr::matrix_t> tmp(d-1);
    impl::cross(r,tmp,vs);
    return r;
}*/

template<typename Repr> struct primitive {
    real intersects(const ray<Repr> &target,ray<Repr> &normal) const;
    int dimension() const;
    
protected:
    primitive() = default;
    ~primitive() = default;
};


enum solid_type {CUBE=1,SPHERE};

struct solid_common {
    static PyTypeObject pytype;
    
    PY_MEM_NEW_DELETE
    PyObject_HEAD
};

template<typename Repr> struct solid : solid_common, primitive<Repr> {
    typedef typename Repr::py_matrix_t matrix_t;
    typedef typename Repr::py_vector_t vector_t;
    
    solid_type type;
    
    matrix_t orientation;
    matrix_t inv_orientation;
    vector_t position;
    
    solid(solid_type type,const matrix_t &o,const matrix_t &io,const vector_t &p) : type(type), orientation(o), inv_orientation(io), position(p) {
        assert(o.dimension() == p.dimension() && o.dimension() == io.dimension());
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
    
    solid(solid_type type,matrix_t o,vector_t p) : solid(type,o,o.inverse(),p) {}
    
    real intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        ray<Repr> transformed(target.origin - position,inv_orientation * target.direction);
        
        real dist;
        if(type == CUBE) {
            dist = hypercube_intersects(transformed,normal);
            if(!dist) return 0;
        } else {
            assert(type == SPHERE);
            
            dist = hypersphere_intersects(transformed,normal);
            if(!dist) return 0;
        }
        
        normal.origin += position;
        normal.direction = orientation * normal.direction;
        return dist;
    }
    
    int dimension() const {
        return orientation.dimension();
    }
};

template<typename Repr> inline auto cube_normal(const solid<Repr> *c,int axis) -> decltype(c->inv_orientation[axis]) {
    return c->inv_orientation[axis];
}

template<typename Repr> inline auto cube_component(const solid<Repr> *c,int axis) -> decltype(c->orientation.column(axis)) {
    return c->orientation.column(axis);
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

struct triangle_common {
    static PyTypeObject pytype;
};

/* Despite being named triangle, this is actually a simplex with a dimension
   that is always one less than the dimension of the scene. This is only a
   triangle when the scene has a dimension of three. */
#define TRIANGLE_BASE Repr::template flexible_obj<triangle<Repr>,typename Repr::py_vector_t,Repr::required_d-1>
template<typename Repr> struct triangle : triangle_common, primitive<Repr>, TRIANGLE_BASE {
    typedef typename Repr::vector_t vector_t;
    typedef typename Repr::py_vector_t py_vector_t;
    
    real d;
    py_vector_t p1;
    py_vector_t face_normal;
    
    int dimension() const {
        return p1.dimension();
    }
    
    real intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        real denom = dot(face_normal,target.direction);
        if(!denom) return 0;
        
        real t = -(dot(face_normal,target.origin) + d) / denom;
        if(t <= 0) return 0;
        
        vector_t P = target.origin + t * target.direction;
        vector_t pside = p1 - P;
        
        real tot_area = 0;
        for(auto &edge : this->items()) {
            real area = dot(edge,pside);
            if(area < 0 || area > 1) return 0;
            tot_area += area;
        }
        
        if(tot_area >= 0 && tot_area <= 1) {
            normal.origin = P;
            normal.direction = face_normal.unit();
            if(denom > 0) normal.direction = -normal.direction;
            return t;
        }
        return 0;
    }
    
    static triangle<Repr> *from_points(const py_vector_t *points) {
        const py_vector_t &P1 = points[0];
        int n = P1.dimension();
        smaller<typename Repr::matrix_t> tmp(n-1);
        
        typename Repr::template smaller_init_array<vector_t> vsides(n-1,[&P1,points](int i) -> vector_t { return points[i+1] - P1; });
        py_vector_t N(n);
        impl::cross(N,tmp,static_cast<vector_t*>(vsides));
        real square = N.square();
        
        return create(P1,N,[&,square](int i) -> py_vector_t {
            vector_t old = vsides[i];
            vsides[i] = N;
            py_vector_t r(N.dimension());
            impl::cross(r,tmp,static_cast<vector_t*>(vsides));
            vsides[i] = old;
            r /= square;
            return r;
        });
    }
    
    template<typename F> static triangle<Repr> *create(const py_vector_t &p1,const py_vector_t &face_normal,F edge_normals) {
        return new(p1.dimension()-1) triangle<Repr>(p1,face_normal,edge_normals);
    }
    
private:
    template<typename F> triangle(const py_vector_t &p1,const py_vector_t &face_normal,F edge_normals) : TRIANGLE_BASE(edge_normals), d(-dot(face_normal,p1)), p1(p1), face_normal(face_normal) {
        assert(p1.dimension() == face_normal.dimension() &&
            std::all_of(items().begin(),items().end(),[&](const py_vector_t &e){ return e.dimension() == p1.dimension() }));
        PyObject_Init(reinterpret_cast<PyObject*>(this),&pytype);
    }
};

template<typename Repr> real primitive<Repr>::intersects(const ray<Repr> &target,ray<Repr> &normal) const {
    if(Py_TYPE(this) == &triangle_common::pytype) return static_cast<const triangle<Repr>*>(this)->intersects(target,normal);
    
    assert(Py_TYPE(this) == &solid_common::pytype);
    return static_cast<const solid<Repr>*>(this)->intersects(target,normal);
}

template<typename Repr> int primitive<Repr>::dimension() const {
    if(Repr::required_d) return Repr::required_d;
    
    if(Py_TYPE(this) == &triangle_common::pytype) return static_cast<const triangle<Repr>*>(this)->dimension();
    
    assert(Py_TYPE(this) == &solid_common::pytype);
    return static_cast<const solid<Repr>*>(this)->dimension();
}


enum node_type {LEAF=1,BRANCH};

template<typename Repr> struct kd_node {
    /* instead of relying on a virtual method table, this member is checked
       manually */
    node_type type;
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal,real t_near,real t_far) const;
    
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
    int axis;
    real split;
    std::unique_ptr<kd_node<Repr> > left; // < split
    std::unique_ptr<kd_node<Repr> > right; // > split
    
    kd_branch(int axis,real split,kd_node<Repr> *left,kd_node<Repr> *right) :
        kd_node<Repr>(BRANCH), axis(axis), split(split), left(left), right(right) {}
    
    kd_branch(int axis,real split,std::unique_ptr<kd_node<Repr> > &&left,std::unique_ptr<kd_node<Repr> > &&right) :
        kd_node<Repr>(BRANCH), axis(axis), split(split), left(left), right(right) {}
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal,real t_near,real t_far) const {
        assert(target.dimension() == normal.dimension());
        
        if(target.direction[axis]) {
            if(target.origin[axis] == split) {
                auto node = (target.direction[axis] > 0 ? right : left).get();
                return node ? node->intersects(target,normal,t_near,t_far) : false;
            }
            
            real t = (split - target.origin[axis]) / target.direction[axis];
            
            auto n_near = left.get();
            auto n_far = right.get();
            if(target.origin[axis] > split) {
                n_near = right.get();
                n_far = left.get();
            }

            if(t <= 0 || t >= t_far) return n_near ? n_near->intersects(target,normal,t_near,t_far) : false;
            if(t <= t_near) return n_far ? n_far->intersects(target,normal,t_near,t_far) : false;
        
            if(n_near && n_near->intersects(target,normal,t_near,t)) return true;
            return n_far ? n_far->intersects(target,normal,t,t_far) : false;
        }

        auto node = (target.origin[axis] >= split ? right : left).get();
        return node ? node->intersects(target,normal,t_near,t_far) : false;
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
        for(auto ptr : *this) Py_DECREF(ptr);
    }
    
    bool intersects(const ray<Repr> &target,ray<Repr> &normal) const {
        assert(dimension() == target.dimension() && dimension() == normal.dimension());
        
        real dist;
        size_t i=0;
        for(; i<size; ++i) {
            dist = items()[i]->intersects(target,normal);
            if(dist) goto hit;
        }
        return false;
        
    hit:
        // is there anything closer?
        real new_dist;
        ray<Repr> new_normal(target.dimension());
        for(; i<size; ++i) {
            new_dist = items()[i]->intersects(target,new_normal);
            if(new_dist && new_dist < dist) {
                dist = new_dist;
                normal = new_normal;
            }
        }
        return true;
    }
    
    int dimension() const {
        assert(size);
        return items()[0]->dimension();
    }
    
private:
    template<typename F> kd_leaf(size_t size,F f) : kd_node<Repr>(LEAF), size(size) {        
        size_t i=0;
        try {
            for(i=0; i<size; ++i) items()[i] = f(i);
        } catch(...) {
            while(i) py::decref(reinterpret_cast<PyObject*>(items()[--i]));
            throw;
        }
    }
};

template<typename Repr> bool kd_node<Repr>::intersects(const ray<Repr> &target,ray<Repr> &normal,real t_near,real t_far) const {
    if(type == LEAF) return static_cast<const kd_leaf<Repr>*>(this)->intersects(target,normal);
    
    assert(type == BRANCH);
    return static_cast<const kd_branch<Repr>*>(this)->intersects(target,normal,t_near,t_far);
}


template<typename Repr> struct composite_scene : Scene {
    typedef typename Repr::vector_t vector_t;
    
    bool locked;
    real fov;
    typename Repr::camera_t camera;
    std::unique_ptr<kd_node<Repr> > root;

    composite_scene(int d,kd_node<Repr> *data) : locked(false), fov(0.8), camera(d), root(data) {}
    
    color calculate_color(int x,int y,int w,int h) const {
        real fovI = (2 * std::tan(fov/2)) / w;

        ray<Repr> view = ray<Repr>(
            camera.origin(),
            (camera.forward() + camera.right() * (fovI * (x - w/2)) - camera.up() * (fovI * (y - h/2))).unit());
        ray<Repr> normal(dimension());
        if(root->intersects(view,normal,std::numeric_limits<real>::lowest(),std::numeric_limits<real>::max())) {
            real sine = dot(view.direction,normal.direction);
            return (sine <= 0 ? -sine : real(0)) * color(1.0f,0.5f,0.5f);
        }
        return background_color<Repr>(view.direction);
    }

    int dimension() const { return camera.dimension(); }
    
    void lock() { locked = true; }
    void unlock() throw() { locked = false; }
};

template<typename Repr> color background_color(const typename Repr::vector_t &dir) {
    real intensity = dot(dir,Repr::vector_t::axis(dir.dimension(),0));
    return intensity > 0 ? color(intensity,intensity,intensity) :
        color(0.0f,-intensity,-intensity);
}

#endif
