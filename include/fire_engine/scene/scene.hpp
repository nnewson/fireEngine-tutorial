#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fire_engine/scene/scene_draw_list.hpp>
#include <fire_engine/scene/scene_node.hpp>
#include <fire_engine/scene/scene_node_id.hpp>

namespace fire_engine
{
/* --- Type aliases --- */

/**
 * @brief Mutable scene-node reference stored inside a C++23 optional.
 *
 * C++23 cannot represent `std::optional<T&>`, so these aliases isolate its
 * `std::reference_wrapper` substitute. Optional references are accepted for C++26.
 * @see https://isocpp.org/blog/2025/11/cpp26-stdoptionalt-sandor-dargo
 */
using SceneNodeRef = std::reference_wrapper<SceneNode>;

/** @brief Read-only counterpart to SceneNodeRef. */
using SceneNodeConstRef = std::reference_wrapper<const SceneNode>;

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

    /**
     * @brief Registers new descendants and recomputes world transforms from the roots downward.
     */
    void updateWorldTransforms();

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

    /**
     * @brief Returns one node by its stable scene-local identity.
     * @param id ID assigned when the node's subtree entered this scene.
     * @return Mutable node reference, or no value when the ID does not belong to this scene.
     */
    [[nodiscard]] std::optional<SceneNodeRef> findNode(SceneNodeId id) noexcept;

    /**
     * @brief Returns one node by its stable scene-local identity.
     * @param id ID assigned when the node's subtree entered this scene.
     * @return Read-only node reference, or no value when the ID does not belong to this scene.
     */
    [[nodiscard]] std::optional<SceneNodeConstRef> findNode(SceneNodeId id) const noexcept;

private:
    /**
     * @brief Assigns IDs to unregistered nodes in one hierarchy.
     * @param node Root of the subtree inspected for new nodes.
     */
    void registerSubtree(SceneNode& node);

    std::vector<std::unique_ptr<SceneNode>> roots_; ///< Owned scene hierarchy.
    std::vector<SceneNode*> nodes_;                 ///< Dense lookup indexed by SceneNodeId.
};
} // namespace fire_engine
