#ifndef SPUTTEROS_KERNEL_KERNEL_CONSTRUCT_TAG_H
#define SPUTTEROS_KERNEL_KERNEL_CONSTRUCT_TAG_H

/**
 * @file KernelConstructTag.h
 * @brief PassKey type gating kernel task construction.
 *
 * Kernel task constructors are public but take a `KernelConstructTag` as
 * their first argument. Because `KernelConstructTag` itself has a private
 * default constructor, only declared friends (`SystemBuilder` and
 * `KernelTestAccess`) can create an instance and therefore construct
 * kernel tasks.
 *
 * This sidesteps the `std::optional::emplace` limitation, which calls
 * the target constructor from within the standard library — outside the
 * scope where friendship with the kernel task would apply.
 *
 * @author Ryan Massie (rmassie)
 * @date 4/8/2026
 */

// Forward declarations for friend list.
namespace SputterOS
{
template <typename Cfg> class SystemBuilder;
}

namespace SputterOS::Kernel
{
struct KernelTestAccess;
}

namespace SputterOS::Kernel
{

/**
 * @brief Opaque access token whose constructor is private.
 *
 * Only `SystemBuilder<Cfg>` and `KernelTestAccess` may default-construct
 * this type, enforcing that kernel tasks are only instantiated through
 * authorised code paths.
 */
struct KernelConstructTag
{
  private:
    KernelConstructTag() = default;

    template <typename Cfg> friend class SputterOS::SystemBuilder;
    friend struct KernelTestAccess;
};

} // namespace SputterOS::Kernel

#endif // SPUTTEROS_KERNEL_KERNEL_CONSTRUCT_TAG_H
