#include <fire_engine/scene/scene.hpp>

#include <fire_engine/core/detail/hash.hpp>

#include <algorithm>
#include <functional>
#include <limits>
#include <stdexcept>
#include <utility>

namespace fire_engine
{
namespace
{
/** @cond INTERNAL */
/* --- File-local function declarations --- */

/**
 * @brief Selects sufficient geometric capacity for a prepared registry insertion.
 * @param currentCapacity Capacity already available in the registry.
 * @param requiredCapacity Capacity needed by the complete incoming subtree.
 * @param maximumSize Largest capacity supported by the registry.
 * @return Existing capacity when sufficient, otherwise the larger of doubled and required
 * capacity, saturated at maximumSize.
 */
[[nodiscard]] constexpr std::size_t nextRegistryCapacity(std::size_t currentCapacity,
                                                         std::size_t requiredCapacity,
                                                         std::size_t maximumSize) noexcept;

/**
 * @brief Counts one detached subtree while proving that none of its nodes have scene identities.
 * @param node Root of the subtree inspected recursively.
 * @return Number of nodes in the complete subtree.
 * @throws std::invalid_argument if any node is already registered.
 * @throws std::length_error if the subtree size cannot be represented.
 */
[[nodiscard]] std::size_t countDetachedNodes(const SceneNode& node);
/** @endcond */
} // namespace

/* --- Public member functions --- */

SceneNode& Scene::addRoot(std::unique_ptr<SceneNode> root)
{
    if (!root)
    {
        throw std::invalid_argument("A scene root cannot be null");
    }

    SceneNode& result = *root;
    prepareSubtreeRegistration(result);
    roots_.push_back(std::move(root));
    registerSubtree(result);
    return result;
}

SceneNode& Scene::addRoot(std::string name)
{
    return addRoot(std::make_unique<SceneNode>(std::move(name)));
}

SceneNode& Scene::addChild(SceneNodeId parent, std::unique_ptr<SceneNode> child)
{
    if (!child)
    {
        throw std::invalid_argument("A scene child cannot be null");
    }
    if (!parent.valid() || parent.value >= nodes_.size())
    {
        throw std::invalid_argument("The child parent ID does not belong to this scene");
    }

    prepareSubtreeRegistration(*child);
    SceneNode& result = nodes_[parent.value]->attachChild(std::move(child));
    registerSubtree(result);
    return result;
}

SceneNode& Scene::addChild(SceneNode& parent, std::unique_ptr<SceneNode> child)
{
    if (!child)
    {
        throw std::invalid_argument("A scene child cannot be null");
    }

    const std::optional<SceneNodeId> parentId = parent.id();
    if (!parentId.has_value() || !parentId->valid() || parentId->value >= nodes_.size() ||
        nodes_[parentId->value] != &parent)
    {
        throw std::invalid_argument("The child parent node does not belong to this scene");
    }

    return addChild(*parentId, std::move(child));
}

void Scene::updateWorldTransforms()
{
    for (const std::unique_ptr<SceneNode>& root : roots_)
    {
        root->resolve(Mat4::identity());
    }
}

SceneDrawList Scene::buildDrawItems(SceneDrawListArena& arena) const
{
    arena.drawItems_.clear();
    for (const std::unique_ptr<SceneNode>& root : roots_)
    {
        root->appendDrawItems(arena.drawItems_);
    }

    // Hash only preparation dependencies. Exact IDs are retained in drawItems
    // so RenderPreparation can still prove equality after this fast check.
    constexpr std::size_t kHashCombineConstant = static_cast<std::size_t>(
        sizeof(std::size_t) == sizeof(std::uint64_t) ? detail::k64BitGoldenRatio
                                                     : detail::k32BitGoldenRatio);
    std::size_t dependencyHash = arena.drawItems_.size();
    for (const DrawItem& drawItem : arena.drawItems_)
    {
        const std::size_t value = std::hash<std::size_t>{}(drawItem.renderObject.value);
        dependencyHash ^=
            value + kHashCombineConstant + (dependencyHash << 6U) + (dependencyHash >> 2U);
    }
    return {
        .drawItems = arena.drawItems_,
        .dependencyHash = dependencyHash,
    };
}

const std::vector<std::unique_ptr<SceneNode>>& Scene::roots() const noexcept
{
    return roots_;
}

std::optional<SceneNodeRef> Scene::findNode(SceneNodeId id) noexcept
{
    if (!id.valid() || id.value >= nodes_.size())
    {
        return std::nullopt;
    }
    return std::ref(*nodes_[id.value]);
}

std::optional<SceneNodeConstRef> Scene::findNode(SceneNodeId id) const noexcept
{
    if (!id.valid() || id.value >= nodes_.size())
    {
        return std::nullopt;
    }
    return std::cref(*nodes_[id.value]);
}

/* --- Private member functions --- */

void Scene::prepareSubtreeRegistration(const SceneNode& node)
{
    const std::size_t subtreeSize = countDetachedNodes(node);
    const std::size_t maximumSize = nodes_.max_size();
    if (subtreeSize > maximumSize - nodes_.size())
    {
        throw std::length_error("A scene cannot register this many nodes");
    }

    const std::size_t requiredCapacity = nodes_.size() + subtreeSize;
    nodes_.reserve(nextRegistryCapacity(nodes_.capacity(), requiredCapacity, maximumSize));
}

void Scene::registerSubtree(SceneNode& node)
{
    node.assignId(SceneNodeId{.value = nodes_.size()});
    nodes_.push_back(&node);

    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        registerSubtree(*child);
    }
}

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[nodiscard]] constexpr std::size_t nextRegistryCapacity(std::size_t currentCapacity,
                                                         std::size_t requiredCapacity,
                                                         std::size_t maximumSize) noexcept
{
    if (requiredCapacity <= currentCapacity)
    {
        return currentCapacity;
    }

    const std::size_t doubledCapacity =
        currentCapacity > maximumSize / 2 ? maximumSize : currentCapacity * 2;
    return std::max(requiredCapacity, doubledCapacity);
}

/** @brief Representative upper bound used to prove registry growth at compile time. */
constexpr std::size_t kCapacityPolicyMaximum = std::numeric_limits<std::size_t>::max();

static_assert(nextRegistryCapacity(0, 1, kCapacityPolicyMaximum) == 1);
static_assert(nextRegistryCapacity(8, 5, kCapacityPolicyMaximum) == 8);
static_assert(nextRegistryCapacity(1, 2, kCapacityPolicyMaximum) == 2);
static_assert(nextRegistryCapacity(4, 5, kCapacityPolicyMaximum) == 8);
static_assert(nextRegistryCapacity(4, 100, kCapacityPolicyMaximum) == 100);
static_assert(nextRegistryCapacity(6, 7, 10) == 10);
static_assert(nextRegistryCapacity(kCapacityPolicyMaximum / 2 + 1, kCapacityPolicyMaximum,
                                   kCapacityPolicyMaximum) == kCapacityPolicyMaximum);

[[nodiscard]] std::size_t countDetachedNodes(const SceneNode& node)
{
    if (node.id().has_value())
    {
        throw std::invalid_argument("A scene subtree cannot contain already registered nodes");
    }

    std::size_t count = 1;
    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        const std::size_t childCount = countDetachedNodes(*child);
        if (childCount > std::numeric_limits<std::size_t>::max() - count)
        {
            throw std::length_error("A scene subtree contains too many nodes");
        }
        count += childCount;
    }
    return count;
}
/** @endcond */
} // namespace

} // namespace fire_engine
