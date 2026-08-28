#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan_raii.hpp>

#include <fire_engine/graphics/color4.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/pipeline_description.hpp>
#include <fire_engine/render/detail/buffer.hpp>
#include <fire_engine/render/detail/image.hpp>

namespace fire_engine
{
struct ImageData;
struct Texture;

namespace detail
{
class Device;
class MemoryAllocator;

/** @cond INTERNAL */
/* --- Classes --- */

/** @brief GPU buffers and retained layout proof compiled from one CPU mesh. */
class CompiledMesh final
{
public:
    /**
     * @brief Uploads one validated CPU mesh into vertex and index buffers.
     * @param allocator VMA owner used to create both buffers.
     * @param mesh Validated CPU geometry copied into the buffers.
     */
    CompiledMesh(const MemoryAllocator& allocator, const Mesh& mesh);

    /** @brief Releases the uploaded vertex and index buffers. */
    ~CompiledMesh() = default;

    /// @brief Copy construction is disabled because Vulkan allocations have unique ownership.
    CompiledMesh(const CompiledMesh&) = delete;
    /// @brief Copy assignment is disabled because Vulkan allocations have unique ownership.
    CompiledMesh& operator=(const CompiledMesh&) = delete;
    /// @brief Move construction is disabled so graph borrower addresses remain stable.
    CompiledMesh(CompiledMesh&&) = delete;
    /// @brief Move assignment is disabled so graph borrower addresses remain stable.
    CompiledMesh& operator=(CompiledMesh&&) = delete;

    /** @brief Returns the uploaded vertex buffer. @return Vertex-buffer allocation. */
    [[nodiscard]] const AllocatedBuffer& vertexBuffer() const noexcept;

    /** @brief Returns the uploaded index buffer. @return Index-buffer allocation. */
    [[nodiscard]] const AllocatedBuffer& indexBuffer() const noexcept;

    /** @brief Returns the uploaded index count. @return Number consumed by drawIndexed. */
    [[nodiscard]] std::uint32_t indexCount() const noexcept;

    /** @brief Returns the retained layout proof. @return Vulkan-free vertex-layout key. */
    [[nodiscard]] VertexLayoutKey vertexLayout() const noexcept;

private:
    AllocatedBuffer vertexBuffer_; ///< GPU buffer containing tightly packed vertices.
    AllocatedBuffer indexBuffer_;  ///< GPU buffer containing 32-bit triangle indices.
    std::uint32_t indexCount_;     ///< Number of indices consumed by drawIndexed.
    VertexLayoutKey vertexLayout_; ///< Layout proved compatible during preparation.
};

/** @brief Device-local sampled image compiled from decoded RGBA8 pixels. */
class CompiledImage final
{
public:
    /**
     * @brief Allocates an image and creates its color view.
     * @param device Logical device that owns the image view.
     * @param allocator VMA owner used for the image allocation.
     * @param source Validated decoded image whose extent is retained.
     */
    CompiledImage(const Device& device, const MemoryAllocator& allocator, const ImageData& source);

    /** @brief Releases the image view before its image allocation. */
    ~CompiledImage() = default;

    /// @brief Copy construction is disabled because Vulkan allocations have unique ownership.
    CompiledImage(const CompiledImage&) = delete;
    /// @brief Copy assignment is disabled because Vulkan allocations have unique ownership.
    CompiledImage& operator=(const CompiledImage&) = delete;
    /// @brief Move construction is disabled so texture borrower addresses remain stable.
    CompiledImage(CompiledImage&&) = delete;
    /// @brief Move assignment is disabled so texture borrower addresses remain stable.
    CompiledImage& operator=(CompiledImage&&) = delete;

    /** @brief Returns the allocated Vulkan image. @return Non-owning image handle. */
    [[nodiscard]] vk::Image image() const noexcept;

    /** @brief Returns the shader-visible color view. @return Owned image view. */
    [[nodiscard]] const vk::raii::ImageView& view() const noexcept;

private:
    AllocatedImage image_;     ///< Device-local image and allocation.
    vk::raii::ImageView view_; ///< Single-mip color view into image_.
};

/** @brief Sampling state borrowing one compiled image in the same graph. */
class CompiledTexture final
{
public:
    /**
     * @brief Creates sampling state for a compiled texture description.
     * @param device Logical device that owns the sampler.
     * @param image Compiled image that must outlive this texture.
     * @param source Validated filtering and addressing description.
     */
    CompiledTexture(const Device& device, const CompiledImage& image, const Texture& source);

    /** @brief Releases the sampler without affecting its borrowed image. */
    ~CompiledTexture() = default;

    /// @brief Copy construction is disabled because Vulkan handles have unique ownership.
    CompiledTexture(const CompiledTexture&) = delete;
    /// @brief Copy assignment is disabled because Vulkan handles have unique ownership.
    CompiledTexture& operator=(const CompiledTexture&) = delete;
    /// @brief Move construction is disabled so render-object borrower addresses remain stable.
    CompiledTexture(CompiledTexture&&) = delete;
    /// @brief Move assignment is disabled so render-object borrower addresses remain stable.
    CompiledTexture& operator=(CompiledTexture&&) = delete;

    /** @brief Returns the sampled image. @return Borrowed compiled image. */
    [[nodiscard]] const CompiledImage& image() const noexcept;

    /** @brief Returns the Vulkan sampler. @return Owned sampler. */
    [[nodiscard]] const vk::raii::Sampler& sampler() const noexcept;

private:
    const CompiledImage* image_ = nullptr; ///< Borrowed image retained by graph ordering.
    vk::raii::Sampler sampler_;            ///< Filtering and addressing state.
};

/* --- POD structs --- */

/** @brief Prepared draw lookup retaining borrowers into one compiled graph. */
struct CompiledRenderObject
{
    const CompiledMesh* mesh = nullptr;       ///< Shared compiled geometry.
    const CompiledTexture* texture = nullptr; ///< Sampled base-color texture.
    Color4 baseColor{};                       ///< Material factor pushed for each draw.
    VertexLayoutKey vertexLayout =
        VertexLayoutKey::ePositionColorTextureCoordinate; ///< Retained compatibility proof.
};

/**
 * @brief Complete stable ownership graph produced as one compilation candidate.
 *
 * Declaration order is part of the lifetime contract. Reverse destruction
 * releases render-object borrowers first, then meshes, every texture, and
 * finally every image borrowed by those textures.
 */
struct CompiledResourceGraph
{
    std::vector<std::unique_ptr<CompiledImage>> images;     ///< Dense ImageId lookup.
    std::shared_ptr<CompiledImage> fallbackImage;           ///< Persistent one-pixel white image.
    std::vector<std::unique_ptr<CompiledTexture>> textures; ///< Dense TextureId lookup.
    std::shared_ptr<CompiledTexture> fallbackTexture;       ///< Sampler paired with fallbackImage.
    std::vector<std::unique_ptr<CompiledMesh>> meshes;      ///< Dense MeshId lookup.
    std::vector<CompiledRenderObject> objects;              ///< Dense RenderObjectId draw lookup.
};
/** @endcond */
} // namespace detail
} // namespace fire_engine
