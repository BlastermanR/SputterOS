#ifndef SPUTTEROS_UTILS_MEMUTILS_H
#define SPUTTEROS_UTILS_MEMUTILS_H

/**
 * @file MemUtils.h
 * @brief Freestanding memory and string utilities — replaces `<cstring>`.
 *
 * Provides `sput_memcpy`, `sput_strlen`, and `sput_strncpy` as
 * zero-overhead replacements for `std::memcpy`, `std::strlen`, and
 * `std::strncpy`, using compiler builtins where available.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

inline void *sput_memcpy(void *dest, const void *src, std::size_t n)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_memcpy(dest, src, n);
#else
    auto       *d = static_cast<uint8_t *>(dest);
    const auto *s = static_cast<const uint8_t *>(src);
    for (std::size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dest;
#endif
}

inline std::size_t sput_strlen(const char *s)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_strlen(s);
#else
    std::size_t len = 0;
    while (s[len] != '\0')
        ++len;
    return len;
#endif
}

inline char *sput_strncpy(char *dest, const char *src, std::size_t n)
{
    std::size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i)
        dest[i] = src[i];
    for (; i < n; ++i)
        dest[i] = '\0';
    return dest;
}

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_MEMUTILS_H
