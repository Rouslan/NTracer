#ifndef geometry_hpp
#define geometry_hpp

#include <cmath>
#include <utility>
#include <stdexcept>

#include "simd.hpp"


using std::declval;

typedef float real;


namespace impl {
    /* This is part of a very simple heuristic for determining if something is
       worth vectorizing. A score greater than or equal to this value means yes.
     */
    const int V_SCORE_THRESHHOLD = 2;
    
    template<typename T> struct vector_expr;
    
    template<typename,size_t> struct _v_item_t {};
    template<typename T,size_t Size> using v_item_t = typename _v_item_t<T,Size>::type;
    template<typename T> using s_item_t = typename _v_item_t<T,1>::type::item_t;
    
    
    template<typename F> inline void rep(int n,F f) {
        for(int i=0; i<n; ++i) f(i);
    }
    
    template<typename F> using v_sizes = simd::v_sizes<s_item_t<F> >;
    
    template<typename F,size_t SI,bool more=(F::v_score >= V_SCORE_THRESHHOLD) && (simd::v_sizes<typename F::item_t>::value[SI] > 1)> struct _vec_rep {
        static inline void go(size_t n,size_t i,F f) {
            static const size_t size = simd::v_sizes<typename F::item_t>::value[SI];
            for(; i<(n - (size - 1)); i+= size) f.template operator()<size>(i);
            
            _vec_rep<F,SI+1>::go(n,i,f);
        }
    };
    
    template<typename F,size_t I> struct _vec_rep<F,I,false> {
        static inline void go(size_t n,size_t i,F f) {
            for(; i<n; ++i) f.template operator()<1>(i);
        }
    };
    
    template<typename F> inline void vec_rep(size_t n,F f) {
        _vec_rep<F,0>::go(n,0,f);
    }
    
    
    template<typename A,typename B> struct vector_sum;
    template<typename A,typename B,size_t Size> struct _v_item_t<vector_sum<A,B>,Size> {
        typedef decltype(declval<v_item_t<A,Size> >() + declval<v_item_t<B,Size> >()) type;
    };
    template<typename A,typename B> struct vector_sum : vector_expr<vector_sum<A,B> > {
        template<typename U> friend struct vector_expr;
        
        static const int v_score = A::v_score + B::v_score;
        
        const A &a;
        const B &b;
        
        vector_sum(const vector_sum<A,B>&) = delete;
        vector_sum &operator=(const vector_sum<A,B>&) = delete;

        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<vector_sum<A,B>,Size> vec(int n) const {
            return a.template vec<Size>(n) + b.template vec<Size>(n);
        }
        
    private:
        vector_sum(const A &a,const B &b) : a(a), b(b) {}
    };
    
    template<typename A,typename B> struct vector_diff;
    template<typename A,typename B,size_t Size> struct _v_item_t<vector_diff<A,B>,Size> {
        typedef decltype(declval<v_item_t<A,Size> >() - declval<v_item_t<B,Size> >()) type;
    };
    template<typename A,typename B> struct vector_diff : vector_expr<vector_diff<A,B> > {
        template<typename U> friend struct vector_expr;
        
        static const int v_score = A::v_score + B::v_score;
        
        const A &a;
        const B &b;
        
        vector_diff(const vector_diff<A,B>&) = delete;
        vector_diff &operator=(const vector_diff<A,B>&) = delete;

        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<vector_diff<A,B>,Size> vec(int n) const {
            return a.template vec<Size>(n) - b.template vec<Size>(n);
        }
        
    private:
        vector_diff(const A &a,const B &b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_neg;
    template<typename T,size_t Size> struct _v_item_t<vector_neg<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    template<typename T> struct vector_neg : vector_expr<vector_neg<T> > {
        template<typename U> friend struct vector_expr;
        
        static const int v_score = T::v_score;
        
        const T &a;

        vector_neg(const vector_neg<T>&) = delete;
        vector_neg &operator=(const vector_neg<T>&) = delete;

        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<T,Size> vec(int n) const {
            return -a.template vec<Size>(n);
        }
        
    private:
        vector_neg(const T &a) : a(a) {}
    };
    
    template<typename T> struct vector_product;
    template<typename T,size_t Size> struct _v_item_t<vector_product<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    template<typename T> struct vector_product : vector_expr<vector_product<T> > {
        template<typename U> friend struct vector_expr;
        template<typename U> friend vector_product<U> operator*(s_item_t<U> a,const vector_expr<U> &b);
        
        static const int v_score = T::v_score + 1;
        
        const T &a;
        s_item_t<T> b;
        
        vector_product(const vector_product<T>&) = delete;
        vector_product &operator=(const vector_product<T>&) = delete;
        
        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<T,Size> vec(int n) const {
            return a.template vec<Size>(n) * b;
        }
        
    private:
        vector_product(const T &a,s_item_t<T> b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_quotient;
    template<typename T,size_t Size> struct _v_item_t<vector_quotient<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    template<typename T> struct vector_quotient : vector_expr<vector_quotient<T> > {
        template<typename U> friend struct vector_expr;
        
        static const int v_score = T::v_score + 1;
        
        const T &a;
        s_item_t<T> b;
        
        vector_quotient(const vector_quotient<T>&) = delete;
        vector_quotient &operator=(const vector_quotient<T>&) = delete;
        
        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<T,Size> vec(int n) const {
            return a.template vec<Size>(n) / b;
        }
        
    private:
        vector_quotient(const T &a,s_item_t<T> b) : a(a), b(b) {}
    };
    
    template<typename T> struct vector_rquotient;
    template<typename T,size_t Size> struct _v_item_t<vector_rquotient<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    template<typename T> struct vector_rquotient : vector_expr<vector_rquotient<T> > {
        template<typename U> friend struct vector_expr;
        template<typename U> friend vector_product<U> operator/(s_item_t<U> a,const vector_expr<U> &b);
        
        static const int v_score = T::v_score + 1;
        
        s_item_t<T> a;
        const T &b;

        vector_rquotient(const vector_rquotient<T>&) = delete;
        vector_rquotient &operator=(const vector_rquotient<T>&) = delete;
        
        int dimension() const { return b.dimension(); }
        int v_dimension() const { return b.v_dimension(); }
        
        template<size_t Size> v_item_t<T,Size> vec(int n) const {
            return v_item_t<T,Size>::repeat(a) / b.template vec<Size>(n); }
        
    private:
        vector_rquotient(s_item_t<T> a,const T &b) : a(a), b(b) {}
    };
    
    template<typename T,typename F> struct vector_apply;
    template<typename T,typename F,size_t Size> struct _v_item_t<vector_apply<T,F>,Size> {
        typedef simd::v_type<typename std::result_of<F(s_item_t<T>)>::type,Size> type;
    };
    template<typename T,typename F> struct vector_apply : vector_expr<vector_apply<T,F> > {
        template<typename U> friend struct vector_expr;
        
        static const int v_score = T::v_score - 1;

        const T &a;
        F f;

        vector_apply(const vector_apply<T,F>&) = delete;
        vector_apply &operator=(const vector_apply<T,F>&) = delete;
        
        int dimension() const { return a.dimension(); }
        int v_dimension() const { return a.v_dimension(); }
        
        template<size_t Size> v_item_t<vector_apply<T,F>,Size> vec(int n) const {
            return a.template vec<Size>(n).apply(f);
        }
        
    private:
        vector_apply(const T &a,F f) : a(a), f(f) {}
    };
    
    template<typename Store> struct vector_axis;
    template<typename Store,size_t Size> struct _v_item_t<vector_axis<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<typename Store> struct vector_axis : vector_expr<vector_axis<Store> > {
        typedef typename Store::item_t item_t;
        
        static const int v_score = 0;
        
        int _dimension;
        int axis;
        item_t length;
        
        int dimension() const { return _dimension; }
        int v_dimension() const { return Store::v_dimension(_dimension); }
        
        template<size_t Size> simd::v_type<item_t,Size> vec(int n) const {
            auto r = simd::v_type<item_t,Size>::zeros();
            if(n >= axis && n < axis+int(Size)) r[n-axis] = length;
            return r;
        }

        vector_axis(int d,int axis,item_t length) : _dimension(d), axis(axis), length(length) {}
    };

    template<typename Store> struct vector;
    template<typename Store,size_t Size> struct _v_item_t<vector<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    struct vector_item_count {
        static constexpr int get(int d) { return d; }
    };
    template<typename Store> struct vector : vector_expr<vector<Store> > {
        typedef typename Store::item_t item_t;
        
        static const int v_score = V_SCORE_THRESHHOLD;

        explicit vector(int d) : store(d) {}
    
        template<typename F> vector(int d,F f) : store(d) {
            fill_with(f);
        }
        
        template<typename B> vector(const vector_expr<B> &b) : store(b.dimension()) {
            fill_with(b);
        }
        
        template<typename B> struct _v_add {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score;
            
            vector<Store> *self;
            const B &b;
            
            template<size_t Size> void operator()(int n) const {
                self->template vec<Size>(n) += b.template vec<Size>(n);
            }
        };
        template<typename B> vector<Store> &operator+=(const vector_expr<B> &b) {
            vec_rep(v_dimension(),_v_add<B>{this,b});
            
            return *this;
        }
        
        template<typename B> struct _v_sub {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score;
            
            vector<Store> *self;
            const B &b;
            
            template<size_t Size> void operator()(int n) const {
                self->template vec<Size>(n) -= b.template vec<Size>(n);
            }
        };
        template<typename B> vector<Store> &operator-=(const vector_expr<B> &b) {
            vec_rep(v_dimension(),_v_sub<B>{this,b});

            return *this;
        }
        
        struct _v_mul {
            typedef typename Store::item_t item_t;
            static const int v_score = 2;
            
            vector<Store> *self;
            item_t b;
            
            template<size_t Size> void operator()(int n) const {
                self->template vec<Size>(n) *= b;
            }
        };
        vector<Store> &operator*=(item_t b) {
            vec_rep(v_dimension(),_v_mul{this,b});
        
            return *this;
        }
        
        struct _v_div {
            typedef typename Store::item_t item_t;
            static const int v_score = 2;
            
            vector<Store> *self;
            item_t b;
            
            template<size_t Size> void operator()(int n) const {
                self->template vec<Size>(n) /= b;
            }
        };
        vector<Store> &operator/=(item_t b) {
            vec_rep(v_dimension(),_v_div{this,b});
            
            return *this;
        }
        
        int dimension() const { return store.dimension(); }
        int v_dimension() const { return Store::v_dimension(dimension()); }
        
        template<typename F> void rep(F f) const {
            impl::rep(dimension(),f);
        }
        
        void fill_with(item_t v) {
            rep([=](int i){ (*this)[i] = v; });
        }
        
        template<typename B> struct _v_assign {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score;
            
            vector<Store> *self;
            const B &b;
            
            template<size_t Size> void operator()(int n) const {
                self->template vec<Size>(n) = b.template vec<Size>(n);
            }
        };
        template<typename B> void fill_with(const vector_expr<B> &b) {
            vec_rep(v_dimension(),_v_assign<B>{this,b});
        }
        
        template<typename F,typename=decltype(declval<F>()(declval<int>()))> void fill_with(F f) {
            rep([=](int i){ (*this)[i] = f(i); });
        }
        
        item_t &operator[](int n) { return store.items[n]; }
        item_t operator[](int n) const { return store.items[n]; }
        
        template<size_t Size> simd::v_type<item_t,Size> &vec(int n) {
            return *reinterpret_cast<simd::v_type<item_t,Size>*>(store.items + n);
        }
        
        template<size_t Size> simd::v_type<item_t,Size> vec(int n) const {
            return *reinterpret_cast<const simd::v_type<item_t,Size>*>(store.items + n);
        }
        
        void normalize() { operator/(this->absolute()); }
        
        static vector_axis<Store> axis(int d,int n,item_t length = item_t(1)) {
            return {d,n,length};
        }
        
        typename Store::template type<vector_item_count> store;
    };

    template<typename A,typename B> struct _v_mul_sum {
        typedef decltype(declval<v_item_t<A,v_sizes<A>::value[0]> >() * declval<v_item_t<B,v_sizes<B>::value[0]> >()) v_sum_t;
        typedef typename v_sum_t::item_t item_t;
        
        static const int v_score = A::v_score + B::v_score - 1;
        
        v_sum_t &r;
        item_t &r_small;
        const A &a;
        const B &b;
        
        template<size_t Size> void operator()(int n) const {
            if(Size == 1) {
                r_small += (a.template vec<1>(n) * b.template vec<1>(n))[0];
            } else {
                /* If the AVX instruction set is used, this relies on all vector
                   operations using the AVX versions, where the upper bits are
                   set to zero. */
                r += v_sum_t(a.template vec<Size>(n) * b.template vec<Size>(n));
            }
        }
        
        static constexpr size_t smallest_vec(int i=0) {
            typedef simd::v_sizes<item_t> sizes;
            return sizes::value[i] == 1 || sizes::value[i+1] == 1 ? sizes::value[i] : smallest_vec(i+1);
        }
    };
    template<typename A,typename B> static typename _v_mul_sum<A,B>::item_t dot(const vector_expr<A> &a,const vector_expr<B> &b) {
        assert(a.dimension() == b.dimension());
        
        typename _v_mul_sum<A,B>::item_t r_small = 0;
        if(_v_mul_sum<A,B>::v_score < V_SCORE_THRESHHOLD || size_t(a.dimension()) < _v_mul_sum<A,B>::smallest_vec()) {
            for(int i=0; i<a.dimension(); ++i) r_small += (static_cast<const A&>(a).template vec<1>(i) * static_cast<const B&>(b).template vec<1>(i))[0];
            return r_small;
        }

        auto r = _v_mul_sum<A,B>::v_sum_t::zeros();
        /* v_dimension is not used because we don't want the sum to include the
           pad value */
        vec_rep(a.dimension(),_v_mul_sum<A,B>{r,r_small,a,b});
        return r.reduce_add() + r_small;
    }
    
    template<typename T> struct vector_expr {
        int dimension() const { return static_cast<const T*>(this)->dimension(); }
        int v_dimension() const { return static_cast<const T*>(this)->v_dimension(); }
        
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
        
        vector_product<T> operator*(s_item_t<T> b) const {
            return {*this,b};
        }
        
        vector_quotient<T> operator/(s_item_t<T> b) const {
            return {*this,b};
        }
        
        template<typename B> struct _v_eq {
            typedef s_item_t<T> item_t;
            static const int v_score = T::v_score + B::v_score;
            
            int &r;
            const T &a;
            const B &b;
            
            template<size_t Size> void operator()(int n) const {
                r |= (a.template vec<Size>(n) != b.template vec<Size>(n)).to_bits();
            }
        };
        template<typename B> bool operator==(const vector_expr<B> &b) const {
            assert(dimension() == b.dimension());

            int r = 0;
            
            vec_rep(v_dimension(),_v_eq<B>{r,*this,b});
            return r == 0;
        }
        
        template<typename B> bool operator!=(const vector_expr<B> &b) const {
            return !operator==(b);
        }
        
        s_item_t<T> square() const { return dot(*this,*this); }
        s_item_t<T> absolute() const { return std::sqrt(square()); }
        vector_quotient<T> unit() const {
            return {*this,absolute()};
        }
        
        template<typename F> vector_apply<T,F> apply(F f) const {
            return {*this,f};
        }
    };
    
    template<typename T> vector_product<T> operator*(s_item_t<T> a,const vector_expr<T> &b) {
        return {b,a};
    }
    
    template<typename T> vector_rquotient<T> operator/(s_item_t<T> a,const vector_expr<T> &b) {
        return {a,b};
    }
}

using impl::dot;
using impl::vector;


template<class Store> struct matrix;

namespace impl {
    template<class Store> struct matrix_row;
    template<class Store,size_t Size> struct _v_item_t<matrix_row<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<class Store> struct matrix_row : vector_expr<matrix_row<Store> > {
        friend struct matrix<Store>;
        
        static const int v_score = V_SCORE_THRESHHOLD;
        
        matrix<Store> &a;
        const int row;

        matrix_row<Store> &operator=(const matrix_row<Store>&) = delete;
        
        template<typename B> struct _v_assign {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score;
            
            matrix<Store> &a;
            const int row;
            const B &b;
            
            template<size_t Size> void operator()(int n) {
                /* the Size > 1 check is not necessary, but it should subject
                   the first branch to dead-code elimination when Size is 1 */
                if(Size > 1 && a.dimension() % Size == 0) {
                    *reinterpret_cast<simd::v_type<typename Store::item_t,Size>*>(a.data() + row*a.dimension() + n) = b.template vec<Size>(n);
                } else {
                    b.template vec<Size>(n).storeu(a.data() + row*a.dimension() + n);
                }
            }
        };
        template<typename B> matrix_row &operator=(const vector_expr<B> &b) {
            a.vec_rep(_v_assign<B>{a,row,b});
            
            return *this;
        }
        
        int dimension() const { return a.dimension(); }
        
        typename Store::item_t &operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.get(row,n);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(int n) const {
            /* the Size > 1 check is not necessary, but it should subject the
               first branch to dead-code elimination when Size is 1 */
            if(Size > 1 && dimension() % Size == 0) {
                return *reinterpret_cast<const simd::v_type<typename Store::item_t,Size>*>(a.data() + row*dimension() + n);
            } else {
                return simd::v_type<typename Store::item_t,Size>::loadu(a.data() + row*dimension() + n);
            }
        }
        
        operator typename Store::item_t*() const { return a.store.items + row * dimension(); }
        
    private:
        matrix_row(matrix<Store> &a,int row) : a(a), row(row) {}
    };
    
    template<class Store> struct const_matrix_row;
    template<class Store,size_t Size> struct _v_item_t<const_matrix_row<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<class Store> struct const_matrix_row : vector_expr<const_matrix_row<Store> > {
        friend struct matrix<Store>;
        
        static const int v_score = V_SCORE_THRESHHOLD;
        
        const matrix<Store> &a;
        const int row;

        const_matrix_row<Store> &operator=(const const_matrix_row<Store>&) = delete;
        
        int dimension() const { return a.dimension(); }
        
        typename Store::item_t operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.get(row,n);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(int n) const {
            /* the Size > 1 check is not necessary, but it should subject the
               first branch to dead-code elimination when Size is 1 */
            if(Size > 1 && dimension() % Size == 0) {
                assert(n == 0);
                return *reinterpret_cast<const simd::v_type<typename Store::item_t,Size>*>(a.data() + row*dimension() + n);
            } else {
                return simd::v_type<typename Store::item_t,Size>::loadu(a.data() + row*dimension() + n);
            }
        }
        
        operator const typename Store::item_t*() const { return a.store.items + row * dimension(); }
        
    private:
        const_matrix_row(const matrix<Store> &a,int row) : a(a), row(row) {}
    };
    
    template<class Store> struct matrix_column;
    template<class Store,size_t Size> struct _v_item_t<matrix_column<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<class Store> struct matrix_column : vector_expr<matrix_column<Store> > {
        friend struct matrix<Store>;
        
        static const int v_score = 0;
        
        matrix<Store> &a;
        const int col;

        matrix_column<Store> &operator=(const matrix_column<Store>&) = delete;
        
        template<typename B> struct _v_assign {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score - 1;
            
            matrix_column<Store> *self;
            const B &b;
            
            template<size_t Size> void operator()(int n) const {
                auto items = b.template vec<Size>(n);
                for(size_t i=0; i<Size; ++i) self->a.get(n+i,self->col) = items[i];
            }
        };
        template<typename B> matrix_column &operator=(const vector_expr<B> &b) {
            a.vec_rep(_v_assign<B>{this,b});
        }
        
        int dimension() const { return a.dimension(); }
        typename Store::item_t &operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.get(n,col);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> &vec(int n) const {
            simd::v_type<typename Store::item_t,Size> r;
            for(size_t i=0; i<Size; ++i) r[i] = a.get(n+1,col);
            return r;
        }
        
    private:
        matrix_column(matrix<Store> &a,int col) : a(a), col(col) {}
    };
    
    template<class Store> struct const_matrix_column;
    template<class Store,size_t Size> struct _v_item_t<const_matrix_column<Store>,Size> {
        typedef simd::v_type<typename Store::item_t,Size> type;
    };
    template<class Store> struct const_matrix_column : vector_expr<const_matrix_column<Store> > {
        friend struct matrix<Store>;
        
        static const int v_score = 0;
        
        const matrix<Store> &a;
        const int col;

        const_matrix_column<Store> &operator=(const const_matrix_column<Store>&) = delete;
        
        int dimension() const { return a.dimension(); }
        typename Store::item_t operator[](int n) const {
            assert(n >= 0 && n < dimension());
            return a.get(n,col);
        }
        
        template<size_t Size> simd::v_type<typename Store::item_t,Size> vec(int n) const {
            simd::v_type<typename Store::item_t,Size> r;
            for(size_t i=0; i<Size; ++i) r[i] = a.get(n+i,col);
            return r;
        }
        
    private:
        const_matrix_column(const matrix<Store> &a,int col) : a(a), col(col) {}
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
    
    template<typename F> void vec_rep(F f) const {
        impl::vec_rep(Store::v_dimension(dimension()),f);
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
    int decompose(matrix<Store> &RESTRICT lu,int *pivots) const {
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
        
        typename Store::template type<impl::vector_item_count,int> pivot(dimension());
        int swapped = decompose(tmp,pivot.items);
        if(swapped < 0) return 0;
        
        item_t r = swapped % 2 ? -1 : 1;
        for(int i=0; i<dimension(); ++i) r *= tmp[i][i];
        return r;
    }
    
    void inverse_(matrix<Store> &RESTRICT inv,matrix<Store> &RESTRICT tmp) const {
        assert(dimension() == r.dimension() && dimension() == tmp.dimension());

        typename Store::template type<impl::vector_item_count,int> pivot(dimension());
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
    
    impl::matrix_row<Store> operator[](int n) { return {*this,n}; }
    impl::const_matrix_row<Store> operator[](int n) const { return {*this,n}; }
    
    item_t *data() { return store.items; }
    const item_t *data() const { return store.items; }
    
    item_t &get(int r,int c) {
        return store.items[r*dimension() + c];
    }
    item_t get(int r,int c) const {
        return store.items[r*dimension() + c];
    }
    
    impl::matrix_column<Store> column(int n) { return {*this,n}; }
    impl::const_matrix_column<Store> column(int n) const { return {*this,n}; }

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
