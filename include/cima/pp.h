#ifndef CIMA_PP_H
#define CIMA_PP_H

#define CIMA_PP_CAT_(a, ...) a##__VA_ARGS__
#define CIMA_PP_CAT(a, ...) CIMA_PP_CAT_(a, __VA_ARGS__)
#define CIMA_PP_ID(...) __VA_ARGS__
#define CIMA_PP_UNPAREN(...) __VA_ARGS__
#define CIMA_PP_FIRST(a, ...) a

// 4^5 rescans, plenty for a few hundred items per list.
#define CIMA_PP_EXPAND(...) CIMA_PP_EXPAND4(CIMA_PP_EXPAND4(CIMA_PP_EXPAND4(CIMA_PP_EXPAND4(__VA_ARGS__))))
#define CIMA_PP_EXPAND4(...) CIMA_PP_EXPAND3(CIMA_PP_EXPAND3(CIMA_PP_EXPAND3(CIMA_PP_EXPAND3(__VA_ARGS__))))
#define CIMA_PP_EXPAND3(...) CIMA_PP_EXPAND2(CIMA_PP_EXPAND2(CIMA_PP_EXPAND2(CIMA_PP_EXPAND2(__VA_ARGS__))))
#define CIMA_PP_EXPAND2(...) CIMA_PP_EXPAND1(CIMA_PP_EXPAND1(CIMA_PP_EXPAND1(CIMA_PP_EXPAND1(__VA_ARGS__))))
#define CIMA_PP_EXPAND1(...) __VA_ARGS__
#define CIMA_PP_PARENS ()

// m(x) for every x. a trailing comma gives an empty x, every m must accept it.
// m must not use CIMA_PP_FOR_EACH itself, nested lists are expanded in arguments first.
#define CIMA_PP_FOR_EACH(m, ...) __VA_OPT__(CIMA_PP_EXPAND(CIMA_PP_FE_(m, __VA_ARGS__)))
#define CIMA_PP_FE_(m, a, ...) m(a) __VA_OPT__(CIMA_PP_FE_AGAIN CIMA_PP_PARENS(m, __VA_ARGS__))
#define CIMA_PP_FE_AGAIN() CIMA_PP_FE_

// m(d, x) for every x.
#define CIMA_PP_FOR_EACH_D(m, d, ...) __VA_OPT__(CIMA_PP_EXPAND(CIMA_PP_FED_(m, d, __VA_ARGS__)))
#define CIMA_PP_FED_(m, d, a, ...) m(d, a) __VA_OPT__(CIMA_PP_FED_AGAIN CIMA_PP_PARENS(m, d, __VA_ARGS__))
#define CIMA_PP_FED_AGAIN() CIMA_PP_FED_

#define CIMA_PP_PLUS_ONE(...) __VA_OPT__(+1)
#define CIMA_PP_COUNT(...) (0 CIMA_PP_FOR_EACH(CIMA_PP_PLUS_ONE, __VA_ARGS__))

// CIMA_PP_IS_PAREN((a, b)) is 1, CIMA_PP_IS_PAREN(a) is 0.
#define CIMA_PP_PROBE(...) ~, 1,
#define CIMA_PP_CHECK(...) CIMA_PP_CHECK_N(__VA_ARGS__, 0, )
#define CIMA_PP_CHECK_N(x, n, ...) n
#define CIMA_PP_IS_PAREN(x) CIMA_PP_CHECK(CIMA_PP_PROBE x)

// CIMA_PP_IF(c)(then, else), both branches must be macro names applied afterwards, so the other one never expands.
#define CIMA_PP_IF(c) CIMA_PP_CAT(CIMA_PP_IF_, c)
#define CIMA_PP_IF_1(t, f) t
#define CIMA_PP_IF_0(t, f) f

#endif
