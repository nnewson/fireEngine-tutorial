#include "fire_engine/math/mat4.hpp"
#include "fire_engine/math/quaternion.hpp"
#include "fire_engine/math/transform.hpp"
#include "fire_engine/math/vec2.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <numbers>
#include <stdexcept>

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
