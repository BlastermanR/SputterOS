#ifndef SPUTTEROS_UTILS_MINMAX_H
#define SPUTTEROS_UTILS_MINMAX_H

/**
 * @file MinMax.h
 * @brief Inline constexpr min/max helpers — replaces `<algorithm>`.
 *
 * Provides `sput_min` and `sput_max` as zero-overhead, freestanding
 * replacements for `std::min` / `std::max`, avoiding the hosted
 * `<algorithm>` header and any associated macro conflicts on some
 * embedded toolchains (e.g. Arduino `min` / `max` macros).
 *
 * @author Ryan Massie (rmassie)
 * @date 4/14/2026
 */

namespace SputterOS
{

/**
 * @brief Return the smaller of two values.
 * @tparam T  Comparable type.
 * @param a   First value.
 * @param b   Second value.
 * @return    The lesser of @p a and @p b.
 */
template <typename T> constexpr const T &sput_min(const T &a, const T &b) { return (b < a) ? b : a; }

/**
 * @brief Return the larger of two values.
 * @tparam T  Comparable type.
 * @param a   First value.
 * @param b   Second value.
 * @return    The greater of @p a and @p b.
 */
template <typename T> constexpr const T &sput_max(const T &a, const T &b) { return (a < b) ? b : a; }

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_MINMAX_H
