#ifndef light_hpp
#define light_hpp

struct color {
    color() {}
    color(float r,float g,float b) : vals{r,g,b} {}
    
    float &r() { return vals[0]; }
    float r() const { return vals[0]; }
    float &g() { return vals[1]; }
    float g() const { return vals[1]; }
    float &b() { return vals[2]; }
    float b() const { return vals[2]; }

    color operator*(float c) const {
        return {r()*c,g()*c,b()*c};
    }

    color operator/(float c) const {
        return {r()/c,g()/c,b()/c};
    }

    color &operator*=(float c) {
        r() *= c; g() *= c; b() *= c;
        return *this;
    }

    color &operator/=(float c) {
        r() /= c; g() /= c; b() /= c;
        return *this;
    }

    color &operator+=(float c) {
        r() += c; g() += c; b() += c;
        return *this;
    }

    color &operator-=(float c) {
        r() -= c; g() -= c; b() -= c;
        return *this;
    }
    
    color operator-() {
        return {-r(),-g(),-b()};
    }

    color operator*(const color &cb) const {
        return {r()*cb.r(),g()*cb.g(),b()*cb.b()};
    }

    color operator/(const color &cb) const {
        return {r()/cb.r(),g()/cb.g(),b()/cb.b()};
    }
    
    color operator+(const color &cb) const {
        return {r()+cb.r(),g()+cb.g(),b()+cb.b()};
    }

    color operator-(const color &cb) const {
        return {r()-cb.r(),g()-cb.g(),b()-cb.b()};
    }

    color &operator*=(const color &cb) {
        r() *= cb.r(); g() *= cb.g(); b() *= cb.b();
        return *this;
    }

    color &operator/=(const color &cb) {
        r() /= cb.r(); g() /= cb.g(); b() /= cb.b();
        return *this;
    }
    
    color &operator+=(const color &cb) {
        r() += cb.r(); g() += cb.g(); b() += cb.b();
        return *this;
    }

    color &operator-=(const color &cb) {
        r() -= cb.r(); g() -= cb.g(); b() -= cb.b();
        return *this;
    }
    
    bool operator==(const color &cb) const {
        return r() == cb.r() && g() == cb.g() && b() == cb.b();
    }
    bool operator!=(const color &cb) const {
        return r() != cb.r() || g() != cb.g() || b() != cb.b();
    }


    float vals[3];
};

inline color operator*(float c,const color& v) {
    return {c*v.r(),c*v.g(),c*v.b()};
}

inline color operator/(float c,const color& v) {
    return {c/v.r(),c/v.g(),c/v.b()};
}

inline color operator+(float c,const color& v) {
    return {c+v.r(),c+v.g(),c+v.b()};
}

inline color operator-(float c,const color& v) {
    return {c-v.r(),c-v.g(),c-v.b()};
}

#endif
