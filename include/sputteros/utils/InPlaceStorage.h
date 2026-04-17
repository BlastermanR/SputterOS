#ifndef SPUTTEROS_UTILS_INPLACESTORAGE_H
#define SPUTTEROS_UTILS_INPLACESTORAGE_H

/**
 * @file InPlaceStorage.h
 * @brief Aligned-storage wrapper — replaces `std::optional` for kernel tasks.
 *
 * Provides `InPlaceStorage<T>` with `emplace()`, `reset()`, and `operator->`
 * semantics similar to `std::optional`, but backed by raw aligned storage
 * and an alive flag. No `<optional>` dependency.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/16/2026
 */

#include <cstddef>
#include <cstdint>
#include <new>

namespace SputterOS
{

template <typename T> class InPlaceStorage
{
  public:
    InPlaceStorage() : m_alive(false) {}

    ~InPlaceStorage() { reset(); }

    InPlaceStorage(const InPlaceStorage &)            = delete;
    InPlaceStorage &operator=(const InPlaceStorage &) = delete;

    template <typename... Args> T &emplace(Args &&...args)
    {
        reset();
        ::new (storage()) T(static_cast<Args &&>(args)...);
        m_alive = true;
        return *ptr();
    }

    void reset()
    {
        if (m_alive)
        {
            ptr()->~T();
            m_alive = false;
        }
    }

    bool     has_value() const { return m_alive; }
    explicit operator bool() const { return m_alive; }

    T       *operator->() { return ptr(); }
    const T *operator->() const { return ptr(); }
    T       &operator*() { return *ptr(); }
    const T &operator*() const { return *ptr(); }

  private:
    alignas(T) uint8_t m_storage[sizeof(T)];
    bool m_alive;

    void       *storage() { return static_cast<void *>(m_storage); }
    const void *storage() const { return static_cast<const void *>(m_storage); }
    T          *ptr() { return static_cast<T *>(storage()); }
    const T    *ptr() const { return static_cast<const T *>(storage()); }
};

} // namespace SputterOS

#endif // SPUTTEROS_UTILS_INPLACESTORAGE_H
