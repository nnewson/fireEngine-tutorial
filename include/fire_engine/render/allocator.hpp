#pragma once

/* --- External forward declarations --- */

struct VmaAllocator_T;

namespace fire_engine
{
/* --- Forward declarations --- */

class Device;

/* --- Classes --- */

/**
 * @brief Owns the Vulkan Memory Allocator associated with one logical device.
 *
 * The allocator does not own the Vulkan handles supplied by Device, so it must
 * be destroyed before that Device. VMA allocations must also be released before
 * this owner. Creation targets Vulkan 1.4 and leaves optional VMA feature
 * flags disabled until a resource requires one.
 */
class MemoryAllocator final
{
public:
    /**
     * @brief Creates a VMA allocator for an initialized Vulkan device.
     *
     * VMA uses the loader already linked by the application rather than loading
     * a second set of Vulkan function pointers.
     *
     * @param device Vulkan objects that remain valid for this allocator's lifetime.
     * @throws std::runtime_error if VMA cannot create the allocator.
     */
    explicit MemoryAllocator(const Device& device);

    /** @brief Destroys the VMA allocator after all of its allocations are released. */
    ~MemoryAllocator();

    /// @brief Copy construction is disabled because the VMA allocator has one owner.
    MemoryAllocator(const MemoryAllocator&) = delete;
    /// @brief Copy assignment is disabled because the VMA allocator has one owner.
    MemoryAllocator& operator=(const MemoryAllocator&) = delete;
    /// @brief Move construction is disabled to keep lifetime ordering explicit.
    MemoryAllocator(MemoryAllocator&&) = delete;
    /// @brief Move assignment is disabled to keep lifetime ordering explicit.
    MemoryAllocator& operator=(MemoryAllocator&&) = delete;

    /**
     * @brief Returns the VMA allocator used to create device memory allocations.
     * @return Opaque allocator handle owned by this object.
     */
    [[nodiscard]] VmaAllocator_T* handle() const noexcept;

private:
    VmaAllocator_T* allocator_ = nullptr; ///< Opaque VMA allocator owned by this object.
};
} // namespace fire_engine
