#include "fire_engine/math/mat4.hpp"
#include "fire_engine/math/quaternion.hpp"
#include "fire_engine/math/transform.hpp"
#include "fire_engine/math/vec2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace
{
using Catch::Approx;
using fire_engine::Mat4;
using fire_engine::NormalizeError;
using fire_engine::Quaternion;
using fire_engine::Transform;
using fire_engine::Vec2;
using fire_engine::Vec3;
using fire_engine::Vec4;

void requireSameRotation(Quaternion actual, Quaternion expected)
{
    if (actual.dot(expected) < 0.0f)
    {
        actual = -actual;
    }
    // Absolute component accuracy, not an angle recovered through ill-conditioned acos
    // near identity. This also bounds the near-equal SLERP approximation.
    constexpr float kComponentTolerance = 1.0e-6f;
    REQUIRE(std::abs(actual.x - expected.x) <= kComponentTolerance);
    REQUIRE(std::abs(actual.y - expected.y) <= kComponentTolerance);
    REQUIRE(std::abs(actual.z - expected.z) <= kComponentTolerance);
    REQUIRE(std::abs(actual.w - expected.w) <= kComponentTolerance);
    REQUIRE(std::abs(actual.lengthSquared() - 1.0f) <= kComponentTolerance);
}
} // namespace

TEST_CASE("Mat4 defaults to the zero matrix")
{
    const Mat4 matrix;

    for (std::size_t index = 0; index < 16; ++index)
    {
        REQUIRE(matrix.data()[index] == 0.0f);
    }
}

TEST_CASE("Vector aggregates preserve their components")
{
    const Vec2 textureCoordinate{.x = 0.25f, .y = 0.75f};
    const Vec3 position{.x = 1.0f, .y = 2.0f, .z = 3.0f};
    const Vec4 vector{.x = 0.1f, .y = 0.2f, .z = 0.3f, .w = 0.4f};

    REQUIRE(textureCoordinate.x == 0.25f);
    REQUIRE(textureCoordinate.y == 0.75f);
    REQUIRE(position.x == 1.0f);
    REQUIRE(position.y == 2.0f);
    REQUIRE(position.z == 3.0f);
    REQUIRE(vector.x == Approx(0.1f));
    REQUIRE(vector.y == Approx(0.2f));
    REQUIRE(vector.z == Approx(0.3f));
    REQUIRE(vector.w == Approx(0.4f));
    REQUIRE(vector.dot(Vec4{.x = 1.0f, .y = 2.0f, .z = 3.0f, .w = 4.0f}) == Approx(3.0f));
}

TEST_CASE("Quaternion normalization and interpolation retain valid rotations")
{
    const Quaternion scaledIdentity{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 4.0f};
    const auto normalizedIdentity = scaledIdentity.normalized();
    REQUIRE(normalizedIdentity.has_value());
    REQUIRE(*normalizedIdentity == Quaternion::identity());

    const Quaternion halfTurn{.x = 0.0f, .y = 0.0f, .z = 1.0f, .w = 0.0f};
    const auto halfway = Quaternion::identity().normalizedLerp(halfTurn, 0.5f);
    REQUIRE(halfway.has_value());
    REQUIRE(halfway->lengthSquared() == Approx(1.0f));
    REQUIRE(halfway->z == Approx(0.70710678f));
    REQUIRE(halfway->w == Approx(0.70710678f));

    const Quaternion equivalentIdentity{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = -1.0f};
    const auto shortestPath = Quaternion::identity().normalizedLerp(equivalentIdentity, 0.5f);
    REQUIRE(shortestPath.has_value());
    REQUIRE(*shortestPath == Quaternion::identity());
    const Quaternion zero{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    REQUIRE(zero.normalized() == std::unexpected{NormalizeError::eZeroLength});
    const Quaternion nonFinite{
        .x = 0.0f,
        .y = 0.0f,
        .z = 0.0f,
        .w = std::numeric_limits<float>::quiet_NaN(),
    };
    REQUIRE(nonFinite.normalized() == std::unexpected{NormalizeError::eNonFinite});
}

TEST_CASE("Quaternion spherical interpolation advances by equal angles")
{
    constexpr double kHalfArc = std::numbers::pi / 3.0; // Full rotation is 120 degrees.
    const Quaternion end{.z = static_cast<float>(std::sin(kHalfArc)),
                         .w = static_cast<float>(std::cos(kHalfArc))};
    for (const Quaternion right : {end, -end})
    {
        for (const float amount : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f})
        {
            CAPTURE(amount, right.z, right.w);
            const auto sampled = Quaternion::identity().sphericalLerp(right, amount);
            REQUIRE(sampled.has_value());
            // The quarter points (30 and 90 degrees), unlike the midpoint, reject NLERP.
            requireSameRotation(*sampled, {.z = static_cast<float>(std::sin(kHalfArc * amount)),
                                           .w = static_cast<float>(std::cos(kHalfArc * amount))});
        }
    }
}

TEST_CASE("Quaternion spherical interpolation normalizes endpoints and preserves tie direction")
{
    const Quaternion left{.x = 1.0f, .y = 2.0f, .z = 3.0f, .w = 4.0f};
    const auto unitLeft = left.normalized();
    REQUIRE(unitLeft.has_value());
    for (const float scale : {1.0e-30f, 4.0f, 1.0e20f})
    {
        CAPTURE(scale);
        const Quaternion scaled{
            .x = left.x * scale, .y = left.y * scale, .z = left.z * scale, .w = left.w * scale};
        for (const float amount : {0.0f, 0.25f, 0.5f, 1.0f})
        {
            const auto equivalent = scaled.sphericalLerp(-left, amount);
            REQUIRE(equivalent.has_value());
            requireSameRotation(*equivalent, *unitLeft);

            const auto sampled =
                Quaternion{.w = scale}.sphericalLerp({.z = scale, .w = 0.0f}, amount);
            REQUIRE(sampled.has_value());
            const double halfAngle = std::numbers::pi * 0.5 * amount;
            requireSameRotation(*sampled, {.z = static_cast<float>(std::sin(halfAngle)),
                                           .w = static_cast<float>(std::cos(halfAngle))});
        }
    }

    for (const float direction : {-1.0f, 1.0f})
    {
        // An exact half turn has two equally short paths; do not silently pick the other.
        const Quaternion halfTurn{.z = direction, .w = 0.0f};
        REQUIRE(Quaternion::identity().dot(halfTurn) == 0.0f);
        const auto sampled = Quaternion::identity().sphericalLerp(halfTurn, 0.25f);
        REQUIRE(sampled.has_value());
        requireSameRotation(*sampled, {.z = direction * std::sin(std::numbers::pi_v<float> / 8.0f),
                                       .w = std::cos(std::numbers::pi_v<float> / 8.0f)});
    }
}

TEST_CASE("Quaternion spherical interpolation remains accurate across its near-equal threshold")
{
    for (const float cosine : {0.9994f, 0.9996f, 0.999999f, 1.0f})
    {
        CAPTURE(cosine);
        const Quaternion right{.y = std::sqrt(1.0f - cosine * cosine), .w = cosine};
        const auto unitRight = right.normalized();
        REQUIRE(unitRight.has_value());
        // Assert the fixture really straddles the registered 0.9995 dot threshold.
        REQUIRE((unitRight->w >= 0.9995f) == (cosine >= 0.9995f));
        const double halfArc = std::atan2(static_cast<double>(unitRight->y), unitRight->w);
        for (const float amount : {0.0f, 0.125f, 0.25f, 0.5f, 0.75f, 0.875f, 1.0f})
        {
            CAPTURE(amount);
            const auto sampled = Quaternion::identity().sphericalLerp(right, amount);
            REQUIRE(sampled.has_value());
            requireSameRotation(*sampled, {.y = static_cast<float>(std::sin(halfArc * amount)),
                                           .w = static_cast<float>(std::cos(halfArc * amount))});
        }
    }
}

TEST_CASE("Quaternion spherical interpolation rejects invalid inputs before endpoint shortcuts")
{
    const Quaternion identity;
    const Quaternion zero{.w = 0.0f};
    const std::array nonFiniteValues{std::numeric_limits<float>::quiet_NaN(),
                                     std::numeric_limits<float>::infinity(),
                                     -std::numeric_limits<float>::infinity()};
    for (const float amount : {0.0f, 0.5f, 1.0f})
    {
        REQUIRE(zero.sphericalLerp(identity, amount) ==
                std::unexpected{NormalizeError::eZeroLength});
        REQUIRE(identity.sphericalLerp(zero, amount) ==
                std::unexpected{NormalizeError::eZeroLength});
        for (const float value : nonFiniteValues)
        {
            for (const auto member :
                 {&Quaternion::x, &Quaternion::y, &Quaternion::z, &Quaternion::w})
            {
                Quaternion invalid;
                invalid.*member = value;
                REQUIRE(invalid.sphericalLerp(identity, amount) ==
                        std::unexpected{NormalizeError::eNonFinite});
                REQUIRE(identity.sphericalLerp(invalid, amount) ==
                        std::unexpected{NormalizeError::eNonFinite});
            }
        }
    }
    for (const float amount : nonFiniteValues)
    {
        REQUIRE(identity.sphericalLerp(identity, amount) ==
                std::unexpected{NormalizeError::eNonFinite});
        REQUIRE(identity.sphericalLerp({.y = 1.0f, .w = 0.0f}, amount) ==
                std::unexpected{NormalizeError::eNonFinite});
    }
}

TEST_CASE("Normalization remains stable across finite float magnitudes")
{
    // hypot keeps both values normalizable; sqrt(lengthSquared()) would underflow the tiny
    // vector to eZeroLength and overflow the large vector to eNonFinite.
    constexpr float kTiny = 1.0e-30f;
    constexpr float kLarge = 1.0e20f;

    const auto tinyVector = Vec3{.x = kTiny, .y = 0.0f, .z = 0.0f}.normalized();
    REQUIRE(tinyVector.has_value());
    REQUIRE(*tinyVector == Vec3{.x = 1.0f, .y = 0.0f, .z = 0.0f});

    const auto largeVector = Vec3{.x = kLarge, .y = kLarge, .z = kLarge}.normalized();
    REQUIRE(largeVector.has_value());
    REQUIRE(largeVector->lengthSquared() == Approx(1.0f));

    const Quaternion tinyQuaternion{.x = kTiny, .y = 0.0f, .z = 0.0f, .w = 0.0f};
    const auto normalizedQuaternion = tinyQuaternion.normalized();
    REQUIRE(normalizedQuaternion.has_value());
    REQUIRE(*normalizedQuaternion == Quaternion{.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f});
}

TEST_CASE("Mat4 stores values in column-major order")
{
    const Mat4 matrix = Mat4::translation(Vec3{.x = 2.0f, .y = 3.0f, .z = 4.0f});

    REQUIRE(matrix[0, 3] == 2.0f);
    REQUIRE(matrix[1, 3] == 3.0f);
    REQUIRE(matrix[2, 3] == 4.0f);
    REQUIRE(matrix.data()[12] == 2.0f);
    REQUIRE(matrix.data()[13] == 3.0f);
    REQUIRE(matrix.data()[14] == 4.0f);

    const Vec4 finalRow = matrix.row(3);
    REQUIRE(finalRow == Vec4{.w = 1.0f});

    const Vec4 finalColumn = matrix.column(3);
    REQUIRE(finalColumn == Vec4{.x = 2.0f, .y = 3.0f, .z = 4.0f, .w = 1.0f});
}

TEST_CASE("Mat4 composes parent and local transforms")
{
    const Mat4 transform = Mat4::translation(Vec3{.x = 2.0f, .y = 3.0f, .z = 4.0f}) *
                           Mat4::scale(Vec3{.x = 2.0f, .y = 3.0f, .z = 4.0f});

    const Vec4 transformed = transform * Vec4{.x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f};

    REQUIRE(transformed.x == Approx(4.0f));
    REQUIRE(transformed.y == Approx(6.0f));
    REQUIRE(transformed.z == Approx(8.0f));
    REQUIRE(transformed.w == Approx(1.0f));
}

TEST_CASE("Transform composes scale rotation and translation")
{
    constexpr float kHalfAngle = std::numbers::pi_v<float> * 0.25f;
    const Transform transform{
        .translation = {.x = 2.0f, .y = 3.0f, .z = 0.0f},
        .rotation = {.x = 0.0f, .y = 0.0f, .z = std::sin(kHalfAngle), .w = std::cos(kHalfAngle)},
        .scale = {.x = 2.0f, .y = 2.0f, .z = 1.0f},
    };

    const Vec4 transformed = transform.matrix() * Vec4{.x = 1.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    REQUIRE(transformed.x == Approx(2.0f).margin(0.00001f));
    REQUIRE(transformed.y == Approx(5.0f).margin(0.00001f));
    REQUIRE(transformed.z == Approx(0.0f).margin(0.00001f));
    REQUIRE(transformed.w == Approx(1.0f));
}

TEST_CASE("Mat4 camera transforms use zero-to-one depth and right-handed view space")
{
    const Mat4 projection = Mat4::perspective(std::numbers::pi_v<float> * 0.5f, 2.0f, 1.0f, 11.0f);
    const Vec4 nearPoint = projection * Vec4{.x = 0.0f, .y = 0.0f, .z = -1.0f, .w = 1.0f};
    const Vec4 farPoint = projection * Vec4{.x = 0.0f, .y = 0.0f, .z = -11.0f, .w = 1.0f};
    REQUIRE(nearPoint.z / nearPoint.w == Approx(0.0f).margin(0.00001f));
    REQUIRE(farPoint.z / farPoint.w == Approx(1.0f).margin(0.00001f));
    REQUIRE(projection[0, 0] == Approx(projection[1, 1] / 2.0f));

    const Vec4 viewSpaceUp = projection * Vec4{.x = 0.0f, .y = 1.0f, .z = -1.0f, .w = 1.0f};
    REQUIRE(viewSpaceUp.y / viewSpaceUp.w == Approx(1.0f));

    const auto view =
        Mat4::lookAt(Vec3{.x = 0.0f, .y = 0.0f, .z = 5.0f}, Vec3{.x = 0.0f, .y = 0.0f, .z = 0.0f},
                     Vec3{.x = 0.0f, .y = 1.0f, .z = 0.0f});
    REQUIRE(view.has_value());
    const Vec4 viewedOrigin = *view * Vec4{.x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f};
    REQUIRE(viewedOrigin == Vec4{.x = 0.0f, .y = 0.0f, .z = -5.0f, .w = 1.0f});

    REQUIRE_THROWS_AS(Mat4::perspective(0.0f, 1.0f, 0.1f, 100.0f), std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::perspective(std::numbers::pi_v<float> * 0.5f,
                                        std::numeric_limits<float>::infinity(), 0.1f, 100.0f),
                      std::invalid_argument);
    REQUIRE(Mat4::lookAt(Vec3{}, Vec3{}, Vec3{.y = 1.0f}) ==
            std::unexpected{NormalizeError::eZeroLength});
}

TEST_CASE("Mat4 orthographic projection maps every asymmetric frustum corner")
{
    constexpr float kLeft = -3.0f;
    constexpr float kRight = 5.0f;
    constexpr float kBottom = -2.0f;
    constexpr float kTop = 6.0f;
    constexpr float kNear = 1.5f;
    constexpr float kFar = 11.5f;
    const Mat4 projection = Mat4::orthographic(kLeft, kRight, kBottom, kTop, kNear, kFar);

    constexpr std::array kHorizontalCorners{
        std::pair{kLeft, -1.0f},
        std::pair{kRight, 1.0f},
    };
    constexpr std::array kVerticalCorners{
        std::pair{kBottom, -1.0f},
        std::pair{kTop, 1.0f},
    };
    constexpr std::array kDepthCorners{
        std::pair{-kNear, 0.0f},
        std::pair{-kFar, 1.0f},
    };

    for (const auto& [viewX, expectedX] : kHorizontalCorners)
    {
        for (const auto& [viewY, expectedY] : kVerticalCorners)
        {
            for (const auto& [viewZ, expectedZ] : kDepthCorners)
            {
                const Vec4 corner =
                    projection * Vec4{.x = viewX, .y = viewY, .z = viewZ, .w = 1.0f};
                REQUIRE(corner.x == Approx(expectedX).margin(0.00001f));
                REQUIRE(corner.y == Approx(expectedY).margin(0.00001f));
                REQUIRE(corner.z == Approx(expectedZ).margin(0.00001f));
                REQUIRE(corner.w == Approx(1.0f));
            }
        }
    }

    constexpr Vec4 kViewCenter{
        .x = (kLeft + kRight) * 0.5f,
        .y = (kBottom + kTop) * 0.5f,
        .z = -(kNear + kFar) * 0.5f,
        .w = 1.0f,
    };
    const Vec4 center = projection * kViewCenter;
    REQUIRE(center.x == Approx(0.0f).margin(0.00001f));
    REQUIRE(center.y == Approx(0.0f).margin(0.00001f));
    REQUIRE(center.z == Approx(0.5f).margin(0.00001f));
    REQUIRE(center.w == Approx(1.0f));
}

TEST_CASE("Mat4 orthographic projection rejects invalid parameters")
{
    constexpr std::array kValidParameters{-3.0f, 5.0f, -2.0f, 6.0f, 1.5f, 11.5f};
    for (std::size_t index = 0; index < kValidParameters.size(); ++index)
    {
        auto parameters = kValidParameters;
        parameters[index] = std::numeric_limits<float>::infinity();
        REQUIRE_THROWS_AS(Mat4::orthographic(parameters[0], parameters[1], parameters[2],
                                             parameters[3], parameters[4], parameters[5]),
                          std::invalid_argument);
    }

    auto nonFiniteParameters = kValidParameters;
    nonFiniteParameters[0] = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS_AS(Mat4::orthographic(nonFiniteParameters[0], nonFiniteParameters[1],
                                         nonFiniteParameters[2], nonFiniteParameters[3],
                                         nonFiniteParameters[4], nonFiniteParameters[5]),
                      std::invalid_argument);

    REQUIRE_THROWS_AS(Mat4::orthographic(1.0f, 1.0f, -2.0f, 6.0f, 1.5f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(2.0f, 1.0f, -2.0f, 6.0f, 1.5f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, 1.0f, 1.0f, 1.5f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, 2.0f, 1.0f, 1.5f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, -2.0f, 6.0f, 0.0f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, -2.0f, 6.0f, -1.0f, 11.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, -2.0f, 6.0f, 1.5f, 1.5f),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(Mat4::orthographic(-3.0f, 5.0f, -2.0f, 6.0f, 2.0f, 1.5f),
                      std::invalid_argument);
}
