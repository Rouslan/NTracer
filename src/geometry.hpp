#ifndef geometry_hpp
#define geometry_hpp

#include <cmath>
#include <utility>
#include <stdexcept>


#ifdef __GNUC__
    #define RESTRICT __restrict__
#else
    #define RESTRICT
#endif


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
        
        vector_sum(const vector_sum<A,B>&) = delete;
        vector_sum &operator=(const vector_sum<A,B>&) = delete;

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
        
        vector_diff(const vector_diff<A,B>&) = delete;
        vector_diff &operator=(const vector_diff<A,B>&) = delete;

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

        vector_neg(const vector_neg<T>&) = delete;
        vector_neg &operator=(const vector_neg<T>&) = delete;

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
        
        vector_product(const vector_product<T>&) = delete;
        vector_product &operator=(const vector_product<T>&) = delete;
        
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
        
        vector_quotient(const vector_quotient<T>&) = delete;
        vector_quotient &operator=(const vector_quotient<T>&) = delete;
        
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

        vector_rquotient(const vector_rquotient<T>&) = delete;
        vector_rquotient &operator=(const vector_rquotient<T>&) = delete;
        
        int dimension() const { return b.dimension(); }
        vector_item_t<T> operator[](int n) const { return a / b[n]; }
        
    private:
        vector_rquotient(vector_item_t<T> a,const T &b) : a(a), b(b) {}
    };
    
    template<typename T,typename F> struct vector_apply;
    template<typename T,typename F> struct _vector_item_t<vector_apply<T,F> > {
        typedef typename std::result_of<F(vector_item_t<T>)>::type type;
    };
    template<typename T,typename F> struct vector_apply : vector_expr<vector_apply<T,F> > {
        template<typename U> friend struct vector_expr;

        const T &a;
        F f;

        vector_apply(const vector_apply<T,F>&) = delete;
        vector_apply &operator=(const vector_apply<T,F>&) = delete;
        
        int dimension() const { return a.dimension(); }
        vector_item_t<vector_apply<T,F> > operator[](int n) const { return f(a[n]); }
        
    private:
        vector_apply(const T &a,F f) : a(a), f(f) {}
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
        template<typename F> typename std::enable_if<!std::is_arithmetic<F>::value>::type fill_with(F f) {
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
        
        template<typename F> vector_apply<T,F> apply(F f) const {
            return {*this,f};
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


template<class Store> struct matrix_methods;

namespace impl {
    template<class Store> struct matrix_row;
    template<class Store> struct _vector_item_t<matrix_row<Store> > {
        typedef REAL type;
    };
    template<class Store> struct matrix_row : vector_expr<matrix_row<Store> > {
        friend struct matrix_methods<Store>;
        
        matrix_methods<Store> &a;
        const int row;

        //matrix_row(const matrix_row<Store>&) = delete;
        matrix_row<Store> &operator=(const matrix_row<Store>&) = delete;
        
        template<typename B> matrix_row &operator=(const vector_expr<B> &b) {
            for(int i=0; i<dimension(); ++i) a.store[row][i] = b[i];
            return *this;
        }
        
        int dimension() const { return a.dimension(); }
        REAL &operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.store[row][n];
        }
        
        operator REAL*() const { return a.store[row]; }
        
    private:
        matrix_row(matrix_methods<Store> &a,int row) : a(a), row(row) {}
    };
    
    template<class Store> struct const_matrix_row;
    template<class Store> struct _vector_item_t<const_matrix_row<Store> > {
        typedef REAL type;
    };
    template<class Store> struct const_matrix_row : vector_expr<const_matrix_row<Store> > {
        friend struct matrix_methods<Store>;
        
        const matrix_methods<Store> &a;
        const int row;

        //const_matrix_row(const const_matrix_row<Store>&) = delete;
        const_matrix_row<Store> &operator=(const const_matrix_row<Store>&) = delete;
        
        int dimension() const { return a.dimension(); }
        REAL operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.store[row][n];
        }
        
        operator const REAL*() const { return a.store[row]; }
        
    private:
        const_matrix_row(const matrix_methods<Store> &a,int row) : a(a), row(row) {}
    };
    
    template<class Store> struct matrix_column;
    template<class Store> struct _vector_item_t<matrix_column<Store> > {
        typedef REAL type;
    };
    template<class Store> struct matrix_column : vector_expr<matrix_column<Store> > {
        friend struct matrix_methods<Store>;
        
        matrix_methods<Store> &a;
        const int col;

        //matrix_column(const matrix_row<Store>&) = delete;
        matrix_column<Store> &operator=(const matrix_row<Store>&) = delete;
        
        template<typename B> matrix_column &operator=(const vector_expr<B> &b) {
            for(int i=0; i<dimension(); ++i) a.store[i][col] = b[i];
        }
        
        int dimension() const { return a.dimension(); }
        REAL &operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.store[n][col];
        }
        
    private:
        matrix_column(matrix_methods<Store> &a,int col) : a(a), col(col) {}
    };
    
    template<class Store> struct const_matrix_column;
    template<class Store> struct _vector_item_t<const_matrix_column<Store> > {
        typedef REAL type;
    };
    template<class Store> struct const_matrix_column : vector_expr<const_matrix_column<Store> > {
        friend struct matrix_methods<Store>;
        
        const matrix_methods<Store> &a;
        const int col;

        //const_matrix_column(const const_matrix_column<Store>&) = delete;
        const_matrix_column<Store> &operator=(const const_matrix_column<Store>&) = delete;
        
        int dimension() const { return a.dimension(); }
        REAL operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.store[n][col];
        }
        
    private:
        const_matrix_column(const matrix_methods<Store> &a,int col) : a(a), col(col) {}
    };
}

template<class Store> struct matrix_methods {
    typedef impl::vector_methods<typename Store::v_store> vector_t;
    
protected:
    matrix_methods(const Store &store) : store(store) {}
    matrix_methods(const matrix_methods<Store> &b) = default;
    ~matrix_methods() = default;
    matrix_methods<Store> &operator=(const matrix_methods<Store> &b) = default;
    
    void multiply(matrix_methods<Store> &RESTRICT r,const matrix_methods<Store> &b) const {
        assert(dimension() == r.dimension() && dimension() == b.dimension());

        Store::rep(dimension(),[=,&b,&r](int row,int col){
            r[row][col] = REAL(0);
            Store::rep1(this->dimension(),[=,&b,&r](int i){ r[row][col] += (*this)[row][i] * b[i][col]; });
        });
    }
    
    void multiply(vector_t &RESTRICT r,const vector_t &b) const {
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
    
    /* Crout matrix decomposition with partial pivoting.
        
       Although this generates two matrices--an upper (U) and lower (L)
       triangular matrix--the result is stored in a single matrix object like
       so:
        
       L[0][0]   U[0][1]   U[0][2]   ... U[0][n-1]
       L[1][0]   L[1][1]   U[1][2]   ... U[1][n-1]
       L[2][0]   L[2][1]   L[2][2]   ... U[2][n-1]
       .         .         .         .   .
       .         .         .          .  .
       .         .         .           . .
       L[n-1][0] L[n-1][1] L[n-1][2] ... L[n-1][n-1]
        
       Every item of the upper matrix's diagonal is always 1 and is not present
       in the result. If the first element of the source matrix is zero, the
       result will instead be computed from a matrix equal to the source matrix
       except having the first row swapped with another. The return value
       indicates how many swaps were performed. If the return value is -1, the
       matrix is singular and the contents of "lu" will be undefined.
    */
    int decompose(matrix_methods<Store> &RESTRICT lu,int *pivots) const {
        assert(dimension() == lu.dimension());
        
        int swapped = 0;

        for(int i=0; i<dimension(); ++i) pivots[i] = i;
        
        for(int j=0; j<dimension(); ++j) {
            for(int i=j; i<dimension(); ++i) {
                REAL sum = REAL(0);
                for(int k=0; k<j; ++k) sum += lu[i][k] * lu[k][j];
                lu[i][j] = (*this)[pivots[i]][j] - sum;
            }
            
            if(lu[j][j] == REAL(0)) {
                for(int i=j+1; i<dimension(); ++i) {
                    if(lu[i][j] != REAL(0)) {
                        std::swap(pivots[i],pivots[j]);
                        ++swapped;
                        for(int k=0; k<j+1; ++k) std::swap(lu[i][k],lu[j][k]);
                        goto okay;
                    }
                }
                return -1;
            }
        
        okay:
            
            for(int i=j+1; i<dimension(); ++i) {
                REAL sum = REAL(0);
                for(int k=0; k<j; ++k) sum += lu[j][k] * lu[k][i];
                lu[j][i] = ((*this)[pivots[j]][i] - sum) / lu[j][j];
            }
        }
        
        return swapped;
    }
    
    REAL determinant(matrix_methods<Store> &RESTRICT tmp) const {
        assert(dimension() == tmp.dimension());
        
        typename Store::pivot_buffer pivot(dimension());
        int swapped = decompose(tmp,pivot.data);
        if(swapped < 0) return REAL(0);
        
        REAL r = swapped % 2 ? REAL(-1) : REAL(1);
        for(int i=0; i<dimension(); ++i) r *= tmp[i][i];
        return r;
    }
    
    void inverse(matrix_methods<Store> &RESTRICT inv,matrix_methods<Store> & RESTRICT tmp) const {
        assert(dimension() == r.dimension() && dimension() == tmp.dimension());

        typename Store::pivot_buffer pivot(dimension());
        int swapped = decompose(tmp,pivot.data);
        if(swapped < 0) throw std::domain_error("matrix is singular (uninvertible)");

        // forward substitution
        // store the result in the lower triangle of tmp
        for(int c=0; c<dimension(); ++c) {
            tmp[c][c] = REAL(1) / tmp[c][c];
            
            for(int r=c+1; r<dimension(); ++r) {
                REAL sum = 0;
                for(int i=c; i<r; ++i) sum -= tmp[r][i] * tmp[i][c];
                tmp[r][c] = sum / tmp[r][r];
            }
        }

        // back substitution
        for(int c=0; c<dimension(); ++c) {
            int pc = pivot.data[c];
            inv[dimension()-1][pc] = tmp[dimension()-1][c];
            
            for(int r=dimension()-2; r>-1; --r) {
                REAL sum = 0;
                if(r >= c) sum = tmp[r][c];
                for(int i=r+1; i<dimension(); ++i) sum -= tmp[r][i] * inv[i][pc];
                inv[r][pc] = sum;
            }
        }
    }
    
    void transpose(matrix_methods<Store> &RESTRICT t) const {
        for(int r=0; r<dimension(); ++r) {
            for(int c=0; c<dimension(); ++c) t[r][c] = (*this)[c][r];
        }
    }

public:
    /* Calculates the determinant by using itself to store the intermediate
       calculations. This avoids allocating space for another matrix but loses
       the original contents of this matrix. */
    REAL determinant_inplace() {
        int swapped = 0;

        for(int j=0; j<dimension(); ++j) {
            for(int i=j; i<dimension(); ++i) {
                REAL sum = REAL(0);
                for(int k=0; k<j; ++k) sum += (*this)[i][k] * (*this)[k][j];
                (*this)[i][j] = (*this)[i][j] - sum;
            }
            
            if((*this)[j][j] == REAL(0)) {
                for(int i=j+1; i<dimension(); ++i) {
                    if((*this)[i][j] != REAL(0)) {
                        ++swapped;
                        for(int k=0; k<dimension(); ++k) std::swap((*this)[i][k],(*this)[j][k]);
                        goto okay;
                    }
                }
                return REAL(0);
            }
        
        okay:
            
            for(int i=j+1; i<dimension(); ++i) {
                REAL sum = REAL(0);
                for(int k=0; k<j; ++k) sum += (*this)[j][k] * (*this)[k][i];
                (*this)[j][i] = ((*this)[j][i] - sum) / (*this)[j][j];
            }
        }

        REAL r = swapped % 2 ? REAL(-1) : REAL(1);
        for(int i=0; i<dimension(); ++i) r *= (*this)[i][i];
        return r;
    }
    
    impl::matrix_row<Store> operator[](int n) { return {*this,n}; }
    impl::const_matrix_row<Store> operator[](int n) const { return {*this,n}; }
    
    REAL *data() { return store[0]; }
    const REAL *data() const { return store[0]; }
    
    impl::matrix_column<Store> column(int n) { return {*this,n}; }
    impl::const_matrix_column<Store> column(int n) const { return {*this,n}; }

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
    
    vector_concrete operator*(const vector_t &b) const {
        vector_concrete r(base::dimension());
        base::multiply(r,b);
        return r;
    }
    
    template<typename B> vector_concrete operator*(const impl::vector_expr<B> &b) const {
        return operator*(vector_concrete(b));
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
    
    matrix_impl<Store,Alloc> transpose() const {
        matrix_impl<Store,Alloc> r(base::dimension());
        base::transpose(r);
        return r;
    }
    
    matrix_impl<Store,Alloc> inverse() const {
        matrix_impl<Store,Alloc> r(base::dimension()), tmp(base::dimension());
        base::inverse(r,tmp);
        return r;
    }
    
    REAL determinant() const {
        matrix_impl<Store,Alloc> tmp(base::dimension());
        return base::determinant(tmp);
    }
};


template<typename T> struct smaller_store {
    typedef T type;
};

template<typename T> struct _smaller;
template<typename Store,typename Alloc> struct _smaller<matrix_impl<Store,Alloc> > {
    typedef matrix_impl<typename smaller_store<Store>::type,Alloc> type;
};
template<typename Store> struct _smaller<matrix_methods<Store> > {
    typedef matrix_methods<typename smaller_store<Store>::type> type;
};

template<typename T> using smaller = typename _smaller<T>::type;


namespace impl {
    // generalized cross product
    template<typename VStore,typename MStore> void cross(vector_methods<VStore> &r,matrix_methods<MStore> &tmp,const vector_methods<VStore> *vs) {
        assert(r.dimension() == (tmp.dimension()-1));

        int f = r.dimension() % 2 ? 1 : -1;
        
        for(int i=0; i<r.dimension(); ++i) {
            assert(r.dimension() == vs[i].dimension());
            
            for(int j=0; j<r.dimension()-1; ++j) {
                for(int k=0; k<i; ++k) tmp[k][j] = vs[j][k];
                for(int k=i+1; k<r.dimension(); ++k) tmp[k-1][j] = vs[j][k];
            }
            r[i] = f * tmp.determinant_inplace();
            f = -f;
        }
    }
}

#endif
