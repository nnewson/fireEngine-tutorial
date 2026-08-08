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

std::optional<RenderObjectId> SceneNode::renderObject() const noexcept
{
    return renderObject_;
}

void SceneNode::renderObject(RenderObjectId renderObject) noexcept
{
    renderObject_ = renderObject;
}

void SceneNode::clearRenderObject() noexcept
{
    renderObject_.reset();
}

SceneNode& SceneNode::addChild(std::unique_ptr<SceneNode> child)
{
    if (!child)
    {
        throw std::invalid_argument("A scene child cannot be null");
    }

    SceneNode& result = *child;
    children_.push_back(std::move(child));
    return result;
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
    if (renderObject_.has_value())
    {
        output.push_back({.renderObject = *renderObject_, .world = worldTransform_});
    }
    for (const std::unique_ptr<SceneNode>& child : children_)
    {
        child->appendDrawItems(output);
    }
}
} // namespace fire_engine
