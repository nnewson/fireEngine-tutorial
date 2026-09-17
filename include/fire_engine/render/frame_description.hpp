#pragma once

#include <fire_engine/render/camera.hpp>
#include <fire_engine/render/directional_shadow_view.hpp>

namespace fire_engine
{
/* --- POD structs --- */

/** @brief Vulkan-free application values sampled to describe one rendered frame. */
struct FrameDescription
{
    Camera camera;                           ///< Perspective view used by the forward pass.
    DirectionalShadowView directionalShadow; ///< Directional view used by the shadow pass.
};
} // namespace fire_engine
