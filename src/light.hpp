#ifndef light_hpp
#define light_hpp

template<typename T> struct _color {
    _color() {}
    _color(T r,T g,T b) : vals{r,g,b} {}

    T &r() { return vals[0]; }
    T r() const { return vals[0]; }
    T &g() { return vals[1]; }
    T g() const { return vals[1]; }
    T &b() { return vals[2]; }
    T b() const { return vals[2]; }

    _color operator*(T c) const {
        return {r()*c,g()*c,b()*c};
    }

    _color operator/(T c) const {
        return {r()/c,g()/c,b()/c};
    }

    _color &operator*=(T c) {
        r() *= c; g() *= c; b() *= c;
        return *this;
    }

    _color &operator/=(T c) {
        r() /= c; g() /= c; b() /= c;
        return *this;
    }

    _color &operator+=(T c) {
        r() += c; g() += c; b() += c;
        return *this;
    }

    _color &operator-=(T c) {
        r() -= c; g() -= c; b() -= c;
        return *this;
    }

    _color operator-() {
        return {-r(),-g(),-b()};
    }

    _color operator*(const _color &cb) const {
        return {r()*cb.r(),g()*cb.g(),b()*cb.b()};
    }

    _color operator/(const _color &cb) const {
        return {r()/cb.r(),g()/cb.g(),b()/cb.b()};
    }

    _color operator+(const _color &cb) const {
        return {r()+cb.r(),g()+cb.g(),b()+cb.b()};
    }

    _color operator-(const _color &cb) const {
        return {r()-cb.r(),g()-cb.g(),b()-cb.b()};
    }

    _color &operator*=(const _color &cb) {
        r() *= cb.r(); g() *= cb.g(); b() *= cb.b();
        return *this;
    }

    _color &operator/=(const _color &cb) {
        r() /= cb.r(); g() /= cb.g(); b() /= cb.b();
        return *this;
    }

    _color &operator+=(const _color &cb) {
        r() += cb.r(); g() += cb.g(); b() += cb.b();
        return *this;
    }

    _color &operator-=(const _color &cb) {
        r() -= cb.r(); g() -= cb.g(); b() -= cb.b();
        return *this;
    }

    bool operator==(const _color &cb) const {
        return r() == cb.r() && g() == cb.g() && b() == cb.b();
    }
    bool operator!=(const _color &cb) const {
        return r() != cb.r() || g() != cb.g() || b() != cb.b();
    }


    T vals[3];
};

template<typename T> inline _color<T> operator*(T c,const _color<T>& v) {
    return {c*v.r(),c*v.g(),c*v.b()};
}

template<typename T> inline _color<T> operator/(T c,const _color<T>& v) {
    return {c/v.r(),c/v.g(),c/v.b()};
}

template<typename T> inline _color<T> operator+(T c,const _color<T>& v) {
    return {c+v.r(),c+v.g(),c+v.b()};
}

template<typename T> inline _color<T> operator-(T c,const _color<T>& v) {
    return {c-v.r(),c-v.g(),c-v.b()};
}

using color = _color<float>;

#endif
