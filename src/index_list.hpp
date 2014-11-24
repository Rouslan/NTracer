#ifndef index_list_hpp
#define index_list_hpp

#include <tuple>
#include <type_traits>


template<size_t... Indexes> struct index_list {
    typedef index_list<Indexes..., sizeof...(Indexes)> next;
};

namespace impl {
    template<size_t N> struct _make_index_list {
        typedef typename _make_index_list<N-1>::type::next type;
    };

    template<> struct _make_index_list<0> {
        typedef index_list<> type;
    };
    
    template<typename F,typename... T,size_t... Indexes> inline typename std::result_of<F(T...)>::type _apply(index_list<Indexes...>,F f,const std::tuple<T...> &vals) {
        return f(std::get<Indexes>(vals)...);
    }
    
    template<typename O,typename M,typename... T,size_t... Indexes> inline auto _apply(index_list<Indexes...>,O &o,M m,const std::tuple<T...> &vals) -> decltype((o.*m)(std::declval<T>()...)) {
        return (o.*m)(std::get<Indexes>(vals)...);
    }
}
    
template<size_t N> using make_index_list = typename impl::_make_index_list<N>::type;

template<typename F,typename... T> inline typename std::result_of<F(T...)>::type apply(F f,const std::tuple<T...> &vals) {
    return impl::_apply<F,T...>(make_index_list<sizeof...(T)>(),f,vals);
}

template<typename O,typename M,typename... T> inline auto apply(O &o,M m,const std::tuple<T...> &vals) -> decltype((o.*m)(std::declval<T>()...)){
    return impl::_apply<O,M,T...>(make_index_list<sizeof...(T)>(),o,m,vals);
}

#endif
