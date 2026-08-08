#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fire_engine/graphics/draw_item.hpp>
#include <fire_engine/graphics/render_ids.hpp>
#include <fire_engine/math/mat4.hpp>
#include <fire_engine/math/transform.hpp>
#include <fire_engine/scene/scene_node_id.hpp>

namespace fire_engine
{
/* --- Classes --- */

#if defined(_MSC_VER)
// The world-transform Mat4 carries 16-byte shader-compatible alignment.
// SceneNode combines it with smaller standard-library members, so its final
// size requires intentional padding that MSVC reports as C4324.
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/** @brief Owns one transformable node in the tutorial scene hierarchy. */
class SceneNode final
{
public:
    /**
     * @brief Creates a node with an identity local transform.
     * @param name Human-readable name used by diagnostics and future importers.
     */
    explicit SceneNode(std::string name = {});

    /** @brief Releases every child node recursively. */
    ~SceneNode() = default;

    SceneNode(const SceneNode&) = delete;
    SceneNode& operator=(const SceneNode&) = delete;
    // Registered nodes are addressed by stable pointers, so the node itself is immovable.
    SceneNode(SceneNode&&) = delete;
    SceneNode& operator=(SceneNode&&) = delete;

    /**
     * @brief Returns the stable ID assigned when this node enters a scene.
     * @return Scene-local ID, or no value before scene registration.
     *
     * The optional records whether this node has entered a Scene. SceneNodeId retains its
     * invalid sentinel separately so findNode() can reject fabricated default IDs.
     */
    [[nodiscard]] std::optional<SceneNodeId> id() const noexcept;

    /** @brief Returns this node's diagnostic name. @return Name supplied at construction. */
    [[nodiscard]] const std::string& name() const noexcept;

    /**
     * @brief Returns the decomposed transform relative to this node's parent.
     * @return Translation, rotation, and scale used to build the local matrix.
     */
    [[nodiscard]] const Transform& localTransform() const noexcept;

    /**
     * @brief Replaces the transform relative to this node's parent.
     * @param transform New decomposed local transform.
     */
    void localTransform(const Transform& transform) noexcept;

    /** @brief Returns the resolved object-to-world transform. @return Cached world matrix. */
    [[nodiscard]] const Mat4& worldTransform() const noexcept;

    /**
     * @brief Returns the optional render object attached to this node.
     * @return Attached object ID, or no value for a transform-only node.
     */
    [[nodiscard]] std::optional<RenderObjectId> renderObject() const noexcept;

    /**
     * @brief Attaches one render object to this node.
     * @param renderObject Object emitted during draw traversal.
     */
    void renderObject(RenderObjectId renderObject) noexcept;

    /** @brief Removes any render object attached to this node. */
    void clearRenderObject() noexcept;

    /**
     * @brief Adds an owned child node.
     * @param child Node whose lifetime becomes part of this subtree.
     * @return Reference stable until this parent is destroyed.
     * @throws std::invalid_argument if child is null.
     */
    SceneNode& addChild(std::unique_ptr<SceneNode> child);

    /**
     * @brief Creates and adds one named child node.
     * @param name Name assigned to the new child.
     * @return Reference stable until this parent is destroyed.
     */
    SceneNode& addChild(std::string name);

    /**
     * @brief Returns this node's owned children in insertion order.
     * @return Child hierarchy owned by this node.
     */
    [[nodiscard]] const std::vector<std::unique_ptr<SceneNode>>& children() const noexcept;

private:
    friend class Scene;

    /**
     * @brief Assigns this node its dense scene-local identity.
     * @param id ID selected by the owning scene.
     */
    void assignId(SceneNodeId id) noexcept;

    /** @brief Resolves this subtree. @param parentWorld Resolved transform of the parent. */
    void resolve(const Mat4& parentWorld) noexcept;
    /** @brief Appends this subtree's visuals. @param output Draw list receiving depth-first items.
     */
    void appendDrawItems(std::vector<DrawItem>& output) const;

    std::string name_;                                 ///< Human-readable node name.
    std::optional<SceneNodeId> id_;                    ///< Identity assigned by the owning scene.
    Transform localTransform_;                         ///< Transform relative to the parent.
    Mat4 worldTransform_ = Mat4::identity();           ///< Cached resolved world transform.
    std::optional<RenderObjectId> renderObject_;       ///< Optional visual attached to this node.
    std::vector<std::unique_ptr<SceneNode>> children_; ///< Owned child hierarchy.
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif
} // namespace fire_engine
