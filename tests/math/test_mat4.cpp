#include "fire_engine/math/mat4.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

namespace
{
using Catch::Approx;
using fire_engine::Mat4;
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
    const Vec3 position{.x = 1.0f, .y = 2.0f, .z = 3.0f};
    const Vec4 vector{.x = 0.1f, .y = 0.2f, .z = 0.3f, .w = 0.4f};

    REQUIRE(position.x == 1.0f);
    REQUIRE(position.y == 2.0f);
    REQUIRE(position.z == 3.0f);
    REQUIRE(vector.x == Approx(0.1f));
    REQUIRE(vector.y == Approx(0.2f));
    REQUIRE(vector.z == Approx(0.3f));
    REQUIRE(vector.w == Approx(0.4f));
    REQUIRE(vector.dot(Vec4{.x = 1.0f, .y = 2.0f, .z = 3.0f, .w = 4.0f}) == Approx(3.0f));
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
