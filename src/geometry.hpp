#ifndef geometry_hpp
#define geometry_hpp

#include <cmath>


typedef float REAL;


template<typename Store> struct vector_methods {
    typedef typename Store::item_t item_t;
    
protected:
    vector_methods() {}
    
    vector_methods(const Store &store) : store(store) {}
    vector_methods(const vector_methods<Store> &b) = default;
    ~vector_methods() = default;
    vector_methods<Store> &operator=(const vector_methods &b) = default;
    
    
    void negate() {
        rep([=](int i){ (*this)[i] = -(*this)[i]; });
    }

    void add(const vector_methods<Store> &b) {
        assert(dimension() == b.dimension());
        rep([this,&b](int i){ (*this)[i] += b[i]; });
    }

    void subtract(const vector_methods<Store> &b) {
        assert(dimension() == b.dimension());
        rep([this,&b](int i){ (*this)[i] -= b[i]; });
    }

    void multiply(item_t b) {
        rep([=](int i){ (*this)[i] *= b; });
    }

    void divide(item_t b) {
        rep([=](int i){ (*this)[i] /= b; });
    }
    
    void div_into(item_t b) {
        rep([=](int i){ (*this)[i] = b / (*this)[i]; });
    }

    void make_axis(int n,item_t length) {
        rep([=](int i){ (*this)[i] = item_t(0); });
        (*this)[n] = length;
    }
    
    void copy_from(const vector_methods<Store> &b) {
        rep([this,&b](int i){ (*this)[i] = b[i]; });
    }

public:
    int dimension() const { return store.dimension(); }
    
    template<typename F> void rep(F f) const {
        return Store::rep(dimension(),f);
    }
    
    void fill_with(item_t v) {
        rep([=](int i){ (*this)[i] = v; });
    }
    template<typename F> void fill_with(F f) {
        rep([=](int i){ (*this)[i] = f(i); });
    }

    item_t &operator[](int n) { return store[n]; }
    item_t operator[](int n) const { return store[n]; }

    bool operator==(const vector_methods<Store> &b) const {
        assert(dimension() == b.dimension());
        bool r = true;
        rep([&](int i){ r = r && store[i] == b[i]; });
        return r;
    }

    bool operator!=(const vector_methods<Store> &b) { return !(operator==(b)); }

    static float dot (const vector_methods<Store> &a,const vector_methods<Store> &b) {
        assert(a.dimension() == b.dimension());
        item_t r = item_t(0);
        a.rep([&](int i){ r += a[i] * b[i]; });
        return r;
    }
    
    typename Store::item_t square() const { return dot(*this,*this); }
    typename Store::item_t absolute() const { return std::sqrt(square()); }
    
    void normalize() { divide(absolute()); }
    
    Store store;
};

template<typename Store,typename Alloc> struct vector_impl;
template<typename Store,typename Alloc> vector_impl<Store,Alloc> operator/(typename vector_impl<Store,Alloc>::item_t,const vector_impl<Store,Alloc>&);

template<typename Store,typename Alloc> struct vector_impl : vector_methods<Store> {
    typedef vector_methods<Store> base;
    typedef typename base::item_t item_t;
    
    friend vector_impl<Store,Alloc> operator/<Store,Alloc>(item_t,const vector_impl<Store,Alloc>&);

    explicit vector_impl(int d) : base(Alloc::template new_store<Store>(d)) {}
    
    template<typename F> vector_impl(int d,F f) : vector_impl(d) {
        base::fill_with(f);
    }
    
    vector_impl(const Store &store) : base(Alloc::template copy_store(store)) {}
    
    vector_impl(const vector_impl<Store,Alloc> &b) : vector_impl(b.store) {}
    
    template<typename AllocB> vector_impl(const vector_impl<Store,AllocB> &b) : vector_impl(b.dimension()) {
        this->copy_from(b);
    }
    
    ~vector_impl() {
        Alloc::template free_store(base::store);
    }
    
    vector_impl<Store,Alloc> &operator=(const vector_impl<Store,Alloc> &b) {
        Alloc::template replace_store(base::store,b.store);        
        return *this;
    }
    
protected:
    template<typename... Args> vector_impl<Store,Alloc> apply_to_clone(void (base::*func)(Args...),Args... args) const {
        vector_impl<Store,Alloc> r = clone();
        (r.*func)(args...);
        return r;
    }
    
public:
    vector_impl<Store,Alloc> clone() const {
        vector_impl<Store,Alloc> r(base::dimension());
        r.copy_from(*this);
        return r;
    }
    
    vector_impl<Store,Alloc> operator+(const base &b) const { return apply_to_clone<const base&>(&vector_impl<Store,Alloc>::add,b); }
    vector_impl<Store,Alloc> operator-(const base &b) const { return apply_to_clone<const base&>(&vector_impl<Store,Alloc>::subtract,b); }
    vector_impl<Store,Alloc> operator-() const { return apply_to_clone(&vector_impl<Store,Alloc>::negate); }
    vector_impl<Store,Alloc> operator*(item_t b) const { return apply_to_clone(&vector_impl<Store,Alloc>::multiply,b); }
    vector_impl<Store,Alloc> operator/(item_t b) const { return apply_to_clone(&vector_impl<Store,Alloc>::divide,b); }

    vector_impl<Store,Alloc> &operator+=(const base &b) {
        base::add(b);
        return *this;
    }
    vector_impl<Store,Alloc> &operator-=(const base &b) {
        base::subtract(b);
        return *this;
    }
    vector_impl<Store,Alloc> &operator*=(item_t b) {
        base::multiply(b);
        return *this;
    }
    vector_impl<Store,Alloc> &operator/=(item_t b) {
        base::divide(b);
        return *this;
    }

    static vector_impl<Store,Alloc> axis(int d,int n,item_t length = item_t(1)) {
        vector_impl<Store,Alloc> r(d);
        r.make_axis(n,length);
        return r;
    }
    
    vector_impl<Store,Alloc> unit() const { return operator/(base::absolute()); }
};

template<typename Store,typename Alloc> vector_impl<Store,Alloc> operator*(typename vector_impl<Store,Alloc>::item_t a,const vector_impl<Store,Alloc> &b) {
    return b * a;
}

template<typename Store,typename Alloc> vector_impl<Store,Alloc> operator/(typename vector_impl<Store,Alloc>::item_t a,const vector_impl<Store,Alloc> &b) {
    return b.apply_to_clone(&vector_impl<Store,Alloc>::div_into,a);
}


template<class Store> struct matrix_methods {
    typedef vector_methods<typename Store::v_store> vector_t;
    
protected:
    matrix_methods(const Store &store) : store(store) {}
    matrix_methods(const matrix_methods<Store> &b) = default;
    ~matrix_methods() = default;
    matrix_methods<Store> &operator=(const matrix_methods<Store> &b) = default;
    
    void multiply(matrix_methods<Store> &r,const matrix_methods<Store> &b) const {
        assert(dimension() == r.dimension() && dimension() == b.dimension());

        Store::rep(dimension(),[=,&b,&r](int row,int col){
            r[row][col] = REAL(0);
            Store::rep1(this->dimension(),[=,&b,&r](int i){ r[row][col] += (*this)[row][i] * b[i][col]; });
        });
    }
    
    void multiply(vector_t &r,const vector_t &b) const {
        assert(dimension() == b.dimension());
        r.fill_with(REAL(0));
        Store::rep(dimension(),[&,this](int row,int col){ r[row] += (*this)[row][col] * b[col]; });
    }
    
    /* given vector p, produces matrix r such that r * p is equal to:
       dot(p,a)*(a*(std::cos(theta)-1) - b*std::sin(theta)) + dot(p,b)*(b*(std::cos(theta)-1) + a*std::sin(theta)) + p */
    static void rotation(matrix_methods<Store> &r,const vector_t &a, const vector_t &b, REAL theta) {
        assert(r.dimension() == a.dimension() && r.dimension() == b.dimension());

        REAL c = std::cos(theta) - REAL(1);
        REAL s = std::sin(theta);

        Store::rep(r.dimension(),[&,c,s](int row,int col){
            REAL x = a[row]*(a[col]*c - b[col]*s) + b[row]*(b[col]*c + a[col]*s);
            if(col == row) x += REAL(1);
            
            r[row][col] = x;
        });
    }
    
    static void scale(matrix_methods<Store> &r,const vector_t &a) {
        assert(r.dimension() == a.dimension());
        Store::rep(r.dimension(),[&](int row,int col){ r[row][col] = row == col ? a[row] : REAL(0); });
    }

    static void scale(matrix_methods<Store> &r,REAL a) {
        Store::rep(r.dimension(),[&r,a](int row,int col){ r[row][col] = row == col ? a : REAL(0); });
    }

public:
    REAL *operator[](int n) { return store[n]; }
    const REAL *operator[](int n) const { return store[n]; }

    int dimension() const { return store.dimension(); }

    Store store;
};

template<typename Store,typename Alloc> struct matrix_impl : matrix_methods<Store> {
    typedef matrix_methods<Store> base;
    typedef vector_methods<typename Store::v_store> vector_t;
    typedef vector_impl<typename Store::v_store,Alloc> vector_concrete;


    explicit matrix_impl(int d) : base(Alloc::template new_store<Store>(d)) {}

    matrix_impl(const Store &store) : base(Alloc::template copy_store(store)) {}
    
    matrix_impl(const matrix_impl<Store,Alloc> &b) : matrix_impl(b.store) {}
    
    /*template<typename AllocB> matrix_impl(const matrix_impl<Store,AllocB> &b) : matrix_impl(b.dimension()) {
        this->copy_from(b);
    }*/
    
    ~matrix_impl() {
        Alloc::template free_store(base::store);
    }
    
    matrix_impl<Store,Alloc> &operator=(const matrix_impl<Store,Alloc> &b) {
        Alloc::template replace_store(base::store,b.store);        
        return *this;
    }


    matrix_impl<Store,Alloc> operator*(const matrix_methods<Store> &b) const {
        matrix_impl<Store,Alloc> r(base::dimension());
        base::multiply(r,b);
        return r;
    }
    
    inline vector_concrete operator*(const vector_t &b) const {
        vector_concrete r(base::dimension());
        base::multiply(r,b);
        return r;
    }

    static matrix_impl<Store,Alloc> rotation(const vector_t &a,const vector_t &b,REAL theta) {
        matrix_impl<Store,Alloc> r(a.dimension());
        base::rotation(r,a,b,theta);
        return r;
    }
    
    static matrix_impl<Store,Alloc> scale(const vector_t &a) {
        matrix_impl<Store,Alloc> r(a.dimension());
        base::scale(r,a);
        return r;
    }

    static matrix_impl<Store,Alloc> scale(int d,REAL a) {
        matrix_impl<Store,Alloc> r(d);
        base::scale(r,a);
        return r;
    }

    static matrix_impl<Store,Alloc> identity(int d) {
        return scale(d,REAL(1));
    }
};




/*
template<int N,class T> struct _extended_matrix
{
    _extended_matrix() {}
    _extended_matrix(float p11,float p12,float p13,float p14,float p21,float p22,float p23,float p24,float p31,float p32,float p33,float p34) :
      _11(p11),_12(p12),_13(p13),_14(p14),_21(p21),_22(p22),_23(p23),_24(p24),_31(p31),_32(p32),_33(p33),_34(p34) {}


    struct _Item
    {
        int row, col;
        const T *a, *b; T &out;
        _Item(int r, int c, T *_a, T *_b, T *o)
            : row(r), col(c), a(_a), b(_b), out(o[r][c]) { out = 0; }
        void operator()(int i) { out += a[row][i] * b[i][col]; }
    };

    struct _Row
    {
        int row;
        const T *a, *b; T *o;
        _Row(int r, T *_a, T *_b, T *_o)
            : row(r), a(_a), b(_b), o(_o) {}
        void operator()(int i) { rep<N+1>(_Item(row,i,a,b,o)); out[i][N] += a[i][N]; }
    };

    struct _Mat
    {
        const T *a, *b; T *o;
        _Mat(T *_a, T *_b, T *_o)
            : a(_a), b(_b), o(_o) {}
        void operator()(int i) { rep<N>(_Row(i,a,b,o)); }
    };

    _extended_matrix operator*(const _extended_matrix &b) const
    {
        _extended_matrix result;
        rep<N>(_Mat(val,b.val,result.val));
        return result;
    }

    //struct _ColRot
    //{
    //    T d; int col; T (*o)[N];
    //    _ColRot(T _d, int _col, T (*_o)[N]) : d(_d), col(_col), o(_o) {}
    //    void operator()(int i) { o[i][col] = d; }
    //};
    //struct _MatRot
    //{
    //    T d; const T *a, *b; T (*o)[N];
    //    _MatRot(const T *_a, const T *_b, T (*_o)[N], T _d) : a(_a), b(_b), o(_o), d(_d) {}
    //    void operator()(int i) { rep<N>(_ColRot( (a[i]*a[i] + b[i]*b[i]) * d , i, o)); o[i][i] += T(1); o[i][N] = 0.0f; }
    //};
    //static _extended_matrix Rotation(const _vector<N,T> &A, const _vector<N,T> &B, float theta)
    //{
    //    _extended_matrix result;
    //    rep<N>(_MatRow(A.val, B.val, result.val, T(1) - cos(theta)));
    //    return result;
    //}


    struct _Scale { T *x, *v;
        _Scale(T *_x, T *_v) : x(_x), v(_v) {}
        void operator()(int n) { x[n] = ((n % (N+1)) == (n / (N+1))) ? v[n / (N+1)] : 0.0f; } };

    static _extended_matrix Scale(const _vector<N,T> &A)
    {
        _extended_matrix result;
        rep<(N+1)*N>(_Scale(result.val));
        return result;
    }

    struct _Identity { T *x;
        _Identity(T *_x) : x(_x) {}
        void operator()(int n) { x[n] = ((n % (N+1)) == (n / (N+1))) ? 1.0f : 0.0f; } };

    static _extended_matrix Identity()
    {
        _extended_matrix result;
        rep<(N+1)*N>(_Identity(result.val));
        return result;
    }

    struct _Translation { T *x, *v;
        _Translation(T *_x, T *_v) : x(_x), v(_v) {}
        void operator()(int n) { x[n] = ((n % (N+1)) == (n / (N+1))) ? 1.0f : (n == N ? v[n / (N+1)] : 0.0f); } };

    static _extended_matrix Translation(const _vector<N,T> &V)
    {
        _extended_matrix result;
        rep<N*N>(_Identity(result.val,V.val));
        return result;
    }

    T val[N][N+1];
};

template<int N,class T> struct _MatVecMul3 { T *r; const T (*a)[N+1]; const T *b;
    _MatVecMul3(T *_r, const T (*_a)[N+1], const T *_b) : r(_r), a(_a), b(_b) {}
    void operator()(int n) { r[n] = a[n][N]; rep<N>(_MatVecMul2<N,T>(r[n],a[n],b)); } };

template<int N,class T> inline _vector<N,T> operator*(const _extended_matrix<N,T> &a, const _vector<N,T> &b)
{
    _vector<N,T> result;
    rep<N>(_MatVecMul3<N,T>(result.val,a.val,b.val));
    return result;
}
*/


#endif
