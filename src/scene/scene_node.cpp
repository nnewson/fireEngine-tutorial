#include <fire_engine/scene/scene_node.hpp>

#include <stdexcept>
#include <utility>

namespace fire_engine
{
/* --- Public member functions --- */

SceneNode::SceneNode(std::string name)
    : name_{std::move(name)}
{
}

const std::string& SceneNode::name() const noexcept
{
    return name_;
}

std::optional<SceneNodeId> SceneNode::id() const noexcept
{
    return id_;
}

const Transform& SceneNode::localTransform() const noexcept
{
    return localTransform_;
}

void SceneNode::localTransform(const Transform& transform) noexcept
{
    localTransform_ = transform;
}

const Mat4& SceneNode::worldTransform() const noexcept
{
    return worldTransform_;
}

const SceneComponent& SceneNode::component() const noexcept
{
    return component_;
}

void SceneNode::component(SceneComponent component) noexcept
{
    component_ = component;
}

SceneNode& SceneNode::addChild(std::unique_ptr<SceneNode> child)
{
    if (!child)
    {
        throw std::invalid_argument("A scene child cannot be null");
    }
    if (id_.has_value())
    {
        throw std::logic_error(
            "Children of a registered scene node must be added with Scene::addChild()");
    }

    return attachChild(std::move(child));
}

SceneNode& SceneNode::addChild(std::string name)
{
    return addChild(std::make_unique<SceneNode>(std::move(name)));
}

const std::vector<std::unique_ptr<SceneNode>>& SceneNode::children() const noexcept
{
    return children_;
}

/* --- Private member functions --- */

SceneNode& SceneNode::attachChild(std::unique_ptr<SceneNode> child)
{
    SceneNode& result = *child;
    children_.push_back(std::move(child));
    return result;
}

void SceneNode::assignId(SceneNodeId id) noexcept
{
    id_ = id;
}

void SceneNode::resolve(const Mat4& parentWorld) noexcept
{
    worldTransform_ = parentWorld * localTransform_.matrix();
    for (const std::unique_ptr<SceneNode>& child : children_)
    {
        child->resolve(worldTransform_);
    }
}

void SceneNode::appendDrawItems(std::vector<DrawItem>& output) const
{
    const RenderObjectId* renderObject = std::get_if<RenderObjectId>(&component_);
    if (renderObject != nullptr)
    {
        output.push_back({.renderObject = *renderObject, .world = worldTransform_});
    }
    for (const std::unique_ptr<SceneNode>& child : children_)
    {
        child->appendDrawItems(output);
    }
}
} // namespace fire_engine
