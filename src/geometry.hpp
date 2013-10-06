#ifndef geometry_hpp
#define geometry_hpp

#include <cmath>
#include <utility>

using std::declval;


typedef float REAL;


template<typename F> inline void rep(int d,F f) {
    for(int i=0; i<d; ++i) f(i);
}

namespace impl {
    template<typename T> struct vector_expr;
    
    template<typename T> struct _vector_item_t;
    template<typename T> using vector_item_t = typename _vector_item_t<T>::type;
    
    template<typename A,typename B> struct vector_sum;
    template<typename A,typename B> struct _vector_item_t<vector_sum<A,B> > {
        typedef decltype(declval<vector_item_t<A> >() + declval<vector_item_t<B> >()) type;
    };
    template<typename A,typename B> struct vector_sum : vector_expr<vector_sum<A,B> > {
        template<typename U> friend struct vector_expr;
        
        const A &a;
        const B &b;
        
        vector_sum(const vector_sum&) = delete;
        vector_sum &operator=(const vector_sum&) = delete;

        int dimension() const { return a.dimension(); }
        vector_item_t<vector_sum<A,B> > operator[](int n) const { return a[n] + b[n]; }
        
    private:
        vector_sum(const A &a,const B &b) : a(a), b(b) {}
    };
    
    template<typename A,typename B> struct vector_diff;
    template<typename A,typename B> struct _vector_item_t<vector_diff<A,B> > {
        typedef decltype(declval<vector_item_t<A> >() - declval<vector_item_t<B> >()) type;
    };
    template<typename A,typename B> struct vector_diff : vector_expr<vector_diff<A,B> > {
        template<typename U> friend struct vector_expr;
        
        const A &a;
        const B &b;
        
        vector_diff(const vector_diff&) = delete;
        vector_diff &operator=(const vector_diff&) = delete;

        int dimension() const { return a.dimension(); }
        vector_item_t<vector_diff<A,B> > operator[](int n) const { return a[n] - b[n]; }
        
    private:
        vector_diff(const A &a,const B &b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_neg;
    template<typename T> struct _vector_item_t<vector_neg<T> > {
        typedef vector_item_t<T> type;
    };
    template<typename T> struct vector_neg : vector_expr<vector_neg<T> > {
        template<typename U> friend struct vector_expr;
        
        const T &a;

        vector_neg(const vector_neg&) = delete;
        vector_neg &operator=(const vector_neg&) = delete;

        int dimension() const { return a.dimension(); }
        vector_item_t<T> operator[](int n) const { return -a[n]; }
        
    private:
        vector_neg(const T &a) : a(a) {}
    };
    
    template<typename T> struct vector_product;
    template<typename T> struct _vector_item_t<vector_product<T> > {
        typedef vector_item_t<T> type;
    };
    template<typename T> struct vector_product : vector_expr<vector_product<T> > {
        template<typename U> friend struct vector_expr;
        template<typename U> friend vector_product<U> operator*(vector_item_t<U> a,const vector_expr<U> &b);
        
        const T &a;
        vector_item_t<T> b;
        
        vector_product(const vector_product&) = delete;
        vector_product &operator=(const vector_product&) = delete;
        
        int dimension() const { return a.dimension(); }
        vector_item_t<T> operator[](int n) const { return a[n] * b; }
        
    private:
        vector_product(const T &a,vector_item_t<T> b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_quotient;
    template<typename T> struct _vector_item_t<vector_quotient<T> > {
        typedef vector_item_t<T> type;
    };
    template<typename T> struct vector_quotient : vector_expr<vector_quotient<T> > {
        template<typename U> friend struct vector_expr;
        
        const T &a;
        vector_item_t<T> b;
        
        vector_quotient(const vector_quotient&) = delete;
        vector_quotient &operator=(const vector_quotient&) = delete;
        
        int dimension() const { return a.dimension(); }
        vector_item_t<T> operator[](int n) const { return a[n] / b; }
        
    private:
        vector_quotient(const T &a,vector_item_t<T> b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_rquotient;
    template<typename T> struct _vector_item_t<vector_rquotient<T> > {
        typedef vector_item_t<T> type;
    };
    template<typename T> struct vector_rquotient : vector_expr<vector_rquotient<T> > {
        template<typename U> friend struct vector_expr;
        template<typename U> friend vector_product<T> operator/(vector_item_t<T> a,const vector_expr<T> &b);
        
        vector_item_t<T> a;
        const T &b;

        vector_rquotient(const vector_rquotient&) = delete;
        vector_rquotient &operator=(const vector_rquotient&) = delete;
        
        int dimension() const { return b.dimension(); }
        vector_item_t<T> operator[](int n) const { return a / b[n]; }
        
    private:
        vector_rquotient(vector_item_t<T> a,const T &b) : a(a), b(b) {}
    };

    template<typename Store> struct vector_methods;
    template<typename Store> struct _vector_item_t<vector_methods<Store> > {
        typedef typename Store::item_t type;
    };
    template<typename Store> struct vector_methods : vector_expr<vector_methods<Store> > {
        typedef typename Store::item_t item_t;
        
    protected:
        vector_methods() {}
        
        vector_methods(const Store &store) : store(store) {}
        vector_methods(const vector_methods<Store> &b) = default;
        ~vector_methods() = default;
        vector_methods<Store> &operator=(const vector_methods<Store> &b) = default;


        template<typename B> void add(const vector_expr<B> &b) {
            assert(dimension() == b.dimension());
            rep([this,&b](int i){ (*this)[i] += b[i]; });
        }
        
        template<typename B> void subtract(const vector_expr<B> &b) {
            assert(dimension() == b.dimension());
            rep([this,&b](int i){ (*this)[i] -= b[i]; });
        }
        
        void multiply(item_t b) {
            rep([=](int i){ (*this)[i] *= b; });
        }
        
        void divide(item_t b) {
            rep([=](int i){ (*this)[i] /= b; });
        }
        
        void make_axis(int n,item_t length) {
            rep([=](int i){ (*this)[i] = item_t(0); });
            (*this)[n] = length;
        }
        
    public:
        int dimension() const { return store.dimension(); }
        
        template<typename F> void rep(F f) const {
            ::rep(dimension(),f);
        }
        
        void fill_with(item_t v) {
            rep([=](int i){ (*this)[i] = v; });
        }
        template<typename B> void fill_with(const vector_expr<B> &b) {
            rep([this,&b](int i){ (*this)[i] = b[i]; });
        }
        template<typename F> void fill_with(F f) {
            rep([=](int i){ (*this)[i] = f(i); });
        }
        
        item_t &operator[](int n) { return store[n]; }
        item_t operator[](int n) const { return store[n]; }
        
        void normalize() { divide(this->absolute()); }
        
        Store store;
    };
    
    template<typename A,typename B> static auto dot(const vector_expr<A> &a,const vector_expr<B> &b) -> decltype(a[0] * b[0]) {
        assert(a.dimension() == b.dimension());
        decltype(a[0] * b[0]) r = 0;
        rep(a.dimension(),[&](int i){ r += a[i] * b[i]; });
        return r;
    }
    
    template<typename T> struct vector_expr {
        int dimension() const { return static_cast<const T*>(this)->dimension(); }
        vector_item_t<T> operator[](int n) const {
            return static_cast<const T*>(this)->operator[](n);
        }
        
        operator T &() { return *static_cast<T*>(this); }
        operator const T &() const { return *static_cast<const T*>(this); }
        
        template<typename B> vector_sum<T,B> operator+(const vector_expr<B> &b) const {
            assert(dimension() == b.dimension());
            return {*this,b};
        }
        
        template<typename B> vector_diff<T,B> operator-(const vector_expr<B> &b) const {
            assert(dimension() == b.dimension());
            return {*this,b};
        }
        
        vector_neg<T> operator-() const {
            return {*this};
        }
        
        vector_product<T> operator*(vector_item_t<T> b) const {
            return {*this,b};
        }
        
        vector_quotient<T> operator/(vector_item_t<T> b) const {
            return {*this,b};
        }
        
        template<typename B> bool operator==(const vector_expr<B> &b) const {
            assert(dimension() == b.dimension());
            for(int i=0; i<dimension(); ++i) {
                if((*this)[i] != b[i]) return false;
            }
            return true;
        }
        
        template<typename B> bool operator!=(const vector_expr<B> &b) const {
            return !operator==(b);
        }
        
        vector_item_t<T> square() const { return dot(*this,*this); }
        vector_item_t<T> absolute() const { return std::sqrt(square()); }
        vector_quotient<T> unit() const {
            return {*this,absolute()};
        }
    };
    
    template<typename T> vector_product<T> operator*(vector_item_t<T> a,const vector_expr<T> &b) {
        return {b,a};
    }
    
    template<typename T> vector_rquotient<T> operator/(vector_item_t<T> a,const vector_expr<T> &b) {
        return {a,b};
    }
}

using impl::dot;


template<typename Store,typename Alloc> struct vector_impl : impl::vector_methods<Store> {
    typedef impl::vector_methods<Store> base;
    typedef typename base::item_t item_t;

    explicit vector_impl(int d) : base(Alloc::template new_store<Store>(d)) {}
    
    template<typename F> vector_impl(int d,F f) : vector_impl(d) {
        base::fill_with(f);
    }
    
    vector_impl(const Store &store) : base(Alloc::template copy_store(store)) {}
    
    vector_impl(const vector_impl<Store,Alloc> &b) : vector_impl(b.store) {}
    
    template<typename B> vector_impl(const impl::vector_expr<B> &b) : vector_impl(b.dimension()) {
        base::fill_with(b);
    }
    
    ~vector_impl() {
        Alloc::template free_store(base::store);
    }
    
    vector_impl<Store,Alloc> &operator=(const vector_impl<Store,Alloc> &b) {
        Alloc::template replace_store(base::store,b.store);        
        return *this;
    }

    vector_impl<Store,Alloc> clone() const {
        vector_impl<Store,Alloc> r(base::dimension());
        r.fill_with(*this);
        return r;
    }

    template<typename B> vector_impl<Store,Alloc> &operator+=(const impl::vector_expr<B> &b) {
        base::add(b);
        return *this;
    }
    template<typename B> vector_impl<Store,Alloc> &operator-=(const impl::vector_expr<B> &b) {
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
};


template<class Store> struct matrix_methods {
    typedef impl::vector_methods<typename Store::v_store> vector_t;
    
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
    typedef impl::vector_methods<typename Store::v_store> vector_t;
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
