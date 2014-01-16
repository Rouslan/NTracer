#ifndef camera_hpp
#define camera_hpp

#include "geometry.hpp"


template<class Store> struct camera {
    vector<Store> origin;
    matrix<Store> t_orientation;
    
    camera(int d) : origin(d,0), t_orientation(matrix<Store>::identity(d)) {}
    template<typename F> camera(const vector<Store> &origin,F axes) : origin(origin), t_orientation(origin.dimension()) {
        for(int i=0; i<dimension(); ++i) t_orientation[i] = axes(i);
    }
    camera(const vector<Store> &origin,const vector<Store> *axes) : camera(origin,[=](int i){ return axes[i]; }) {}

    void translate(const vector<Store> &v) {
        for(int i=0; i<dimension(); ++i) origin += v[i] * t_orientation[i];
    }
    
    void transform(const matrix<Store> &m) {
        t_orientation = t_orientation.mult_transpose(m);
    }
    
    void normalize() {
        typename Store::template smaller_init_array<vector<Store> > new_axes(dimension()-1,[this](int i) -> vector<Store> {
            vector<Store> x(this->dimension(),0);
            for(int j=0; j<i; ++j) x += dot(this->t_orientation[i+1],this->t_orientation[j]) * this->t_orientation[j];
            return this->t_orientation[i+1] - x;
        });

        t_orientation[0] = t_orientation[0] / t_orientation[0].absolute();
        for(int i=1; i<dimension(); ++i) {
            t_orientation[i] = new_axes[i-1].unit();
        }
    }

    int dimension() const { return origin.dimension(); }

    impl::matrix_row<Store> right() { return t_orientation[0]; }
    impl::const_matrix_row<Store> right() const { return t_orientation[0]; }
    impl::matrix_row<Store> up() { assert(dimension() > 1); return t_orientation[1]; }
    impl::const_matrix_row<Store> up() const { assert(dimension() > 1); return t_orientation[1]; }
    impl::matrix_row<Store> forward() { assert(dimension() > 2); return t_orientation[2]; }
    impl::const_matrix_row<Store> forward() const { assert(dimension() > 2); return t_orientation[2]; }
};

#endif
