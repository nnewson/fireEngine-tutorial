#include <fire_engine/scene/scene_snapshot.hpp>

#include <fire_engine/graphics/render_assets.hpp>

#include <utility>

namespace fire_engine
{
/* --- Public member functions --- */

SceneSnapshot::SceneSnapshot(const RenderAssets& catalog, std::vector<DrawItem> drawItems)
    : drawItems_{std::move(drawItems)},
      catalog_{catalog.stamp()}
{
}

SceneSnapshot::SceneSnapshot(SceneSnapshot&& other) noexcept
    : drawItems_{std::move(other.drawItems_)},
      catalog_{std::exchange(other.catalog_, std::nullopt)}
{
    // A moved-from vector is valid but unspecified; clearing makes the source
    // an empty snapshot, which cannot be mistaken for a stamped one.
    other.drawItems_.clear();
}

SceneSnapshot& SceneSnapshot::operator=(SceneSnapshot&& other) noexcept
{
    if (this == &other)
    {
        return *this;
    }

    drawItems_ = std::move(other.drawItems_);
    other.drawItems_.clear();
    catalog_ = std::exchange(other.catalog_, std::nullopt);
    return *this;
}

std::span<const DrawItem> SceneSnapshot::drawItems() const noexcept
{
    return drawItems_;
}

std::optional<RenderAssetsStamp> SceneSnapshot::catalog() const noexcept
{
    return catalog_;
}

std::vector<DrawItem> SceneSnapshot::releaseStorage() && noexcept
{
    std::vector<DrawItem> storage = std::move(drawItems_);
    drawItems_.clear();
    catalog_.reset();
    return storage;
}
} // namespace fire_engine
