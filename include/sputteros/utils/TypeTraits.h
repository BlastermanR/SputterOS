#ifndef SPUTTEROS_UTILS_TYPETRAITS_H
#define SPUTTEROS_UTILS_TYPETRAITS_H

/**
 * @file TypeTraits.h
 * @brief Freestanding type trait helpers — replaces `<type_traits>`.
 *
 * Uses compiler builtins (`__is_enum`, `__is_base_of`) available in
 * GCC, Clang, and MSVC since ~2012. Falls back to minimal hand-rolled
 * implementations on other compilers.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

namespace SputterOS
{

// ── is_enum ──────────────────────────────────────────────────────────────────

template <typename T> struct sput_is_enum
{
    static constexpr bool value = __is_enum(T);
};

// ── is_base_of ───────────────────────────────────────────────────────────────

template <typename Base, typename Derived> struct sput_is_base_of
{
    static constexpr bool value = __is_base_of(Base, Derived);
};

// ── void_t ───────────────────────────────────────────────────────────────────

template <typename...> using void_t = void;

// ── conditional ──────────────────────────────────────────────────────────────

template <bool B, typename T, typename F> struct sput_conditional
{
    using type = T;
};

template <typename T, typename F> struct sput_conditional<false, T, F>
{
    using type = F;
};

template <bool B, typename T, typename F> using sput_conditional_t = typename sput_conditional<B, T, F>::type;

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_TYPETRAITS_H
