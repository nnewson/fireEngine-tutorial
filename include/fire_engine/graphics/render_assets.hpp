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
 *
 * Each catalog has a process-unique identity that follows its content. Within
 * one identity the content is append-only: an entry, once added, keeps its ID
 * and never changes, so preparation and drawing may rely on it at every later
 * revision. No catalog function mutates an existing entry or returns a
 * writable alias. Callers must also not mutate storage they transferred into
 * the catalog, for example through a pointer kept from a moved vector; the
 * catalog cannot detect such a write.
 */
class RenderAssets final
{
public:
    /** @brief Creates an empty collection with a new identity at revision zero. */
    RenderAssets() noexcept;
    ~RenderAssets() = default;

    RenderAssets(const RenderAssets&) = delete;
    RenderAssets& operator=(const RenderAssets&) = delete;
    /**
     * @brief Moves every render description, with its identity and revision, from another
     * collection.
     * @param other Collection left empty, with a new identity and revision zero.
     */
    RenderAssets(RenderAssets&& other) noexcept;
    /**
     * @brief Replaces this collection's content, identity, and revision with another's.
     * @param other Collection left empty, with a new identity and revision zero.
     * @return This collection. Its previous identity is never issued again.
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
     * @brief Returns the identity of this collection's content history.
     * @return Process-unique ID, carried by moves and never issued twice.
     */
    [[nodiscard]] RenderAssetsId id() const noexcept;

    /**
     * @brief Returns the revision of descriptions requiring GPU preparation.
     * @return Insertions into this identity's content history, starting at zero.
     */
    [[nodiscard]] std::size_t revision() const noexcept;

    /**
     * @brief Returns this collection's identity and revision together.
     * @return Stamp describing exactly the content currently held.
     */
    [[nodiscard]] RenderAssetsStamp stamp() const noexcept;

private:
    /** @brief Empties a moved-from collection and starts a new identity at revision zero. */
    void restart() noexcept;

    std::vector<Mesh> meshes_;                ///< CPU mesh descriptions.
    std::vector<ImageData> images_;           ///< Decoded RGBA8 pixel data.
    std::vector<Texture> textures_;           ///< Image and sampling relationships.
    std::vector<Material> materials_;         ///< CPU material descriptions.
    std::vector<RenderObject> renderObjects_; ///< Typed mesh/material relationships.
    RenderAssetsId id_;                       ///< Identity of this content history.
    std::size_t revision_ = 0;                ///< Incremented by every description insertion.
};
} // namespace fire_engine
