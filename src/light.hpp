#ifndef light_hpp
#define light_hpp

struct color {
    color() {}
    color(float _R,float _G,float _B) : R(_R), G(_G), B(_B) {}

    color operator+(const color &b) const
    {return color(R+b.R,G+b.G,B+b.B);}

    color operator-(const color &b) const
    {return color(R-b.R,G-b.G,B-b.B);}

    color operator-() const
    {return color(-R,-G,-B);}

    color operator*(float c) const
    {return color(R*c,G*c,B*c);}

    color operator/(float c) const
    {return color(R/c,G/c,B/c);}


    void operator=(const color &v)
    {R = v.R; G = v.G; B = v.B;}

    void operator+=(const color &b)
    {R += b.R; G += b.G; B += b.B;}

    void operator-=(const color &b)
    {R -= b.R; G -= b.G; B -= b.B;}

    void operator*=(float c)
    {R *= c; G *= c; B *= c;}

    void operator/=(float c)
    {R /= c; G /= c; B /= c;}

    void operator+=(float c)
    {R += c; G += c; B += c;}

    void operator-=(float c)
    {R -= c; G -= c; B -= c;}

    color operator*(const color &b) const
    {return color(R*b.R,G*b.G,B*b.B);}

    color operator/(const color &b) const
    {return color(R/b.R,G/b.G,B/b.B);}

    void operator*=(const color &b)
    {R *= b.R; G *= b.G; B *= b.B;}

    void operator/=(const color &b)
    {R /= b.R; G /= b.G; B /= b.B;}


    float R,G,B;
};

inline color operator*(float c,const color& v)
{return color(c*v.R,c*v.G,c*v.B);}

inline color operator/(float c,const color& v)
{return color(c/v.R,c/v.G,c/v.B);}

inline color operator+(float c,const color& v)
{return color(c+v.R,c+v.G,c+v.B);}

inline color operator-(float c,const color& v)
{return color(c-v.R,c-v.G,c-v.B);}


struct material {
    color c;
    float opacity, reflectivity;
};

#endif
