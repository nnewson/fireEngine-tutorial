#include <fire_engine/scene/scene.hpp>

#include <fire_engine/core/hash.hpp>

#include <functional>
#include <stdexcept>
#include <utility>

namespace fire_engine
{
/* --- Public member functions --- */

SceneNode& Scene::addRoot(std::unique_ptr<SceneNode> root)
{
    if (!root)
    {
        throw std::invalid_argument("A scene root cannot be null");
    }

    SceneNode& result = *root;
    roots_.push_back(std::move(root));
    registerSubtree(result);
    return result;
}

SceneNode& Scene::addRoot(std::string name)
{
    return addRoot(std::make_unique<SceneNode>(std::move(name)));
}

void Scene::updateWorldTransforms()
{
    for (const std::unique_ptr<SceneNode>& root : roots_)
    {
        registerSubtree(*root);
        root->resolve(Mat4::identity());
    }
}

SceneDrawList Scene::buildDrawItems() const
{
    SceneDrawList output;
    for (const std::unique_ptr<SceneNode>& root : roots_)
    {
        root->appendDrawItems(output.drawItems);
    }

    // Hash only preparation dependencies. Exact IDs are retained in drawItems
    // so RenderPreparation can still prove equality after this fast check.
    constexpr std::size_t kHashCombineConstant = static_cast<std::size_t>(
        sizeof(std::size_t) == sizeof(std::uint64_t) ? hash::k64BitGoldenRatio
                                                     : hash::k32BitGoldenRatio);
    output.dependencyHash = output.drawItems.size();
    for (const DrawItem& drawItem : output.drawItems)
    {
        const std::size_t value = std::hash<std::size_t>{}(drawItem.renderObject.value);
        output.dependencyHash ^= value + kHashCombineConstant + (output.dependencyHash << 6U) +
                                 (output.dependencyHash >> 2U);
    }
    return output;
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

void Scene::registerSubtree(SceneNode& node)
{
    if (!node.id().has_value())
    {
        node.assignId(SceneNodeId{.value = nodes_.size()});
        nodes_.push_back(&node);
    }

    for (const std::unique_ptr<SceneNode>& child : node.children())
    {
        registerSubtree(*child);
    }
}

} // namespace fire_engine
