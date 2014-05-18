#ifndef v_array_hpp
#define v_array_hpp

#include <tuple>
#include <utility>

#include "simd.hpp"
#include "index_list.hpp"


// this only applies to dividing by a scalar
// Enabling this seems to causes problems
//#define MULT_RECIPROCAL_INSTEAD_OF_DIV

namespace impl {
    /* This is part of a very simple heuristic for determining if something is
       worth vectorizing. A score greater than or equal to this value means yes.
     */
    const int V_SCORE_THRESHHOLD = 2;
    //const int V_SCORE_THRESHHOLD = -100;
    
    const size_t VSIZE_MAX = std::numeric_limits<size_t>::max();
    
    
    template<typename T> struct v_expr;
    
    template<typename,size_t> struct _v_item_t {};
    template<typename T,size_t Size> using v_item_t = typename _v_item_t<T,Size>::type;
    template<typename T> using s_item_t = typename _v_item_t<T,1>::type::item_t;

    
    template<typename F> using v_sizes = simd::v_sizes<s_item_t<F> >;
    
    template<typename F,size_t SI,bool more=(F::v_score >= V_SCORE_THRESHHOLD) && (simd::v_sizes<typename F::item_t>::value[SI] > 1)> struct _v_rep {
        static FORCE_INLINE void go(size_t n,size_t i,F f) {
            static const size_t size = simd::v_sizes<typename F::item_t>::value[SI];
            for(; i<(n - (size - 1)); i+= size) f.template operator()<size>(i);
            
            _v_rep<F,SI+1>::go(n,i,f);
        }
    };
    
    template<typename F,size_t SI> struct _v_rep<F,SI,false> {
        static FORCE_INLINE void go(size_t n,size_t i,F f) {
            for(; i<n; ++i) f.template operator()<1>(i);
        }
    };
    
    template<typename F> FORCE_INLINE void v_rep(size_t n,F f) {
        assert(n != VSIZE_MAX);
        _v_rep<F,0>::go(n,0,f);
    }
    
    
    template<typename F,size_t SI,bool more=(F::v_score >= V_SCORE_THRESHHOLD) && (simd::v_sizes<typename F::item_t>::value[SI] > 1)> struct _v_rep_until {
        static FORCE_INLINE bool go(size_t n,size_t i,F f) {
            static const size_t size = simd::v_sizes<typename F::item_t>::value[SI];
            for(; i<(n - (size - 1)); i+= size) {
                if(f.template operator()<size>(i)) return true;
            }
            
            return _v_rep_until<F,SI+1>::go(n,i,f);
        }
    };
    
    template<typename F,size_t SI> struct _v_rep_until<F,SI,false> {
        static FORCE_INLINE bool go(size_t n,size_t i,F f) {
            for(; i<n; ++i) {
                if(f.template operator()<1>(i)) return true;
            }
            
            return false;
        }
    };
    
    template<typename F> FORCE_INLINE bool v_rep_until(size_t n,F f) {
        assert(n != VSIZE_MAX);
        return _v_rep_until<F,0>::go(n,0,f);
    }
    
    
    template<typename T> using v_expr_store = typename std::conditional<T::temporary,T,const T&>::type;
    

    struct op_add {
        template<typename A,typename B> static auto op(A a,B b) -> decltype(a + b) {
            return a + b;
        }
        template<typename A,typename B> static auto assign(A &a,B b) -> decltype(a += b) {
            return a += b;
        }
        template<typename T> static auto reduce(T x) -> decltype(x.reduce_add()) {
            return x.reduce_add();
        }
        template<typename T> static constexpr T first() {
            return T::zeros();
        }
    };

#define BINARY_EQ_OP(NAME,OP) struct NAME { \
        template<typename A,typename B> static auto op(A a,B b) -> decltype(a OP b) { \
            return a OP b; \
        } \
        template<typename A,typename B> static auto assign(A &a,B b) -> decltype(a OP##= b) { \
            return a OP##= b; \
        } \
    }
#define BINARY_OP(NAME,OP) struct NAME { \
        template<typename A,typename B> static auto op(A a,B b) -> decltype(a OP b) { \
            return a OP b; \
        } \
    }
#define BINARY_FUNC(NAME,F) struct NAME { \
        template<typename A,typename B> static auto op(A a,B b) -> decltype(F(a,b)) { \
            return F(a,b); \
        } \
    }
#define UNARY_OP(NAME,OP) struct NAME { \
        template<typename T> static auto op(T a) -> decltype(OP a) { \
            return OP a; \
        } \
    }
#define UNARY_FUNC(NAME,F) struct NAME { \
        template<typename T> static auto op(T a) -> decltype(a.F()) { \
            return a.F(); \
        } \
    }
#define NCMP_FUNC(NAME,F,INT_OP) struct NAME { \
        template<typename A,typename=typename std::enable_if<std::is_floating_point<typename A::item_t>::value>::type> static auto op(A a,A b) -> decltype(F(a,b)) { \
            return F(a,b); \
        } \
        template<typename A,typename=typename std::enable_if<!std::is_floating_point<typename A::item_t>::value>::type> static auto op(A a,A b) -> decltype(a INT_OP b) { \
            return a INT_OP b; \
        } \
    }
    
    BINARY_EQ_OP(op_subtract,-);
    BINARY_EQ_OP(op_multiply,*);
    BINARY_EQ_OP(op_divide,/);
    BINARY_EQ_OP(op_and,&);
    BINARY_EQ_OP(op_or,|);
    BINARY_EQ_OP(op_xor,^);
    BINARY_OP(op_eq,==);
    BINARY_OP(op_neq,!=);
    BINARY_OP(op_gt,>);
    BINARY_OP(op_ge,>=);
    BINARY_OP(op_lt,<);
    BINARY_OP(op_le,<=);
    NCMP_FUNC(op_ngt,simd::cmp_ngt,<=);
    NCMP_FUNC(op_nge,simd::cmp_nge,<);
    NCMP_FUNC(op_nlt,simd::cmp_nlt,>=);
    NCMP_FUNC(op_nle,simd::cmp_nle,>);
    BINARY_OP(op_l_and,&&);
    BINARY_OP(op_l_or,||);
    UNARY_OP(op_l_not,!);
    UNARY_OP(op_negate,-);
    UNARY_FUNC(op_abs,abs);
    BINARY_FUNC(op_l_andn,simd::l_andn);
    BINARY_FUNC(op_l_xor,simd::l_xor);
    BINARY_FUNC(op_l_xnor,simd::l_xnor);
    
#undef BINARY_EQ_OP
#undef BINARY_OP
#undef BINARY_FUNC
#undef UNARY_OP
#undef UNARY_FUNC
#undef NCMP_FUNC
    
    struct op_max {
        template<typename T> static T op(T a,T b) {
            return simd::max(a,b);
        }
        template<typename T> static typename T::item_t reduce(T x) {
            return x.reduce_max();
        }
        template<typename T> static constexpr T first() {
            return T::repeat(std::numeric_limits<typename T::item_t>::lowest());
        }
    };
    
    struct op_min {
        template<typename T> static T op(T a,T b) {
            return simd::min(a,b);
        }
        template<typename T> static typename T::item_t reduce(T x) {
            return x.reduce_min();
        }
        template<typename T> static constexpr T first() {
            return T::repeat(std::numeric_limits<typename T::item_t>::max());
        }
    };
    

    template<typename T> struct inverse {};
    template<> struct inverse<op_eq> { typedef op_neq type; };
    template<> struct inverse<op_neq> { typedef op_eq type; };
    template<> struct inverse<op_gt> { typedef op_ngt type; };
    template<> struct inverse<op_ge> { typedef op_nge type; };
    template<> struct inverse<op_lt> { typedef op_nlt type; };
    template<> struct inverse<op_le> { typedef op_nle type; };
    template<> struct inverse<op_ngt> { typedef op_gt type; };
    template<> struct inverse<op_nge> { typedef op_ge type; };
    template<> struct inverse<op_nlt> { typedef op_lt type; };
    template<> struct inverse<op_nle> { typedef op_le type; };


    template<typename...> struct score_sum;
    
    template<typename T1,typename... Tn> struct score_sum<T1,Tn...> {
        static const int v_score = T1::v_score + score_sum<Tn...>::v_score;
    };
    template<> struct score_sum<> {
        static const int v_score = 0;
    };
    
    template<typename Op,typename... T> struct v_op_expr;
    template<typename Op,typename... T,size_t Size> struct _v_item_t<v_op_expr<Op,T...>,Size> {
        typedef decltype(Op::op(std::declval<v_item_t<T,Size> >()...)) type;
    };
    template<typename Op,typename... T> struct v_op_expr : v_expr<v_op_expr<Op,T...> > {
        static const int v_score = score_sum<T...>::v_score;
        static constexpr bool temporary = true;

        std::tuple<v_expr_store<T>...> values;

        size_t _size() const { return std::get<0>(values)._size(); }
        size_t _v_size() const { return std::get<0>(values)._v_size(); }

        template<size_t Size> FORCE_INLINE v_item_t<v_op_expr<Op,T...>,Size> vec(size_t n) const {
            return _vec<Size>(n,make_index_list<sizeof...(T)>());
        }
        
        v_op_expr(const T&... args) : values(args...) {}

    private:
        template<size_t Size,size_t... Indexes> FORCE_INLINE v_item_t<v_op_expr<Op,T...>,Size> _vec(size_t n,index_list<Indexes...>) const {
            return Op::op(std::get<Indexes>(values).template vec<Size>(n)...);
        }
    };
    
    template<typename T,typename F> struct v_apply;
    template<typename T,typename F,size_t Size> struct _v_item_t<v_apply<T,F>,Size> {
        typedef simd::v_type<typename std::result_of<F(s_item_t<T>)>::type,Size> type;
    };
    template<typename T,typename F> struct v_apply : v_expr<v_apply<T,F> > {
        static const int v_score = T::v_score - 1;
        static constexpr bool temporary = true;

        v_expr_store<T> a;
        F f;
        
        size_t _size() const { return a._size(); }
        size_t _v_size() const { return a._v_size(); }
        
        template<size_t Size> FORCE_INLINE v_item_t<v_apply<T,F>,Size> vec(size_t n) const {
            return a.template vec<Size>(n).apply(f);
        }

        v_apply(const T &a,F f) : a(a), f(f) {}
    };
    
    template<typename T> struct v_repeat;
    template<typename T,size_t Size> struct _v_item_t<v_repeat<T>,Size> {
        typedef simd::v_type<T,Size> type;
    };
    template<typename T> struct v_repeat : v_expr<v_repeat<T> > {
        static const int v_score = 0;
        static constexpr bool temporary = true;
        
        size_t size_;
        T value;
        
        size_t _size() const { return size_; }
        size_t _v_size() const { return VSIZE_MAX; }
        
        template<size_t Size> FORCE_INLINE simd::v_type<T,Size> vec(size_t n) const {
            return simd::v_type<T,Size>::repeat(value);
        }

        v_repeat(size_t s,T value) : size_(s), value(value) {}
    };

    template<typename Store,typename T> struct v_array;
    template<typename Store,typename T,size_t Size> struct _v_item_t<v_array<Store,T>,Size> {
        typedef simd::v_type<T,Size> type;
    };
    struct v_item_count {
        static constexpr int get(int d) { return d; }
    };
    template<typename Store,typename T> struct v_array : v_expr<v_array<Store,T> > {
        static const int v_score = V_SCORE_THRESHHOLD;
        static constexpr bool temporary = false;

        explicit v_array(int s) : store(s) {}
    
        template<typename F> v_array(int s,F f) : store(s) {
            fill_with(f);
        }
        
        template<typename B> FORCE_INLINE v_array(const v_expr<B> &b) : store(b.size()) {
            fill_with(b);
        }
        
        template<typename B> FORCE_INLINE v_array<Store,T> &operator=(const v_expr<B> &b) {
            assert(_size() == b.size());
            fill_with(b);
        }
        
        template<typename Op,typename B> struct _v_compound {
            typedef typename Store::item_t item_t;
            static const int v_score = B::v_score;
            
            v_array<Store,T> &self;
            const B &b;
            
            template<size_t Size> void operator()(size_t n) const {
                Op::assign(self.template vec<Size>(n),b.template vec<Size>(n));
            }
        };
        template<typename B> v_array<Store,T> &operator+=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_add,B>{*this,b});
            return *this;
        }
        template<typename B> v_array<Store,T> &operator-=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_subtract,B>{*this,b});
            return *this;
        }
        template<typename B> v_array<Store,T> &operator*=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_multiply,B>{*this,b});
            return *this;
        }
        v_array<Store,T> &operator*=(T b) {
            v_rep(_v_size(),_v_compound<op_multiply,v_repeat<T> >{*this,v_repeat<T>{_size(),b}});
            return *this;
        }
        template<typename B> v_array<Store,T> &operator/=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_divide,B>{*this,b});
            return *this;
        }
        v_array<Store,T> &operator/=(T b) {
#ifdef MULT_RECIPROCAL_INSTEAD_OF_DIV
            return operator*=(1/b);
#else
            v_rep(_v_size(),_v_compound<op_divide,v_repeat<T> >{*this,v_repeat<T>{_size(),b}});
            return *this;
#endif
        }
        template<typename B> v_array<Store,T> &operator&=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_and,B>{*this,b});
            return *this;
        }
        template<typename B> v_array<Store,T> &operator|=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_or,B>{*this,b});
            return *this;
        }
        template<typename B> v_array<Store,T> &operator^=(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_compound<op_xor,B>{*this,b});
            return *this;
        }
        
        static v_repeat<T> repeat(size_t s,T val) {
            return {s,val};
        }
        
        size_t _size() const { return store.dimension(); }
        size_t _v_size() const { return Store::v_dimension(_size()); }
        
        void fill_with(T v) {
            for(size_t i=0; i<_size(); ++i) (*this)[i] = v;
        }
        
        template<typename B> struct _v_assign {
            typedef T item_t;
            static const int v_score = B::v_score;
            
            v_array<Store,T> &self;
            const B &b;
            
            template<size_t Size> void operator()(size_t n) const {
                self.template vec<Size>(n) = b.template vec<Size>(n);
            }
        };
        template<typename B> FORCE_INLINE void fill_with(const v_expr<B> &b) {
            assert(b.v_size() >= _v_size());
            v_rep(_v_size(),_v_assign<B>{*this,b});
        }
        
        template<typename F,typename=decltype(std::declval<F>()(std::declval<size_t>()))> void fill_with(F f) {
            for(size_t i=0; i<_size(); ++i) (*this)[i] = f(i);
        }
        
        T &operator[](size_t n) { return store.items[n]; }
        T operator[](size_t n) const { return store.items[n]; }
        
        T *data() { return store.items; }
        const T *data() const { return store.items; }
        
        template<size_t Size> FORCE_INLINE simd::v_type<T,Size> &vec(size_t n) {
            return *reinterpret_cast<simd::v_type<T,Size>*>(store.items + n);
        }
        
        template<size_t Size> FORCE_INLINE simd::v_type<T,Size> vec(size_t n) const {
            return *reinterpret_cast<const simd::v_type<T,Size>*>(store.items + n);
        }
        
        typename Store::template type<v_item_count,T> store;
    };

    template<typename Op,typename T> struct v_reduce {
        typedef s_item_t<T> item_t;
        typedef v_item_t<T,simd::v_sizes<item_t>::value[0]> v_t;
        
        static const int v_score = T::v_score - (std::is_same<Op,op_add>::value && v_t::has_vec_reduce_add ? 1 : 5);
        
        v_t &r;
        item_t &r_small;
        const T &a;
        
        template<size_t Size> FORCE_INLINE void operator()(size_t n) const {
            if(Size == 1) {
                r_small = Op::op(r_small,a.template vec<1>(n)[0]);
            } else {
                /* If the AVX instruction set is used, this relies on all vector
                   operations using the AVX versions, where the upper bits are
                   set to zero. */
                r = Op::op(r,v_t(a.template vec<Size>(n)));
            }
        }
        
        static constexpr size_t smallest_vec(int i=0) {
            typedef simd::v_sizes<item_t> sizes;
            return (sizes::value[i] == 1 || sizes::value[i+1] == 1) ? sizes::value[i] : smallest_vec(i+1);
        }
    };
    template<typename Op,typename T> inline s_item_t<T> reduce(const v_expr<T> &a) {
        s_item_t<T> r_small = 0;
        if(v_reduce<Op,T>::v_score < V_SCORE_THRESHHOLD || size_t(a.size()) < v_reduce<Op,T>::smallest_vec()) {
            for(size_t i=0; i<a.size(); ++i) r_small = Op::op(r_small,static_cast<const T&>(a).template vec<1>(i)[0]);
            return r_small;
        }

        auto r = Op::template first<v_item_t<T,v_sizes<T>::value[0]> >();
        // v_size is not used because we don't want to include the pad value
        v_rep(a.size(),v_reduce<Op,T>{r,r_small,a});
        return Op::op(Op::reduce(r),r_small);
    }
    
    
    template<typename T> struct v_bool_expr;
    
    template<typename Op,typename A,typename B,typename X=typename std::enable_if<std::is_same<s_item_t<A>,s_item_t<B> >::value>::type> struct v_comparison;
    template<typename Op,typename A,typename B,typename X,size_t Size> struct _v_item_t<v_comparison<Op,A,B,X>,Size> {
        typedef v_item_t<A,Size> type;
    };
    template<typename Op,typename A,typename B,typename X> struct v_comparison : v_bool_expr<v_comparison<Op,A,B,X> > {
        static const int v_score = A::v_score + B::v_score + 1;
        
        v_expr_store<A> a;
        v_expr_store<B> b;
        
        size_t _size() const { return a._size(); }
        
        template<size_t Size> FORCE_INLINE typename v_item_t<A,Size>::mask vec(size_t n) const {
            return Op::op(a.template vec<Size>(n),b.template vec<Size>(n));
        }
        
        v_comparison<typename inverse<Op>::type,A,B> operator!() const {
            return {a,b};
        }
        
        v_comparison(v_expr_store<A> a,v_expr_store<B> b) : a(a), b(b) {}
    };
    
    template<typename Op,typename A,typename B> struct v_l_expr;
    template<typename Op,typename A,typename B,size_t Size> struct _v_item_t<v_l_expr<Op,A,B>,Size> {
        typedef v_item_t<A,Size> type;
    };
    template<typename Op,typename A,typename B> struct v_l_expr : v_bool_expr<v_l_expr<Op,A,B> > {
        static const int v_score = A::v_score + B::v_score + 1;
        
        A a;
        B b;
        
        size_t _size() const { return a._size(); }
        
        template<size_t Size> FORCE_INLINE typename v_item_t<A,Size>::mask vec(size_t n) const {
            return Op::op(a.template vec<Size>(n),b.template vec<Size>(n));
        }
        
        v_l_expr(const A &a,const B &b) : a(a), b(b) {}
    };
    
    template<typename T> struct v_l_not;
    template<typename T,size_t Size> struct _v_item_t<v_l_not<T>,Size> {
        typedef v_item_t<T,Size> type;
    };
    template<typename T> struct v_l_not : v_bool_expr<v_l_not<T> > {
        static const int v_score = T::v_score + 1;
        
        T a;
        
        size_t _size() const { return a._size(); }
        
        template<size_t Size> FORCE_INLINE typename v_item_t<T,Size>::mask vec(size_t n) const {
            return !a.template vec<Size>(n);
        }
        
        template<typename B> v_l_expr<op_l_andn,T,B> operator&&(const v_bool_expr<B> &b) const {
            return {a,b};
        }
        
        T operator!() const {
            return a;
        }
    };
    
    template<typename T> struct v_bool_expr {
        size_t size() const {
            return static_cast<const T*>(this)->_size();
        }
        operator T&() { return *static_cast<T*>(this); }
        operator const T&() const { return *static_cast<const T*>(this); }
        
        bool any() const;
        bool all() const;
        
        template<typename B> v_l_expr<op_l_and,T,B> operator&&(const v_bool_expr<B> &b) const {
            return {*this,b};
        }
        
        template<typename B> v_l_expr<op_l_or,T,B> operator||(const v_bool_expr<B> &b) const {
            return {*this,b};
        }
        
        v_l_not<T> operator!() const {
            return {*this};
        }
    };

    template<typename T> struct _v_any {
        typedef s_item_t<T> item_t;
        static const int v_score = T::v_score;
        
        const T &self;
        
        template<size_t Size> FORCE_INLINE bool operator()(size_t n) const {
            return self.template vec<Size>(n).any();
        }
    };
    template<typename T> inline bool v_bool_expr<T>::any() const {
        return v_rep_until(size(),impl::_v_any<T>{*this});
    }

    template<typename T> struct _v_not_all {
        typedef s_item_t<T> item_t;
        static const int v_score = T::v_score;
        
        const T &self;
        
        template<size_t Size> FORCE_INLINE bool operator()(size_t n) const {
            return !self.template vec<Size>(n).all();
        }
    };
    template<typename T> inline bool v_bool_expr<T>::all() const {
        return !v_rep_until(size(),impl::_v_not_all<T>{*this});
    }
    
    
    template<typename T> struct v_expr {
        operator T &() { return *static_cast<T*>(this); }
        operator const T &() const { return *static_cast<const T*>(this); }
        
        size_t size() const { return static_cast<const T*>(this)->_size(); }
        size_t v_size() const { return static_cast<const T*>(this)->_v_size(); }
        
        template<typename B> v_op_expr<op_add,T,B> operator+(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_op_expr<op_subtract,T,B> operator-(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_op_expr<op_multiply,T,B> operator*(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        v_op_expr<op_multiply,T,v_repeat<s_item_t<T> > > operator*(s_item_t<T> b) const {
            return {*this,v_repeat<s_item_t<T> >{size(),b}};
        }
        
        template<typename B> v_op_expr<op_divide,T,B> operator/(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
#ifdef MULT_RECIPROCAL_INSTEAD_OF_DIV
        v_op_expr<op_multiply,T,v_repeat<s_item_t<T> > > operator/(s_item_t<T> b) const {
            return operator*(1/b);
        }
#else
        v_op_expr<op_divide,T,v_repeat<s_item_t<T> > > operator/(s_item_t<T> b) const {
            return {*this,v_repeat<s_item_t<T> >{size(),b}};
        }
#endif
        
        template<typename B> v_op_expr<op_and,T,B> operator&(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_op_expr<op_or,T,B> operator|(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_op_expr<op_xor,T,B> operator^(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        v_op_expr<op_negate,T> operator-() const { return {*this}; }

        template<typename B> v_comparison<op_eq,T,B> operator==(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_comparison<op_neq,T,B> operator!=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_comparison<op_gt,T,B> operator>(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_comparison<op_ge,T,B> operator>=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_comparison<op_lt,T,B> operator<(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename B> v_comparison<op_le,T,B> operator<=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*this,b};
        }
        
        template<typename F> v_apply<T,F> apply(F f) const {
            return {*this,f};
        }
        
        v_op_expr<op_abs,T> abs() const { return {*this}; }
        v_op_expr<op_max,T> max() const { return {*this}; }
        v_op_expr<op_min,T> min() const { return {*this}; }
        
        s_item_t<T> reduce_add() const { return reduce<op_add,T>(*this); }
        s_item_t<T> reduce_max() const { return reduce<op_max,T>(*this); }
        s_item_t<T> reduce_min() const { return reduce<op_min,T>(*this); }
    };
}

using impl::v_array;

#endif
