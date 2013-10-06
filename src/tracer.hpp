#ifndef tracer_hpp
#define tracer_hpp

#include "geometry.hpp"
#include "light.hpp"
#include "scene.hpp"
#include "camera.hpp"


template<class Repr> class ray {
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

template<class Repr> bool hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal);
template<class Repr> color background_color(const typename Repr::vector_t &dir);

template<class Repr> class BoxScene : public Scene {
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

template<class Repr> bool hypercube_intersects(const ray<Repr> &target,ray<Repr> &normal) {
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

template<class Repr> color background_color(const typename Repr::vector_t &dir) {
    REAL intensity = dot(dir,Repr::vector_t::axis(dir.dimension(),0));
    return intensity > REAL(0) ? color(intensity,intensity,intensity) :
        color(0.0f,-intensity,-intensity);
}

#endif
