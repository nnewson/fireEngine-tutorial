#pragma once

#include <cstddef>
#include <vector>

#include <fire_engine/graphics/image_data.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/graphics/render_object.hpp>
#include <fire_engine/graphics/texture.hpp>

namespace fire_engine
{
/* --- Classes --- */

/**
 * @brief Owns Vulkan-free render descriptions shared by scene instances.
 *
 * Images, textures, meshes, and materials are stored once. RenderObject connects
 * one mesh and material and can then be instanced by any number of SceneNodes.
 */
class RenderAssets final
{
public:
    RenderAssets() = default;
    ~RenderAssets() = default;

    RenderAssets(const RenderAssets&) = delete;
    RenderAssets& operator=(const RenderAssets&) = delete;
    /**
     * @brief Moves every render description from another collection.
     * @param other Collection whose contents are transferred.
     */
    RenderAssets(RenderAssets&& other) noexcept;
    /**
     * @brief Replaces this collection by moving another.
     * @param other Collection whose contents are transferred.
     * @return This collection with a newly incremented revision.
     */
    RenderAssets& operator=(RenderAssets&& other) noexcept;

    /**
     * @brief Adds one CPU mesh description.
     * @param mesh Mesh moved into this collection.
     * @return Stable ID of the inserted mesh.
     */
    [[nodiscard]] MeshId addMesh(Mesh mesh);

    /**
     * @brief Adds one decoded RGBA8 image.
     * @param image Image moved into this collection.
     * @return Stable ID of the inserted image.
     */
    [[nodiscard]] ImageId addImage(ImageData image);

    /**
     * @brief Adds one image and sampling description.
     * @param texture Texture copied into this collection.
     * @return Stable ID of the inserted texture.
     */
    [[nodiscard]] TextureId addTexture(Texture texture);

    /**
     * @brief Adds one CPU material description.
     * @param material Material copied into this collection.
     * @return Stable ID of the inserted material.
     */
    [[nodiscard]] MaterialId addMaterial(Material material);

    /**
     * @brief Adds one mesh/material relationship.
     * @param renderObject Typed references validated during renderer preparation.
     * @return Stable ID used by scene nodes.
     */
    [[nodiscard]] RenderObjectId addRenderObject(RenderObject renderObject);

    /** @brief Returns meshes in typed-ID order. @return Owned CPU mesh descriptions. */
    [[nodiscard]] const std::vector<Mesh>& meshes() const noexcept;

    /** @brief Returns images in typed-ID order. @return Owned decoded image data. */
    [[nodiscard]] const std::vector<ImageData>& images() const noexcept;

    /** @brief Returns textures in typed-ID order. @return Owned sampling descriptions. */
    [[nodiscard]] const std::vector<Texture>& textures() const noexcept;

    /** @brief Returns materials in typed-ID order. @return Owned CPU material descriptions. */
    [[nodiscard]] const std::vector<Material>& materials() const noexcept;

    /**
     * @brief Returns mesh/material relationships in typed-ID order.
     * @return Owned render-object descriptions.
     */
    [[nodiscard]] const std::vector<RenderObject>& renderObjects() const noexcept;

    /**
     * @brief Returns the revision of descriptions requiring GPU preparation.
     * @return Monotonically increasing revision changed by every insertion.
     */
    [[nodiscard]] std::size_t revision() const noexcept;

private:
    std::vector<Mesh> meshes_;                ///< CPU mesh descriptions.
    std::vector<ImageData> images_;           ///< Decoded RGBA8 pixel data.
    std::vector<Texture> textures_;           ///< Image and sampling relationships.
    std::vector<Material> materials_;         ///< CPU material descriptions.
    std::vector<RenderObject> renderObjects_; ///< Typed mesh/material relationships.
    std::size_t revision_ = 0;                ///< Incremented by every description insertion.
};
} // namespace fire_engine
