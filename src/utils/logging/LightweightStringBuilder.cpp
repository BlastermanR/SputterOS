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
#include <cstdio>
#include <cstring>

namespace SputterOS
{

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
    std::snprintf(tmp, sizeof(tmp), "%d", static_cast<int>(value));
    return append(tmp);
}

LightweightStringBuilder &LightweightStringBuilder::append(uint32_t value)
{
    char tmp[16];
    std::snprintf(tmp, sizeof(tmp), "%u", static_cast<unsigned>(value));
    return append(tmp);
}

LightweightStringBuilder &LightweightStringBuilder::append(float value, uint8_t decimals)
{
    char fmt[8];
    std::snprintf(fmt, sizeof(fmt), "%%.%df", static_cast<int>(decimals));
    char tmp[32];
    std::snprintf(tmp, sizeof(tmp), fmt, static_cast<double>(value));
    return append(tmp);
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
