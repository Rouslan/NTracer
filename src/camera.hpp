#ifndef camera_hpp
#define camera_hpp

#include "geometry.hpp"


template<class Store> struct _camera {
    typedef typename Store::vector_t vector_t;
    
    _camera(int d) : store(d,vector_t(d,REAL(0)),[d](int i){ return vector_t::axis(d,i); }) {}
    template<typename F> _camera(int d,const vector_t &origin,F axes) : store(d,origin,axes) {}
    _camera(int d,const vector_t &origin,const vector_t *axes) : store(d,origin,[axes](int i){ return axes[i]; }) {}
    _camera(const Store &store) : store(store) {}

    void translate(const vector_t &v) {
        for(int i=0; i<dimension(); ++i) origin() += v[i] * axes()[i];
    }
    
    void normalize() {
        typename Store::smaller_array new_axes(dimension()-1,[this](int i){
            vector_t x(this->dimension(),REAL(0));
            for(int j=0; j<i; ++j) x += dot(this->axes()[i+1],this->axes()[j]) * this->axes()[j];
            return vector_t(this->axes()[i+1] - x);
        });

        axes()[0].normalize();
        for(int i=1; i<dimension(); ++i) {
            axes()[i] = new_axes[i-1];
            axes()[i].normalize();
        }
    }

    int dimension() const { return store.dimension(); }
    vector_t &origin() { return store.origin(); }
    const vector_t &origin() const { return store.origin(); }
    vector_t *axes() { return store.axes(); }
    const vector_t *axes() const { return store.axes(); }

    Store store;

    vector_t &right() { return axes()[0]; }
    const vector_t &right() const { return axes()[0]; }
    vector_t &up() { assert(dimension() > 1); return axes()[1]; }
    const vector_t &up() const { assert(dimension() > 1); return axes()[1]; }
    vector_t &forward() { assert(dimension() > 2); return axes()[2]; }
    const vector_t &forward() const { assert(dimension() > 2); return axes()[2]; }
};

#endif
