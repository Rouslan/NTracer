#ifndef v_array_hpp
#define v_array_hpp

#include <tuple>
#include <utility>

#include "simd.hpp"
#include "geom_allocator.hpp"


// this only applies to dividing by a scalar
// Enabling this seems to causes problems
//#define MULT_RECIPROCAL_INSTEAD_OF_DIV

namespace impl {
    /* This is part of a very simple heuristic for determining if something is
       worth vectorizing. A score greater than or equal to this value means yes.
     */
    constexpr int V_SCORE_THRESHHOLD = 2;
    //const int V_SCORE_THRESHHOLD = -100;

    constexpr size_t VSIZE_MAX = std::numeric_limits<size_t>::max();


    template<typename T> struct v_expr;

    /* "v_expr" needs to know what "vec" returns in a given expression template,
    but the expression template needs to be a sub-class of "v_expr", thus the
    return type needs to be specified manually before the expression template's
    definition. */
    template<typename,size_t> struct _v_item_t {};
    template<typename T,size_t Size> using v_item_t = typename _v_item_t<T,Size>::type;
    template<typename T> using s_item_t = typename _v_item_t<T,1>::type::item_t;


    template<typename F> using v_sizes = simd::v_sizes<s_item_t<F>>;

    template<typename F,size_t SI> void _v_rep(size_t n,size_t i,F f) {
        constexpr size_t size = simd::v_sizes<typename F::item_t>::value[SI];
        if constexpr(F::v_score >= V_SCORE_THRESHHOLD && size > 1) {
            if constexpr(F::max_items >= size) {
                for(; i<(n - (size - 1)); i+= size) f.template operator()<size>(i);
            }

            _v_rep<F,SI+1>(n,i,f);
        } else {
            for(; i<n; ++i) f.template operator()<1>(i);
        }
    }

    template<typename F> FORCE_INLINE void v_rep(size_t n,F f) {
        assert(n != VSIZE_MAX);
        _v_rep<F,0>(n,0,f);
    }


    template<typename F,size_t SI> bool _v_rep_until(size_t n,size_t i,F f) {
        constexpr size_t size = simd::v_sizes<typename F::item_t>::value[SI];
        if constexpr(F::v_score >= V_SCORE_THRESHHOLD && size > 1) {
            if constexpr(F::max_items >= size) {
                for(; i<(n - (size - 1)); i+= size) {
                    if(f.template operator()<size>(i)) return true;
                }
            }

            return _v_rep_until<F,SI+1>(n,i,f);
        } else {
            for(; i<n; ++i) {
                if(f.template operator()<1>(i)) return true;
            }
            return false;
        }
    }

    template<typename F> FORCE_INLINE bool v_rep_until(size_t n,F f) {
        assert(n != VSIZE_MAX);
        return _v_rep_until<F,0>(n,0,f);
    }
    template<typename F> FORCE_INLINE bool v_rep_until(size_t start,size_t end,F f) {
        return _v_rep_until<F,0>(end,start,f);
    }

    #if 0
    template<typename TR,size_t RSize,typename F,size_t SI>
    simd::packed_union_array<TR,RSize> _v_rep_cvt_step(size_t i,F1 f1) {
        constexpr int size = simd::v_sizes<typename F::item_t>::value[SI];
        static_assert(size <= RSize);

        simd::packed_union_array<F::item_t,RSize> tmp;
        for(size_t j=0; j<RSize; j+=size) simd::at<size>(tmp,j) = f1.template operator()<size>(i+j);

        return simd::convert<TR>(tmp);
    }

    template<typename TR,size_t RSize,typename F1,size_t SI1,typename F2,size_t SI2,typename... Fn,size_t... SIn>
    simd::packed_union_array<TR,RSize> _v_rep_cvt_step(size_t i,F1 f1,F2 f2,Fn... fn) {
        constexpr int size = simd::v_sizes<typename F1::item_t>::value[SI1];
        static_assert(size <= RSize);
        constexpr int maxsize2 = std::max({
            simd::v_sizes<typename F2::item_t>::value[SI2],
            simd::v_sizes<typename Fn::item_t>::value[SIn]...});

        simd::packed_union_array<F2::item_t,RSize> tmp;
        for(size_t j=0; j<RSize; j+=maxsize2) {
            simd::chunk_at<maxsize2>(tmp,j) = _v_rep_cvt_step<F1::item_t,maxsize2,F2,SI2,Fn...,SIn...>(i+j,f2,fn...);
        }
        simd::packed_union_array<F1::item_t,RSize> tmp2;
        for(size_t j=0; j<RSize; j+=size) {
            simd::at<size>(tmp2,j) = f1.template operator()<size>(i+j,simd::at<size>(tmp,j));
        }

        return simd::convert<TR>(tmp2);
    }

    template<typename F,size_t SI> constexpr size_t _advance_si(size_t maxsize) {
        return simd::v_sizes<typename F1::item_t>::value[SI] == maxsize ? SI + 1 : SI;
    }

    template<typename F1,size_t SI1,typename F2,size_t SI2,typename... Fn,size_t... SIn>
    void _v_rep_cvt(size_t n,size_t i,F1 f1,F2 f2,Fn... fn) {
        constexpr int size = simd::v_sizes<typename F1::item_t>::value[SI1];
        constexpr int maxsize2 = std::max({
            simd::v_sizes<typename F2::item_t>::value[SI2],
            simd::v_sizes<typename Fn::item_t>::value[SIn]...});
        constexpr int maxsize = std::max(size,maxsize2);
        constexpr size_t max_items = std::min({F1::max_items,F2::max_items,Fs::max_items...});
        constexpr size_t avg_score = (F1::v_score + F2::v_score + ... + Fn::v_score) / (sizeof...(Fn) + 2);
        if constexpr(avg_score >= V_SCORE_THRESHHOLD && maxsize > 1) {
            if constexpr(max_items >= size) {
                for(; long(i)<(long(n) - (long(size) - 1)); i+= size) {
                    simd::packed_union_array<F2::item_t,maxsize> tmp;
                    for(size_t j=0; j<maxsize; j+=maxsize2) {
                        simd::chunk_at<size>(tmp,j) = _v_rep_cvt_step<F1::item_t,maxsize2,F2,SI2,Fn...,SIn...>(i+j,f2,fn...);
                    }

                    for(size_t j=0; j<maxsize; j+=size) {
                        f1.template operator()<size>(i+j,simd::at<size>(tmp,j));
                    }
                }
            }

            _v_rep<
                F1,
                _advance_si<F1,SI1>(maxsize),
                F2,
                _advance_si<F2,SI2>(maxsize),
                Fn...,
                _advance_si<Fn,SIn>(maxsize)...>(n,i,f1,f2,fn...);
        } else {
            for(; i<n; ++i) f1.template operator()<1>(i,simd::at<1>(_v_rep_cvt<F::item_t,1,F2,SI2,Fn...,SIn...>(i,f2,fn...),0));
        }
    }

    template<typename> struct _zero { static constexpr size_t value = 0; };

    /* When working with more than one base type, the largest compatible vector
    register might not hold the same number of items, thus the operations need
    to be broken into seperate functors, so they may be called different numbers
    of times */
    template<typename F1,typename F2,typename... Fs> FORCE_INLINE void v_rep_cvt(size_t n,F1 f1,F2 f2Fs... fs) {
        assert(n != VSIZE_MAX);
        _v_rep<F1,0,F2,0,_zero<Fs>::value...>(n,0,f1,f2,fs...);
    }
    #endif


    template<typename T> using v_expr_store = std::conditional_t<T::temporary,T,const T&>;


    struct op_add {
        template<typename A,typename B> static FORCE_INLINE auto op(A a,B b) {
            return a + b;
        }
        template<typename T> static FORCE_INLINE auto reduce(T x) {
            return simd::reduce_add(x);
        }
        template<typename T> static FORCE_INLINE constexpr T first() {
            return T::zeros();
        }
    };

#define BINARY_OP(NAME,OP) struct NAME { \
        template<typename A,typename B> static FORCE_INLINE auto op(A a,B b) { \
            return a OP b; \
        } \
    }
#define BINARY_FUNC(NAME,F) struct NAME { \
        template<typename A,typename B> static FORCE_INLINE auto op(A a,B b) { \
            return F(a,b); \
        } \
    }
#define UNARY_OP(NAME,OP) struct NAME { \
        template<typename T> static FORCE_INLINE auto op(T a) { \
            return OP a; \
        } \
    }
#define UNARY_FUNC(NAME,F) struct NAME { \
        template<typename T> static FORCE_INLINE auto op(T a) { \
            return F(a); \
        } \
    }

    BINARY_OP(op_subtract,-);
    BINARY_OP(op_multiply,*);
    BINARY_OP(op_divide,/);
    BINARY_OP(op_and,&);
    BINARY_OP(op_or,|);
    BINARY_OP(op_xor,^);
    BINARY_OP(op_eq,==);
    BINARY_OP(op_neq,!=);
    BINARY_OP(op_gt,>);
    BINARY_OP(op_ge,>=);
    BINARY_OP(op_lt,<);
    BINARY_OP(op_le,<=);
    BINARY_FUNC(op_ngt,simd::cmp_ngt);
    BINARY_FUNC(op_nge,simd::cmp_nge);
    BINARY_FUNC(op_nlt,simd::cmp_nlt);
    BINARY_FUNC(op_nle,simd::cmp_nle);
    BINARY_OP(op_l_and,&&);
    BINARY_OP(op_l_or,||);
    UNARY_OP(op_l_not,!);
    UNARY_OP(op_negate,-);
    UNARY_FUNC(op_abs,simd::abs);
    BINARY_FUNC(op_l_andn,simd::l_andn);
    BINARY_FUNC(op_l_xor,simd::l_xor);
    BINARY_FUNC(op_l_xnor,simd::l_xnor);

#undef BINARY_OP
#undef BINARY_FUNC
#undef UNARY_OP
#undef UNARY_FUNC

    struct op_max {
        template<typename T> static FORCE_INLINE T op(T a,T b) {
            return simd::max(a,b);
        }
        template<typename T> static FORCE_INLINE typename T::item_t reduce(T x) {
            return x.reduce_max();
        }
        template<typename T> static FORCE_INLINE constexpr T first() {
            return T::repeat(std::numeric_limits<typename T::item_t>::lowest());
        }
    };

    struct op_min {
        template<typename T> static FORCE_INLINE T op(T a,T b) {
            return simd::min(a,b);
        }
        template<typename T> static FORCE_INLINE typename T::item_t reduce(T x) {
            return x.reduce_min();
        }
        template<typename T> static FORCE_INLINE constexpr T first() {
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

    template<typename Op,typename... T> struct v_op_expr;
    template<typename Op,typename... T,size_t Size> struct _v_item_t<v_op_expr<Op,T...>,Size> {
        typedef decltype(Op::op(std::declval<v_item_t<T,Size>>()...)) type;
    };
    template<typename Op,typename... T> struct v_op_expr : v_expr<v_op_expr<Op,T...>> {
        template<typename U> static constexpr auto _min(U x) { return x; }
        template<typename U1,typename U2,typename... U> static constexpr auto _min(U1 x1,U2 x2,U... x) {
            return std::min({x1,x2,x...});
        }

        static constexpr int v_score = (0 + ... + T::v_score);
        static constexpr size_t max_items = _min(T::max_items...);
        static constexpr bool temporary = true;

        std::tuple<v_expr_store<T>...> values;

        size_t _size() const { return std::get<0>(values)._size(); }
        static constexpr bool supports_padding = (true && ... && T::supports_padding);

        template<size_t Size> FORCE_INLINE v_item_t<v_op_expr,Size> vec(size_t n) const {
            return std::apply(
                [n](auto... x) { return Op::op(x.template vec<Size>(n)...); },
                values);
        }

        v_op_expr(const T&... args) : values(args...) {}
    };

    template<typename T,typename F> struct v_apply;
    template<typename T,typename F,size_t Size> struct _v_item_t<v_apply<T,F>,Size> {
        typedef simd::v_type<std::invoke_result_t<F,s_item_t<T>>,Size> type;
    };
    template<typename T,typename F> struct v_apply : v_expr<v_apply<T,F>> {
        static constexpr int v_score = T::v_score - 1;
        static constexpr size_t max_items = T::max_items;
        static constexpr bool temporary = true;

        v_expr_store<T> a;
        F f;

        size_t _size() const { return a._size(); }
        static constexpr bool supports_padding = T::supports_padding;

        template<size_t Size> FORCE_INLINE v_item_t<v_apply,Size> vec(size_t n) const {
            return simd::apply(f,a.template vec<Size>(n));
        }

        v_apply(const T &a,F f) : a(a), f(f) {}
    };

    template<typename T> struct v_repeat;
    template<typename T,size_t Size> struct _v_item_t<v_repeat<T>,Size> {
        typedef simd::v_type<T,Size> type;
    };
    template<typename T> struct v_repeat : v_expr<v_repeat<T>> {
        static constexpr int v_score = 0;
        static constexpr size_t max_items = std::numeric_limits<size_t>::max();
        static constexpr bool temporary = true;

        size_t size_;
        T value;

        size_t _size() const { return size_; }
        static constexpr bool supports_padding = true;

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
        static constexpr size_t get(size_t d) { return d; }
    };
    template<typename Store,typename T> struct v_array : v_expr<v_array<Store,T>> {
        static constexpr int v_score = V_SCORE_THRESHHOLD;
        static constexpr bool temporary = false;
        static constexpr size_t max_items = Store::template type<v_item_count,T>::max_items;

        explicit v_array(size_t s,v_array_allocator *a=Store::def_allocator) : store{s,a} {}

        v_array(const v_array&) = default;
        v_array(v_array &&b) : store{std::move(b.store)} {}

        v_array(const v_array &b,shallow_copy_t) : store{b.store,shallow_copy} {}

        template<typename F> v_array(size_t s,F f,v_array_allocator *a=Store::def_allocator) : store{s,a} {
            fill_with(f);
        }

        template<typename B> FORCE_INLINE v_array(const v_expr<B> &b,v_array_allocator *a=Store::def_allocator)
            : store(b.size(),a)
        {
            fill_with(b);
        }

        v_array &operator=(const v_array&) = default;
        v_array &operator=(v_array &&b) = default;

        template<typename B> FORCE_INLINE v_array &operator=(const v_expr<B> &b) {
            fill_with(b);
            return *this;
        }

        template<typename B> size_t padded_size_with(const v_expr<B> &b) const {
            assert(_size() == b.size());
            if constexpr(B::supports_padding) {
                return Store::template v_dimension<T>(_size());
            } else {
                return _size();
            }
        }

        size_t padded_size() const {
            return Store::template v_dimension<T>(_size());
        }

        template<typename Op,typename B> struct _v_compound {
            typedef T item_t;
            static const int v_score = B::v_score;
            static const size_t max_items = std::min(v_array::max_items,B::max_items);

            v_array<Store,T> &self;
            const B &b;

            template<size_t Size> void operator()(size_t n) const {
                self.store.template store_vec<Size>(n,Op::op(self.template vec<Size>(n),b.template vec<Size>(n)));
            }
        };
        template<typename B> v_array &operator+=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_add,B>{*this,b});
            return *this;
        }
        template<typename B> v_array &operator-=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_subtract,B>{*this,b});
            return *this;
        }
        template<typename B> v_array &operator*=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_multiply,B>{*this,b});
            return *this;
        }
        v_array &operator*=(T b) {
            v_rep(padded_size(),_v_compound<op_multiply,v_repeat<T>>{*this,v_repeat<T>{_size(),b}});
            return *this;
        }
        template<typename B> v_array &operator/=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_divide,B>{*this,b});
            return *this;
        }
        v_array &operator/=(T b) {
#ifdef MULT_RECIPROCAL_INSTEAD_OF_DIV
            return operator*=(1/b);
#else
            v_rep(padded_size(),_v_compound<op_divide,v_repeat<T>>{*this,v_repeat<T>{_size(),b}});
            return *this;
#endif
        }
        template<typename B> v_array &operator&=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_and,B>{*this,b});
            return *this;
        }
        template<typename B> v_array &operator|=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_or,B>{*this,b});
            return *this;
        }
        template<typename B> v_array &operator^=(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_compound<op_xor,B>{*this,b});
            return *this;
        }

        static v_repeat<T> repeat(size_t s,T val) {
            return {s,val};
        }

        size_t _size() const { return store.dimension(); }
        static constexpr bool supports_padding = true;

        void fill_with(T v) {
            for(size_t i=0; i<_size(); ++i) (*this)[i] = v;
        }

        template<typename B> struct _v_assign {
            typedef T item_t;
            static constexpr int v_score = B::v_score;
            static constexpr size_t max_items = std::min(v_array::max_items,B::max_items);

            v_array<Store,T> &self;
            const B &b;

            template<size_t Size> void operator()(size_t n) const {
                self.store.template store_vec<Size>(n,b.template vec<Size>(n));
            }
        };
        template<typename B> void fill_with(const v_expr<B> &b) {
            v_rep(padded_size_with(b),_v_assign<B>{*this,b});
        }

        template<typename F,typename=decltype(std::declval<F>()(std::declval<size_t>()))> void fill_with(F f) {
            for(size_t i=0; i<_size(); ++i) (*this)[i] = f(i);
        }

        T *data() { return store.data(); }
        const T *data() const { return store.data(); }

        T &operator[](size_t n) {
            assert(n < _size());
            return data()[n];
        }
        const T &operator[](size_t n) const {
            assert(n < _size());
            return data()[n];
        }

        template<size_t Size> FORCE_INLINE simd::v_type<T,Size> vec(size_t n) const {
            return store.template vec<Size>(n);
        }

        void swap(v_array &b) {
            using std::swap;
            swap(store,b.store);
        }

        typename Store::template type<v_item_count,T> store;
    };

    template<typename Op,typename T> struct v_reduce {
        typedef s_item_t<T> item_t;
        typedef v_item_t<T,simd::v_sizes<item_t>::value[0]> v_t;

        static constexpr int v_score = T::v_score - (std::is_same_v<Op,op_add> && v_t::has_vec_reduce_add ? 1 : 5);
        static constexpr size_t max_items = T::max_items;

        v_t &r;
        simd::scalar<item_t> &r_small;
        const T &a;

        template<size_t Size> FORCE_INLINE void operator()(size_t n) const {
            if constexpr(Size == 1) {
                r_small = Op::op(r_small,a.template vec<1>(n));
            } else {
                /* If the AVX instruction set is used, this relies on all vector
                   operations using the AVX versions, where the upper bits are
                   set to zero. */
                r = Op::op(r,v_t(a.template vec<Size>(n)));
            }
        }

        static constexpr size_t smallest_vec(size_t i=0) {
            typedef simd::v_sizes<item_t> sizes;
            return (sizes::value[i] == 1 || sizes::value[i+1] == 1) ? sizes::value[i] : smallest_vec(i+1);
        }
    };
    template<typename Op,typename T> inline s_item_t<T> reduce(const v_expr<T> &a) {
        auto r_small = Op::template first<v_item_t<T,1>>();
        if(v_reduce<Op,T>::v_score < V_SCORE_THRESHHOLD || a.size() < v_reduce<Op,T>::smallest_vec()) {
            for(size_t i=0; i<a.size(); ++i) r_small = Op::op(r_small,static_cast<const T&>(a).template vec<1>(i));
            return r_small[0];
        }

        auto r = Op::template first<v_item_t<T,v_sizes<T>::value[0]>>();
        /* padded_size is not used because we don't want to include the pad
           values */
        v_rep(a.size(),v_reduce<Op,T>{r,r_small,a});
        return Op::op(Op::reduce(r),r_small[0]);
    }


    template<typename T> struct v_bool_expr;

    template<typename Op,typename A,typename B,typename X=std::enable_if_t<std::is_same_v<s_item_t<A>,s_item_t<B>>>> struct v_comparison;
    template<typename Op,typename A,typename B,typename X,size_t Size> struct _v_item_t<v_comparison<Op,A,B,X>,Size> {
        typedef v_item_t<A,Size> type;
    };
    template<typename Op,typename A,typename B,typename X> struct v_comparison : v_bool_expr<v_comparison<Op,A,B,X>> {
        static constexpr int v_score = A::v_score + B::v_score + 1;
        static constexpr size_t max_items = std::min(A::max_items,B::max_items);

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
    template<typename Op,typename A,typename B> struct v_l_expr : v_bool_expr<v_l_expr<Op,A,B>> {
        static constexpr int v_score = A::v_score + B::v_score + 1;
        static constexpr size_t max_items = std::min(A::max_items,B::max_items);

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
    template<typename T> struct v_l_not : v_bool_expr<v_l_not<T>> {
        static constexpr int v_score = T::v_score + 1;
        static constexpr size_t max_items = T::max_items;

        T a;

        size_t _size() const { return a._size(); }

        template<size_t Size> FORCE_INLINE auto vec(size_t n) const {
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
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_l_expr<op_l_or,T,B> operator||(const v_bool_expr<B> &b) const {
            return {*static_cast<const T*>(this),b};
        }

        v_l_not<T> operator!() const {
            return {*static_cast<const T*>(this)};
        }
    };

    template<typename T> struct _v_any {
        typedef s_item_t<T> item_t;
        static constexpr int v_score = T::v_score;
        static constexpr size_t max_items = T::max_items;

        const T &self;

        template<size_t Size> bool operator()(size_t n) const {
            return self.template vec<Size>(n).any();
        }
    };
    template<typename T> inline bool v_bool_expr<T>::any() const {
        return v_rep_until(size(),impl::_v_any<T>{*this});
    }

    template<typename T> struct _v_not_all {
        typedef s_item_t<T> item_t;
        static constexpr int v_score = T::v_score;
        static constexpr size_t max_items = T::max_items;

        const T &self;

        template<size_t Size> bool operator()(size_t n) const {
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

        template<typename B> v_op_expr<op_add,T,B> operator+(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_op_expr<op_subtract,T,B> operator-(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_op_expr<op_multiply,T,B> operator*(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }
        v_op_expr<op_multiply,T,v_repeat<s_item_t<T>>> operator*(s_item_t<T> b) const {
            return {*static_cast<const T*>(this),v_repeat<s_item_t<T>>{size(),b}};
        }

        template<typename B> v_op_expr<op_divide,T,B> operator/(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }
#ifdef MULT_RECIPROCAL_INSTEAD_OF_DIV
        v_op_expr<op_multiply,T,v_repeat<s_item_t<T>>> operator/(s_item_t<T> b) const {
            return operator*(1/b);
        }
#else
        v_op_expr<op_divide,T,v_repeat<s_item_t<T>>> operator/(s_item_t<T> b) const {
            return {*static_cast<const T*>(this),v_repeat<s_item_t<T>>{size(),b}};
        }
#endif

        template<typename B> v_op_expr<op_and,T,B> operator&(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_op_expr<op_or,T,B> operator|(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_op_expr<op_xor,T,B> operator^(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        v_op_expr<op_negate,T> operator-() const { return {*this}; }

        template<typename B> v_comparison<op_eq,T,B> operator==(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_comparison<op_neq,T,B> operator!=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_comparison<op_gt,T,B> operator>(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_comparison<op_ge,T,B> operator>=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_comparison<op_lt,T,B> operator<(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        template<typename B> v_comparison<op_le,T,B> operator<=(const v_expr<B> &b) const {
            assert(size() == b.size());
            return {*static_cast<const T*>(this),b};
        }

        s_item_t<T> operator[](size_t n) const {
            assert(n < size());
            return static_cast<const T*>(this)->template vec<1>(n)[0];
        }

        template<typename F> v_apply<T,F> apply(F f) const {
            return {*static_cast<const T*>(this),f};
        }

        v_op_expr<op_abs,T> abs() const { return {*static_cast<const T*>(this)}; }

        s_item_t<T> reduce_add() const { return reduce<op_add,T>(*static_cast<const T*>(this)); }
        s_item_t<T> reduce_max() const { return reduce<op_max,T>(*static_cast<const T*>(this)); }
        s_item_t<T> reduce_min() const { return reduce<op_min,T>(*static_cast<const T*>(this)); }
    };

    template<typename A,typename B> v_op_expr<op_min,A,B> min(const v_expr<A> &a,const v_expr<B> &b) {
        return {a,b};
    }

    template<typename A,typename B> v_op_expr<op_max,A,B> max(const v_expr<A> &a,const v_expr<B> &b) {
        return {a,b};
    }
}

using impl::v_array;
using impl::min;
using impl::max;

template<typename Store,typename T> void swap(v_array<Store,T> &a,v_array<Store,T> &b) {
    a.swap(b);
}

#endif
