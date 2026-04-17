/**
 * @file LightweightStringBuilder.cpp
 * @brief Lightweight string builder implementation for fixed-size buffers.
 *
 * Provides simple numeric and string append operations for text generation
 * without dynamic allocation.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/5/26
 */

#include "sputteros/utils/logging/LightweightStringBuilder.h"

namespace SputterOS
{

// =========================================================================
// Private helpers for snprintf-free numeric formatting
// =========================================================================

namespace
{

/**
 * @brief Render unsigned 32-bit integer into tmp[].
 * @return Number of characters written (no null terminator).
 */
int renderU32(char *tmp, std::size_t tmpLen, uint32_t val)
{
    if (val == 0)
    {
        tmp[0] = '0';
        return 1;
    }
    int idx = 0;
    while (val > 0 && idx < static_cast<int>(tmpLen))
    {
        tmp[idx++] = '0' + static_cast<char>(val % 10);
        val /= 10;
    }
    // Reverse in place
    for (int i = 0, j = idx - 1; i < j; ++i, --j)
    {
        char c  = tmp[i];
        tmp[i]  = tmp[j];
        tmp[j]  = c;
    }
    return idx;
}

} // anonymous namespace

LightweightStringBuilder::LightweightStringBuilder() : m_len(0) { m_buf[0] = '\0'; }

LightweightStringBuilder &LightweightStringBuilder::append(const char *str)
{
    if (!str)
    {
        return *this;
    }
    while (*str && m_len < kCapacity - 1)
    {
        m_buf[m_len++] = *str++;
    }
    m_buf[m_len] = '\0';
    return *this;
}

LightweightStringBuilder &LightweightStringBuilder::append(int32_t value)
{
    char tmp[16];
    bool negative = value < 0;
    uint32_t uval = negative ? static_cast<uint32_t>(-value) : static_cast<uint32_t>(value);
    int len = renderU32(tmp, sizeof(tmp), uval);
    if (negative)
    {
        append('-');
    }
    for (int i = 0; i < len && m_len < kCapacity - 1; ++i)
    {
        m_buf[m_len++] = tmp[i];
    }
    m_buf[m_len] = '\0';
    return *this;
}

LightweightStringBuilder &LightweightStringBuilder::append(uint32_t value)
{
    char tmp[16];
    int len = renderU32(tmp, sizeof(tmp), value);
    for (int i = 0; i < len && m_len < kCapacity - 1; ++i)
    {
        m_buf[m_len++] = tmp[i];
    }
    m_buf[m_len] = '\0';
    return *this;
}

LightweightStringBuilder &LightweightStringBuilder::append(float value, uint8_t decimals)
{
    if (value < 0.0f)
    {
        append('-');
        value = -value;
    }

    // Round to the requested number of decimal places
    // For decimals==0, this rounds to nearest integer (e.g. 5.9 -> 6)
    float scale = 1.0f;
    for (uint8_t d = 0; d < decimals; ++d)
    {
        scale *= 10.0f;
    }
    value = static_cast<float>(static_cast<uint32_t>(value * scale + 0.5f)) / scale;

    auto intPart = static_cast<uint32_t>(value);
    char tmp[16];
    int len = renderU32(tmp, sizeof(tmp), intPart);
    for (int i = 0; i < len && m_len < kCapacity - 1; ++i)
    {
        m_buf[m_len++] = tmp[i];
    }
    m_buf[m_len] = '\0';

    if (decimals > 0)
    {
        append('.');
        float frac = value - static_cast<float>(intPart);
        // Scale fractional part to the requested number of decimal places
        float scale = 1.0f;
        for (uint8_t d = 0; d < decimals; ++d)
        {
            scale *= 10.0f;
        }
        auto fracInt = static_cast<uint32_t>(frac * scale + 0.5f);
        // Clamp to max representable (e.g. 999 for 3 decimals)
        auto maxFrac = static_cast<uint32_t>(scale) - 1;
        if (fracInt > maxFrac)
            fracInt = maxFrac;
        // Leading zeros
        for (uint8_t d = 1; d < decimals; ++d)
        {
            uint32_t threshold = 1;
            for (uint8_t k = 0; k < decimals - d; ++k)
                threshold *= 10;
            if (fracInt < threshold)
                append('0');
        }
        len = renderU32(tmp, sizeof(tmp), fracInt);
        for (int i = 0; i < len && m_len < kCapacity - 1; ++i)
        {
            m_buf[m_len++] = tmp[i];
        }
        m_buf[m_len] = '\0';
    }
    return *this;
}

LightweightStringBuilder &LightweightStringBuilder::append(char c)
{
    if (m_len < kCapacity - 1)
    {
        m_buf[m_len++] = c;
        m_buf[m_len]   = '\0';
    }
    return *this;
}

const char *LightweightStringBuilder::c_str() const { return m_buf; }

std::size_t LightweightStringBuilder::length() const { return m_len; }

bool LightweightStringBuilder::isFull() const { return m_len >= kCapacity - 1; }

void LightweightStringBuilder::clear()
{
    m_len    = 0;
    m_buf[0] = '\0';
}

} // namespace SputterOS
