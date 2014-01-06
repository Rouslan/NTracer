#ifndef geometry_hpp
#define geometry_hpp

#include <cmath>
#include <stdexcept>

#include "v_array.hpp"


typedef float real;


namespace impl {
    template<typename T> struct vector_expr;
    
    template<typename VExpr> struct vector_expr_adapter : vector_expr<vector_expr_adapter<VExpr> > {
        static const int v_score = VExpr::v_score;
        static constexpr bool temporary = VExpr::temporary;
        
        VExpr expr;
        
        template<typename... Args> FORCE_INLINE vector_expr_adapter(Args&&... args) : expr(std::forward<Args>(args)...) {}
        
        size_t _size() const { return expr.size(); }
        size_t _v_size() const { return expr.v_size(); }
        template<size_t Size> v_item_t<VExpr,Size> &vec(size_t n) {
            return expr.template vec<Size>(n);
        }
        template<size_t Size> v_item_t<VExpr,Size> vec(size_t n) const {
            return expr.template vec<Size>(n);
        }
    };
    template<typename T,size_t Size> struct _v_item_t<vector_expr_adapter<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    
    template<typename Op,typename... T> using vector_op_expr = vector_expr_adapter<v_op_expr<Op,T...> >;

    
    template<typename Store> struct v_axis;
    template<typename Store,size_t Size> struct _v_item_t<v_axis<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct v_axis : vector_expr<v_axis<Store> > {
        typedef typename Store::item_t item_t;
        static constexpr bool temporary = true;
        
        static const int v_score = 0;
        
        int _dimension;
        size_t axis;
        item_t length;
        
        size_t _size() const { return _dimension; }
        size_t _v_size() const { return Store::v_dimension(_dimension); }
        
        template<size_t Size> simd::v_type<item_t,Size> vec(size_t n) const {
            auto r = simd::v_type<item_t,Size>::zeros();
            if(n+Size > axis && n <= axis) r[axis-n] = length;
            return r;
        }

        v_axis(int d,int axis,item_t length) : _dimension(d), axis(axis), length(length) {}
    };

    template<typename Store> struct vector;
    template<typename Store,size_t Size> struct _v_item_t<vector<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    struct vector_item_count {
        static constexpr int get(int d) { return d; }
    };
    template<typename Store> struct vector : vector_expr_adapter<v_array<Store,typename Store::item_t> > {
        typedef vector_expr_adapter<v_array<Store,typename Store::item_t> > base_t;
        typedef typename Store::item_t item_t;

        explicit vector(int d) : base_t(d) {}
        template<typename F> vector(int d,F f) : base_t(d,f) {}    
        template<typename B> FORCE_INLINE vector(const vector_expr<B> &b) : base_t(b) {}

        template<typename B> FORCE_INLINE vector<Store> &operator+=(const vector_expr<B> &b) {
            this->expr += b;
            return *this;
        }
        template<typename B> FORCE_INLINE vector<Store> &operator-=(const vector_expr<B> &b) {
            this->expr -= b;
            return *this;
        }
        vector<Store> &operator*=(item_t b) {
            this->expr *= v_repeat<item_t>(dimension(),b);
            return *this;
        }
        vector<Store> &operator/=(item_t b) {
            this->expr *= v_repeat<item_t>(dimension(),1/b);
            return *this;
        }
        
        int dimension() const { return this->expr.size(); }
        
        template<typename T> void fill_with(T x) { this->expr.fill_with(x); }

        item_t &operator[](size_t n) { return this->expr[n]; }
        item_t operator[](size_t n) const { return this->expr[n]; }
        
        void normalize() { operator/=(this->absolute()); }
        
        static vector_expr_adapter<v_axis<Store> > axis(int d,int n,item_t length = 1) {
            return {d,n,length};
        }
    };
    
    
    template<typename T> using vector_multiply = vector_op_expr<op_multiply,T,v_repeat<s_item_t<T> > >;
    template<typename T> using vector_divide = vector_op_expr<op_divide,T,v_repeat<s_item_t<T> > >;
    template<typename T> using vector_rdivide = vector_op_expr<op_divide,v_repeat<s_item_t<T> >,T>;
    
    template<typename T> vector_multiply<T> operator*(s_item_t<T> a,const vector_expr<T> &b);
    template<typename T> vector_rdivide<T> operator/(s_item_t<T> a,const vector_expr<T> &b);
    
    template<typename T> struct vector_expr : private v_expr<T> {
        friend struct v_expr<T>;
        template<typename VExpr> friend struct vector_expr_adapter;
        template<typename Store> friend struct vector;
        friend vector_multiply<T> operator*<T>(s_item_t<T> a,const vector_expr<T> &b);
        friend vector_rdivide<T> operator/<T>(s_item_t<T> a,const vector_expr<T> &b);
        template<typename A,typename B> friend s_item_t<v_op_expr<op_multiply,A,B> > dot(const vector_expr<A> &a,const vector_expr<B> &b);
        
        operator T &() { return *static_cast<T*>(this); }
        operator const T &() const { return *static_cast<const T*>(this); }
        
        template<typename B> vector_op_expr<op_add,T,B> operator+(const vector_expr<B> &b) const {
            return {*this,b};
        }
        
        template<typename B> vector_op_expr<op_subtract,T,B> operator-(const vector_expr<B> &b) const {
            return {*this,b};
        }
        
        vector_op_expr<op_negate,T> operator-() const {
            return {*this};
        }
        
        vector_multiply<T> operator*(s_item_t<T> b) const {
            return {*this,v_repeat<s_item_t<T> >{this->size(),b}};
        }
        
        vector_multiply<T> operator/(s_item_t<T> b) const {
            return {*this,v_repeat<s_item_t<T> >{this->size(),1/b}};
        }

        template<typename B> bool operator==(const vector_expr<B> &b) const {
            return (*static_cast<const v_expr<T>*>(this) == b).all();
        }
        
        template<typename B> bool operator!=(const vector_expr<B> &b) const {
            return (*static_cast<const v_expr<T>*>(this) != b).any();
        }
        
        s_item_t<T> square() const { return dot(*this,*this); }
        s_item_t<T> absolute() const { return std::sqrt(square()); }
        vector_multiply<T> unit() const {
            return operator/(absolute());
        }
        
        template<typename F> vector_expr_adapter<v_apply<T,F> > apply(F f) const {
            return {*this,f};
        }
    };
    
    template<typename T> vector_multiply<T> operator*(s_item_t<T> a,const vector_expr<T> &b) {
        return {b,v_repeat<s_item_t<T> >{b.size(),a}};
    }
    template<typename T> vector_rdivide<T> operator/(s_item_t<T> a,const vector_expr<T> &b) {
        return {v_repeat<s_item_t<T> >{b.size(),a},b};
    }
    
    template<typename A,typename B> s_item_t<v_op_expr<op_multiply,A,B> > dot(const vector_expr<A> &a,const vector_expr<B> &b) {
        return (static_cast<const v_expr<A>&>(a) * b).reduce_add();
    }
}

using impl::dot;
using impl::vector;


template<class Store> struct matrix;

namespace impl {
    template<typename Store> struct matrix_row;
    template<typename Store,size_t Size> struct _v_item_t<matrix_row<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct matrix_row : vector_expr<matrix_row<Store> > {
        static const int v_score = V_SCORE_THRESHHOLD;
        static constexpr bool temporary = true;
        
        matrix<Store> &a;
        const size_t row;

        matrix_row<Store> &operator=(const matrix_row<Store>&) = delete;
        
        template<typename B> struct _v_assign {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score - 1;
            
            matrix<Store> &a;
            const size_t row;
            const B &b;
            
            template<size_t Size> void operator()(size_t n) {
                /* the Size > 1 check is not necessary, but it should subject
                   the first branch to dead-code elimination when Size is 1 */
                if(Size > 1 && a.dimension() % Size == 0) {
                    *reinterpret_cast<simd::v_type<typename Store::item_t,Size>*>(a.data() + row*a.dimension() + n) = b.template vec<Size>(n);
                } else {
                    auto data = b.template vec<Size>(n);
                    for(size_t i=0; i<(Size < size_t(a.dimension()) ? Size : size_t(a.dimension())); ++i) a.get(row,n+i) = data[i];
                }
            }
        };
        template<typename B> FORCE_INLINE matrix_row<Store> &operator=(const vector_expr<B> &b) {
            v_rep(a.dimension(),_v_assign<B>{a,row,b});
            
            return *this;
        }
        
        size_t _size() const { return a.dimension(); }
        size_t _v_size() const { return Store::v_dimension(a.dimension()); }
        
        typename Store::item_t &operator[](size_t n) const {
            assert(n >= 0 && n < a.dimension());
            return a.get(row,n);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(size_t n) const {
            /* the Size > 1 check is not necessary, but it should subject the
               first branch to dead-code elimination when Size is 1 */
            if(Size > 1 && a.dimension() % Size == 0) {
                return *reinterpret_cast<const simd::v_type<typename Store::item_t,Size>*>(a.data() + row*a.dimension() + n);
            } else {
                return simd::v_type<typename Store::item_t,Size>::loadu(a.data() + row*a.dimension() + n);
            }
        }
        
        operator typename Store::item_t*() const { return a.store.items + row * a.dimension(); }

        matrix_row(matrix<Store> &a,size_t row) : a(a), row(row) {}
    };
    
    template<typename Store> struct const_matrix_row;
    template<typename Store,size_t Size> struct _v_item_t<const_matrix_row<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct const_matrix_row : vector_expr<const_matrix_row<Store> > {
        static const int v_score = V_SCORE_THRESHHOLD;
        static constexpr bool temporary = true;
        
        const matrix<Store> &a;
        const size_t row;

        const_matrix_row<Store> &operator=(const const_matrix_row<Store>&) = delete;
        
        size_t _size() const { return a.dimension(); }
        size_t _v_size() const { return Store::v_dimension(a.dimension()); }
        
        typename Store::item_t operator[](size_t n) const {
            assert(n >= 0 && n < a.dimension());
            return a.get(row,n);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(size_t n) const {
            /* the Size > 1 check is not necessary, but it should subject the
               first branch to dead-code elimination when Size is 1 */
            if(Size > 1 && a.dimension() % Size == 0) {
                assert(n == 0);
                return *reinterpret_cast<const simd::v_type<typename Store::item_t,Size>*>(a.data() + row*a.dimension() + n);
            } else {
                return simd::v_type<typename Store::item_t,Size>::loadu(a.data() + row*a.dimension() + n);
            }
        }
        
        operator const typename Store::item_t*() const { return a.store.items + row * a.dimension(); }

        const_matrix_row(const matrix<Store> &a,size_t row) : a(a), row(row) {}
    };
    
    template<typename Store> struct matrix_column;
    template<typename Store,size_t Size> struct _v_item_t<matrix_column<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct matrix_column : vector_expr<matrix_column<Store> > {
        static const int v_score = 0;
        static constexpr bool temporary = true;
        
        matrix<Store> &a;
        const size_t col;

        matrix_column<Store> &operator=(const matrix_column<Store>&) = delete;
        
        template<typename B> struct _v_assign {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score - 1;
            
            matrix_column<Store> &self;
            const B &b;
            
            template<size_t Size> void operator()(size_t n) const {
                auto items = b.template vec<Size>(n);
                for(size_t i=0; i<Size; ++i) self.a.get(n+i,self.col) = items[i];
            }
        };
        template<typename B> matrix_column &operator=(const vector_expr<B> &b) {
            a.v_rep(_v_assign<B>{*this,b});
        }
        
        size_t _size() const { return a.dimension(); }
        size_t _v_size() const { return Store::v_dimension(a.dimension()); }
        
        typename Store::item_t &operator[](size_t n) const {
            assert(n >= 0 && n < a.dimension());
            return a.get(n,col);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> &vec(size_t n) const {
            simd::v_type<typename Store::item_t,Size> r;
            for(size_t i=0; i<Size; ++i) r[i] = a.get(n+1,col);
            return r;
        }

        matrix_column(matrix<Store> &a,size_t col) : a(a), col(col) {}
    };
    
    template<typename Store> struct const_matrix_column;
    template<typename Store,size_t Size> struct _v_item_t<const_matrix_column<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct const_matrix_column : vector_expr<const_matrix_column<Store> > {
        static const int v_score = 0;
        static constexpr bool temporary = true;
        
        const matrix<Store> &a;
        const size_t col;

        const_matrix_column<Store> &operator=(const const_matrix_column<Store>&) = delete;
        
        size_t _size() const { return a.dimension(); }
        size_t _v_size() const { return Store::v_dimension(a.dimension()); }
        
        typename Store::item_t operator[](size_t n) const {
            assert(n >= 0 && n < a.dimension());
            return a.get(n,col);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(size_t n) const {
            simd::v_type<typename Store::item_t,Size> r;
            for(size_t i=0; i<Size; ++i) r[i] = a.get(n+i,col);
            return r;
        }

        const_matrix_column(const matrix<Store> &a,size_t col) : a(a), col(col) {}
    };
    
    struct matrix_item_count {
        static constexpr int get(int d) { return d*d; }
    };
}

template<class Store> struct matrix {
    typedef typename Store::item_t item_t;
    
    explicit matrix(int d) : store(d) {}
    
    template<typename F> void rep(F f) const {
        for(int row=0; row<dimension(); ++row) {
            for(int col=0; col<dimension(); ++col) f(row,col);
        }
    }
    
    template<typename F> FORCE_INLINE void v_rep(F f) const {
        impl::v_rep(Store::v_dimension(dimension()),f);
    }
    
    void multiply(matrix<Store> &RESTRICT r,const matrix<Store> &b) const {
        assert(dimension() == r.dimension() && dimension() == b.dimension());

        rep([=,&b,&r](int row,int col){
            r[row][col] = dot((*this)[row],b.column(col));
        });
    }
    
    void multiply(vector<Store> &RESTRICT r,const vector<Store> &b) const {
        assert(dimension() == b.dimension());
        r.fill_with(0);
        rep([&,this](int row,int col){ r[row] += (*this)[row][col] * b[col]; });
    }
    
    void mult_transpose_(matrix<Store> &RESTRICT r,const matrix<Store> &b) const {
        assert(dimension() == r.dimension() && dimension() == b.dimension());

        rep([=,&b,&r](int row,int col){
            r[row][col] = dot((*this)[row],b[col]);
        });
    }
    
    /* given vector p, produces matrix r such that r * p is equal to:
       dot(p,a)*(a*(std::cos(theta)-1) - b*std::sin(theta)) + dot(p,b)*(b*(std::cos(theta)-1) + a*std::sin(theta)) + p */
    static void rotation_(matrix<Store> &r,const vector<Store> &a, const vector<Store> &b, item_t theta) {
        assert(r.dimension() == a.dimension() && r.dimension() == b.dimension());

        item_t c = std::cos(theta) - 1;
        item_t s = std::sin(theta);

        r.rep([&,c,s](int row,int col){
            item_t x = a[row]*(a[col]*c - b[col]*s) + b[row]*(b[col]*c + a[col]*s);
            if(col == row) ++x;
            
            r[row][col] = x;
        });
    }
    
    static void scale_(matrix<Store> &r,const vector<Store> &a) {
        assert(r.dimension() == a.dimension());
        r.rep([&](int row,int col){ r[row][col] = row == col ? a[row] : item_t(0); });
    }

    static void scale_(matrix<Store> &r,item_t a) {
        r.rep([&r,a](int row,int col){ r[row][col] = row == col ? a : item_t(0); });
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
       in the result. The return value indicates how many swaps were performed.
       If the return value is -1, the matrix is singular and the contents of
       "lu" will be undefined.
    */
    int decompose(matrix<Store> &RESTRICT lu,size_t *pivots) const {
        assert(dimension() == lu.dimension());
        
        int swapped = 0;

        for(int i=0; i<dimension(); ++i) pivots[i] = i;
        
        for(int j=0; j<dimension(); ++j) {
            for(int i=j; i<dimension(); ++i) {
                item_t sum = 0;
                for(int k=0; k<j; ++k) sum += lu[i][k] * lu[k][j];
                lu[i][j] = (*this)[pivots[i]][j] - sum;
            }
            
            if(lu[j][j] == 0) {
                for(int i=j+1; i<dimension(); ++i) {
                    if(lu[i][j] != 0) {
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
                item_t sum = 0;
                for(int k=0; k<j; ++k) sum += lu[j][k] * lu[k][i];
                lu[j][i] = ((*this)[pivots[j]][i] - sum) / lu[j][j];
            }
        }
        
        return swapped;
    }
    
    item_t determinant_(matrix<Store> &RESTRICT tmp) const {
        assert(dimension() == tmp.dimension());
        
        typename Store::template type<impl::vector_item_count,size_t> pivot(dimension());
        int swapped = decompose(tmp,pivot.items);
        if(swapped < 0) return 0;
        
        item_t r = swapped % 2 ? -1 : 1;
        for(int i=0; i<dimension(); ++i) r *= tmp[i][i];
        return r;
    }
    
    void inverse_(matrix<Store> &RESTRICT inv,matrix<Store> &RESTRICT tmp) const {
        assert(dimension() == r.dimension() && dimension() == tmp.dimension());

        typename Store::template type<impl::vector_item_count,size_t> pivot(dimension());
        int swapped = decompose(tmp,pivot.items);
        if(swapped < 0) throw std::domain_error("matrix is singular (uninvertible)");

        // forward substitution
        // store the result in the lower triangle of tmp
        for(int c=0; c<dimension(); ++c) {
            tmp[c][c] = item_t(1) / tmp[c][c];
            
            for(int r=c+1; r<dimension(); ++r) {
                item_t sum = 0;
                for(int i=c; i<r; ++i) sum -= tmp[r][i] * tmp[i][c];
                tmp[r][c] = sum / tmp[r][r];
            }
        }

        // back substitution
        for(int c=0; c<dimension(); ++c) {
            int pc = pivot.items[c];
            inv[dimension()-1][pc] = tmp[dimension()-1][c];
            
            for(int r=dimension()-2; r>-1; --r) {
                item_t sum = 0;
                if(r >= c) sum = tmp[r][c];
                for(int i=r+1; i<dimension(); ++i) sum -= tmp[r][i] * inv[i][pc];
                inv[r][pc] = sum;
            }
        }
    }
    
    void transpose_(matrix<Store> &RESTRICT t) const {
        rep([&,this](int r,int c){ t[r][c] = (*this)[c][r]; });
    }

    matrix<Store> operator*(const matrix<Store> &b) const {
        matrix<Store> r(dimension());
        multiply(r,b);
        return r;
    }
    
    vector<Store> operator*(const vector<Store> &b) const {
        vector<Store> r(dimension());
        multiply(r,b);
        return r;
    }
    
    matrix<Store> mult_transpose(const matrix<Store> &b) const {
        matrix<Store> r(dimension());
        mult_transpose_(r,b);
        return r;
    }

    static matrix<Store> rotation(const vector<Store> &a,const vector<Store> &b,item_t theta) {
        matrix<Store> r(a.dimension());
        rotation_(r,a,b,theta);
        return r;
    }
    
    static matrix<Store> scale(const vector<Store> &a) {
        matrix<Store> r(a.dimension());
        scale_(r,a);
        return r;
    }

    static matrix<Store> scale(int d,item_t a) {
        matrix<Store> r(d);
        scale_(r,a);
        return r;
    }

    static matrix<Store> identity(int d) {
        return scale(d,1);
    }
    
    matrix<Store> transpose() const {
        matrix<Store> r(dimension());
        transpose_(r);
        return r;
    }
    
    matrix<Store> inverse() const {
        matrix<Store> r(dimension()), tmp(dimension());
        inverse_(r,tmp);
        return r;
    }
    
    item_t determinant() const {
        matrix<Store> tmp(dimension());
        return determinant_(tmp);
    }
    
    /* Calculates the determinant by using itself to store the intermediate
       calculations. This avoids allocating space for another matrix but loses
       the original contents of this matrix. */
    item_t determinant_inplace() {
        int swapped = 0;

        for(int j=0; j<dimension(); ++j) {
            for(int i=j; i<dimension(); ++i) {
                item_t sum = 0;
                for(int k=0; k<j; ++k) sum += (*this)[i][k] * (*this)[k][j];
                (*this)[i][j] = (*this)[i][j] - sum;
            }
            
            if((*this)[j][j] == 0) {
                for(int i=j+1; i<dimension(); ++i) {
                    if((*this)[i][j] != 0) {
                        ++swapped;
                        for(int k=0; k<dimension(); ++k) std::swap((*this)[i][k],(*this)[j][k]);
                        goto okay;
                    }
                }
                return 0;
            }
        
        okay:
            
            for(int i=j+1; i<dimension(); ++i) {
                item_t sum = 0;
                for(int k=0; k<j; ++k) sum += (*this)[j][k] * (*this)[k][i];
                (*this)[j][i] = ((*this)[j][i] - sum) / (*this)[j][j];
            }
        }

        item_t r = swapped % 2 ? -1 : 1;
        for(int i=0; i<dimension(); ++i) r *= (*this)[i][i];
        return r;
    }
    
    impl::matrix_row<Store> operator[](size_t n) { return {*this,n}; }
    impl::const_matrix_row<Store> operator[](size_t n) const { return {*this,n}; }
    
    item_t *data() { return store.items; }
    const item_t *data() const { return store.items; }
    
    item_t &get(int r,int c) {
        return store.items[r*dimension() + c];
    }
    item_t get(int r,int c) const {
        return store.items[r*dimension() + c];
    }
    
    impl::matrix_column<Store> column(size_t n) { return {*this,n}; }
    impl::const_matrix_column<Store> column(size_t n) const { return {*this,n}; }

    int dimension() const { return store.dimension(); }

    typename Store::template type<impl::matrix_item_count> store;
};


template<typename T> struct smaller_store {
    typedef T type;
};

template<typename T> struct _smaller;
template<typename Store> struct _smaller<matrix<Store> > {
    typedef matrix<typename smaller_store<Store>::type> type;
};

template<typename T> using smaller = typename _smaller<T>::type;


// generalized cross product
template<typename Store> void cross_(vector<Store> &r,smaller<matrix<Store> > &tmp,const vector<Store> *vs) {
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

// generalized cross product
template<typename Store> vector<Store> cross(const vector<Store> *vs) {
    assert(vs);
    
    vector<Store> r(vs[0].dimension());
    smaller<matrix<Store> > tmp(vs[0].dimension()-1);
    cross_(r,tmp,vs);
    
    return r;
}

#endif
