#ifndef light_hpp
#define light_hpp

struct color {
    color() {}
    color(float R,float G,float B) : R(R), G(G), B(B) {}

    color operator+(const color &b) const {
        return color(R+b.R,G+b.G,B+b.B);
    }

    color operator-(const color &b) const {
        return color(R-b.R,G-b.G,B-b.B);
    }

    color operator-() const {
        return color(-R,-G,-B);
    }

    color operator*(float c) const {
        return color(R*c,G*c,B*c);
    }

    color operator/(float c) const {
        return color(R/c,G/c,B/c);
    }

    color &operator+=(const color &b) {
        R += b.R; G += b.G; B += b.B;
        return *this;
    }

    color &operator-=(const color &b) {
        R -= b.R; G -= b.G; B -= b.B;
        return *this;
    }

    color &operator*=(float c) {
        R *= c; G *= c; B *= c;
        return *this;
    }

    color &operator/=(float c) {
        R /= c; G /= c; B /= c;
        return *this;
    }

    color &operator+=(float c) {
        R += c; G += c; B += c;
        return *this;
    }

    color &operator-=(float c) {
        R -= c; G -= c; B -= c;
        return *this;
    }

    color operator*(const color &b) const {
        return color(R*b.R,G*b.G,B*b.B);
    }

    color operator/(const color &b) const {
        return color(R/b.R,G/b.G,B/b.B);
    }

    color &operator*=(const color &b) {
        R *= b.R; G *= b.G; B *= b.B;
        return *this;
    }

    color &operator/=(const color &b) {
        R /= b.R; G /= b.G; B /= b.B;
        return *this;
    }
    
    bool operator==(const color &b) const {
        return R == b.R && G == b.G && B == b.B;
    }
    bool operator!=(const color &b) const {
        return R != b.R || G != b.G || B != b.B;
    }

    float R,G,B;
};

inline color operator*(float c,const color& v) {
    return color(c*v.R,c*v.G,c*v.B);
}

inline color operator/(float c,const color& v) {
    return color(c/v.R,c/v.G,c/v.B);
}

inline color operator+(float c,const color& v) {
    return color(c+v.R,c+v.G,c+v.B);
}

inline color operator-(float c,const color& v) {
    return color(c-v.R,c-v.G,c-v.B);
}


struct material {
    color c;
    float opacity, reflectivity;
    material(color c,float opacity=1,float reflectivity=0) : c(c), opacity(opacity), reflectivity(reflectivity) {}
};

#endif
