#pragma once

#include <memory>
#include <string>
#include <vector>

#include <fire_engine/scene/scene_draw_list.hpp>
#include <fire_engine/scene/scene_node.hpp>

namespace fire_engine
{
/* --- Classes --- */

/**
 * @brief Owns one Vulkan-free forest of transformable scene nodes.
 *
 * One scene may have several roots, matching formats such as glTF without an
 * artificial identity node. Render descriptions live separately in
 * RenderAssets and are referenced by typed IDs attached to SceneNodes.
 */
class Scene final
{
public:
    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    /** @brief Moves the complete hierarchy ownership from another scene. */
    Scene(Scene&&) noexcept = default;
    /** @brief Replaces this scene by moving another scene's contents. @return This scene. */
    Scene& operator=(Scene&&) noexcept = default;

    /**
     * @brief Adds an owned root node.
     * @param root Node whose lifetime becomes part of this scene.
     * @return Reference stable until the scene is destroyed.
     * @throws std::invalid_argument if root is null.
     */
    SceneNode& addRoot(std::unique_ptr<SceneNode> root);

    /**
     * @brief Creates and adds one named root node.
     * @param name Human-readable name assigned to the new root.
     * @return Reference stable until this scene is destroyed.
     */
    SceneNode& addRoot(std::string name);

    /** @brief Recomputes every cached world transform from the roots downward. */
    void updateWorldTransforms() noexcept;

    /**
     * @brief Builds this frame's draws in stable depth-first order.
     * @return Current draws plus a transform-independent dependency hash.
     */
    [[nodiscard]] SceneDrawList buildDrawItems() const;

    /**
     * @brief Returns the owned root nodes in insertion order.
     * @return Scene hierarchy roots.
     */
    [[nodiscard]] const std::vector<std::unique_ptr<SceneNode>>& roots() const noexcept;

private:
    std::vector<std::unique_ptr<SceneNode>> roots_; ///< Owned scene hierarchy.
};
} // namespace fire_engine
