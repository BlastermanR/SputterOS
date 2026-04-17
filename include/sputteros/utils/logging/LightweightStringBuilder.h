#ifndef SPUTTEROS_UTILS_LIGHTWEIGHTSTRINGBUILDER_H
#define SPUTTEROS_UTILS_LIGHTWEIGHTSTRINGBUILDER_H

#include <cstddef>
#include <cstdint>

namespace SputterOS
{

/**
 * @file lightweightStringBuilder.h
 * @brief Heap-free, fixed-capacity string formatter for telemetry output.
 *
 * Formats floats, ints, and string literals into a single stack-allocated
 * buffer without using `sprintf`, `std::string`, or any heap allocation.
 *
 * Example:
 * @code
 *   LightweightStringBuilder sb;
 *   sb.append("STATE:").append(int32_t(3))
 *     .append(",P:").append(5.2e-3f, 3)
 *     .append("\n");
 *   // sb.c_str() == "STATE:3,P:0.005\n"
 * @endcode
 *
 * @note When the internal buffer is full, further `append()` calls are
 *       silently ignored. Check `isFull()` after bulk appends if needed.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/2026
 */
class LightweightStringBuilder
{
  public:
    /** @brief Fixed capacity of the internal character buffer (bytes). */
    static constexpr std::size_t kCapacity = 128;

    /**
     * @brief Construct an empty LightweightStringBuilder.
     */
    LightweightStringBuilder();

    /**
     * @brief Append a null-terminated string literal.
     * @param str: Null-terminated source string.
     * @return Reference to this builder for method chaining.
     */
    LightweightStringBuilder &append(const char *str);

    /**
     * @brief Append a signed 32-bit integer as a decimal string.
     * @param value: Integer to format.
     * @return Reference to this builder for method chaining.
     */
    LightweightStringBuilder &append(int32_t value);

    /**
     * @brief Append an unsigned 32-bit integer as a decimal string.
     * @param value: Integer to format.
     * @return Reference to this builder for method chaining.
     */
    LightweightStringBuilder &append(uint32_t value);

    /**
     * @brief Append a float formatted to a fixed number of decimal places.
     * @param value: Float to format.
     * @param decimals: Decimal places to render (0–6, default 3).
     * @return Reference to this builder for method chaining.
     */
    LightweightStringBuilder &append(float value, uint8_t decimals = 3);

    /**
     * @brief Append a single character.
     * @param c: Character to append.
     * @return Reference to this builder for method chaining.
     */
    LightweightStringBuilder &append(char c);

    /**
     * @brief Return a null-terminated pointer to the built string.
     * @return Pointer to the internal buffer.
     */
    const char *c_str() const;

    /**
     * @brief Return the current string length (excluding null terminator).
     * @return Number of characters written.
     */
    std::size_t length() const;

    /**
     * @brief Check whether the buffer is at capacity.
     * @return true if no more characters can be appended.
     */
    bool isFull() const;

    /**
     * @brief Reset the buffer to empty without any heap operation.
     */
    void clear();

  private:
    char        m_buf[kCapacity]; /**< @brief Null-terminated character storage. */
    std::size_t m_len;            /**< @brief Number of characters written so far. */
};

}; // namespace SputterOS
#endif // SPUTTEROS_UTILS_LIGHTWEIGHTSTRINGBUILDER_H