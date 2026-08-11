#include <fire_engine/gltf/gltf_loader.hpp>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fastgltf/core.hpp>
#include <fastgltf/math.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>

#include <fire_engine/content/detail/scene_content_validation.hpp>
#include <fire_engine/gltf/detail/image_decoder.hpp>
#include <fire_engine/graphics/material.hpp>
#include <fire_engine/graphics/mesh.hpp>
#include <fire_engine/graphics/render_object.hpp>
#include <fire_engine/scene/animator.hpp>

namespace
{
/** @cond INTERNAL */
/* --- File-local structs --- */

/** @brief Engine animation binding selected for one source glTF node. */
struct NodeAnimationBinding
{
    fire_engine::Animator animator; ///< Reusable channel and the target property it drives.
};

/** @brief Imported render objects produced by each source glTF mesh. */
using MeshRenderObjects = std::vector<std::vector<fire_engine::RenderObjectId>>;

/* --- File-local function declarations --- */

/**
 * @brief Stops loading with a consistently classified unsupported-subset diagnostic.
 * @param message Specific unsupported glTF feature.
 * @throws std::runtime_error unconditionally.
 */
[[noreturn]] void unsupported(const std::string& message);

/**
 * @brief Parses one JSON glTF and loads its external buffer data.
 * @param path Source glTF path.
 * @return fastgltf asset retaining parsed descriptions and buffer bytes.
 * @throws std::runtime_error if reading or parsing fails, or a required extension is unsupported.
 */
[[nodiscard]] fastgltf::Asset parseAsset(const std::filesystem::path& path);

/**
 * @brief Decodes every external source image into the destination asset collection.
 * @param source Parsed glTF containing image descriptions.
 * @param directory Directory used to resolve relative image paths.
 * @param destination Render assets receiving decoded images in source order.
 * @throws std::runtime_error if an image source or its encoded data is unsupported or invalid.
 */
void loadImages(const fastgltf::Asset& source, const std::filesystem::path& directory,
                fire_engine::RenderAssets& destination);

/**
 * @brief Maps a glTF filtering mode to the tutorial's non-mipmapped subset.
 * @param filter Source filtering mode.
 * @return Corresponding nearest or linear texture filter.
 * @throws std::runtime_error if the filtering mode is unknown.
 */
[[nodiscard]] fire_engine::TextureFilter mapFilter(fastgltf::Filter filter);

/**
 * @brief Maps a glTF coordinate-wrapping mode to the tutorial texture description.
 * @param wrap Source wrapping mode.
 * @return Corresponding texture-addressing mode.
 * @throws std::runtime_error if the wrapping mode is unknown.
 */
[[nodiscard]] fire_engine::TextureWrap mapWrap(fastgltf::Wrap wrap);

/**
 * @brief Imports image references and sampler behavior in source texture order.
 * @param source Parsed glTF containing textures and samplers.
 * @param destination Render assets receiving texture descriptions.
 * @throws std::runtime_error if a texture reference or sampler value is invalid.
 */
void loadTextures(const fastgltf::Asset& source, fire_engine::RenderAssets& destination);

/**
 * @brief Imports base-color factors and texture references in source material order.
 * @param source Parsed glTF containing material descriptions.
 * @param destination Render assets receiving materials.
 * @return Engine material IDs corresponding to source material indices.
 * @throws std::runtime_error if a material uses an unsupported texture coordinate or reference.
 */
[[nodiscard]] std::vector<fire_engine::MaterialId>
loadMaterials(const fastgltf::Asset& source, fire_engine::RenderAssets& destination);

/**
 * @brief Checks one vertex-like accessor against the supported float subset.
 * @param accessor Source accessor to inspect.
 * @param type Required scalar or vector shape.
 * @param semantic Diagnostic name of the attribute represented by the accessor.
 * @throws std::runtime_error if the accessor is sparse, missing data, or has the wrong type.
 */
void validateVertexAccessor(const fastgltf::Accessor& accessor, fastgltf::AccessorType type,
                            std::string_view semantic);

/**
 * @brief Imports one indexed triangle primitive with positions and texture coordinates.
 * @param source Parsed glTF owning the primitive's accessors and buffer data.
 * @param primitive Primitive description to import.
 * @return Complete CPU mesh description.
 * @throws std::runtime_error if required geometry data is absent, invalid, or unsupported.
 */
[[nodiscard]] fire_engine::Mesh loadPrimitive(const fastgltf::Asset& source,
                                              const fastgltf::Primitive& primitive);

/**
 * @brief Imports every mesh primitive and creates its mesh/material relationship.
 * @param source Parsed glTF containing mesh descriptions.
 * @param materials Engine material IDs corresponding to source material indices.
 * @param destination Render assets receiving meshes and render objects.
 * @return Render-object IDs grouped by their source mesh index.
 * @throws std::runtime_error if a primitive or material reference is invalid.
 */
[[nodiscard]] MeshRenderObjects loadMeshes(const fastgltf::Asset& source,
                                           const std::vector<fire_engine::MaterialId>& materials,
                                           fire_engine::RenderAssets& destination);

/**
 * @brief Imports one linear quaternion timeline from a glTF animation sampler.
 * @param source Parsed glTF owning the sampler's accessors and buffer data.
 * @param sampler Animation sampler connecting timestamp and rotation accessors.
 * @return Target-independent rotation channel.
 * @throws std::runtime_error if interpolation, timestamps, or quaternion values are invalid.
 */
[[nodiscard]] fire_engine::AnimationChannel
loadAnimationChannel(const fastgltf::Asset& source, const fastgltf::AnimationSampler& sampler);

/**
 * @brief Imports target-independent animations and records their source-node bindings.
 * @param source Parsed glTF containing animations and target nodes.
 * @param destination Animation collection receiving imported channels.
 * @return Optional Animator binding for every source node.
 * @throws std::runtime_error if a channel target, sampler, or binding is unsupported or invalid.
 */
[[nodiscard]] std::vector<std::optional<NodeAnimationBinding>>
loadAnimations(const fastgltf::Asset& source, std::vector<fire_engine::Animation>& destination);

/**
 * @brief Converts one decomposed glTF node transform into engine math types.
 * @param node Source node containing translation, rotation, and scale.
 * @return Normalized engine transform.
 * @throws std::runtime_error if decomposition or quaternion normalization failed.
 */
[[nodiscard]] fire_engine::Transform loadTransform(const fastgltf::Node& node);

/**
 * @brief Recursively imports one source node and its renderable children.
 * @param source Parsed glTF owning the node hierarchy.
 * @param nodeIndex Source node index to import.
 * @param meshes Render-object IDs grouped by source mesh index.
 * @param animationBindings Optional Animator binding for every source node.
 * @return Owned engine node containing the imported subtree.
 * @throws std::runtime_error if a node, mesh, transform, or child reference is invalid.
 */
[[nodiscard]] std::unique_ptr<fire_engine::SceneNode>
loadNode(const fastgltf::Asset& source, std::size_t nodeIndex, const MeshRenderObjects& meshes,
         const std::vector<std::optional<NodeAnimationBinding>>& animationBindings);

/**
 * @brief Imports the default selected scene and all reachable root subtrees.
 * @param source Parsed glTF containing scene and node descriptions.
 * @param meshes Render-object IDs grouped by source mesh index.
 * @param animationBindings Optional Animator binding for every source node.
 * @param destination Scene receiving imported roots.
 * @throws std::runtime_error if the selected scene or a reachable node is invalid.
 */
void loadScene(const fastgltf::Asset& source, const MeshRenderObjects& meshes,
               const std::vector<std::optional<NodeAnimationBinding>>& animationBindings,
               fire_engine::Scene& destination);
/** @endcond */
} // namespace

namespace fire_engine
{
/* --- Public member functions --- */

SceneContent GltfLoader::load(const std::filesystem::path& path) const
{
    fastgltf::Asset source = parseAsset(path);
    SceneContent result;

    loadImages(source, path.parent_path(), result.assets);
    loadTextures(source, result.assets);
    const std::vector<MaterialId> materials = loadMaterials(source, result.assets);
    const MeshRenderObjects meshes = loadMeshes(source, materials, result.assets);
    const std::vector<std::optional<NodeAnimationBinding>> animationBindings =
        loadAnimations(source, result.animations);
    loadScene(source, meshes, animationBindings, result.scene);
    result.scene.updateWorldTransforms();
    detail::validateSceneContent(result);
    return result;
}
} // namespace fire_engine

namespace
{
/** @cond INTERNAL */
/* --- File-local functions --- */

[[noreturn]] void unsupported(const std::string& message)
{
    throw std::runtime_error("Unsupported glTF data: " + message);
}

[[nodiscard]] fastgltf::Asset parseAsset(const std::filesystem::path& path)
{
    auto file = fastgltf::GltfDataBuffer::FromPath(path);
    if (file.error() != fastgltf::Error::None)
    {
        throw std::runtime_error("Could not read glTF '" + path.string() +
                                 "': " + std::string{fastgltf::getErrorMessage(file.error())});
    }

    // Enable every extension grammar built into fastgltf so parsing can finish
    // and the tutorial can name any recognized required extension that its
    // deliberately small subset does not support.
    constexpr auto kRecognizedExtensions =
        static_cast<fastgltf::Extensions>(std::numeric_limits<std::uint64_t>::max());
    fastgltf::Parser parser{kRecognizedExtensions};
    constexpr fastgltf::Options kOptions =
        fastgltf::Options::LoadExternalBuffers | fastgltf::Options::DecomposeNodeMatrices;
    auto parsed = parser.loadGltf(file.get(), path.parent_path(), kOptions);
    if (parsed.error() != fastgltf::Error::None)
    {
        throw std::runtime_error("Could not parse glTF '" + path.string() +
                                 "': " + std::string{fastgltf::getErrorMessage(parsed.error())});
    }

    fastgltf::Asset result = std::move(parsed.get());
    if (!result.extensionsRequired.empty())
    {
        unsupported("required extension '" + std::string{result.extensionsRequired.front()} + "'");
    }
    return result;
}

void loadImages(const fastgltf::Asset& source, const std::filesystem::path& directory,
                fire_engine::RenderAssets& destination)
{
    for (const fastgltf::Image& image : source.images)
    {
        const auto* uri = std::get_if<fastgltf::sources::URI>(&image.data);
        if (uri == nullptr || !uri->uri.isLocalPath())
        {
            unsupported("images must use external local files");
        }
        static_cast<void>(
            destination.addImage(fire_engine::detail::decodeRgba8(directory / uri->uri.fspath())));
    }
}

[[nodiscard]] fire_engine::TextureFilter mapFilter(fastgltf::Filter filter)
{
    switch (filter)
    {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return fire_engine::TextureFilter::eNearest;
    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
        return fire_engine::TextureFilter::eLinear;
    }
    unsupported("unknown texture filter");
}

[[nodiscard]] fire_engine::TextureWrap mapWrap(fastgltf::Wrap wrap)
{
    switch (wrap)
    {
    case fastgltf::Wrap::Repeat:
        return fire_engine::TextureWrap::eRepeat;
    case fastgltf::Wrap::MirroredRepeat:
        return fire_engine::TextureWrap::eMirroredRepeat;
    case fastgltf::Wrap::ClampToEdge:
        return fire_engine::TextureWrap::eClampToEdge;
    }
    unsupported("unknown texture wrap mode");
}

void loadTextures(const fastgltf::Asset& source, fire_engine::RenderAssets& destination)
{
    for (const fastgltf::Texture& texture : source.textures)
    {
        if (!texture.imageIndex.has_value() || *texture.imageIndex >= source.images.size())
        {
            unsupported("a texture has no supported source image");
        }

        fire_engine::Texture imported{.image = fire_engine::ImageId{.value = *texture.imageIndex}};
        if (texture.samplerIndex.has_value())
        {
            if (*texture.samplerIndex >= source.samplers.size())
            {
                throw std::runtime_error("glTF texture refers to an invalid sampler");
            }
            const fastgltf::Sampler& sampler = source.samplers[*texture.samplerIndex];
            imported.minFilter = sampler.minFilter.has_value()
                                     ? mapFilter(*sampler.minFilter)
                                     : fire_engine::TextureFilter::eLinear;
            imported.magFilter = sampler.magFilter.has_value()
                                     ? mapFilter(*sampler.magFilter)
                                     : fire_engine::TextureFilter::eLinear;
            imported.wrapU = mapWrap(sampler.wrapS);
            imported.wrapV = mapWrap(sampler.wrapT);
        }
        static_cast<void>(destination.addTexture(imported));
    }
}

[[nodiscard]] std::vector<fire_engine::MaterialId>
loadMaterials(const fastgltf::Asset& source, fire_engine::RenderAssets& destination)
{
    std::vector<fire_engine::MaterialId> result;
    result.reserve(source.materials.size() + 1);
    for (const fastgltf::Material& material : source.materials)
    {
        const auto& factor = material.pbrData.baseColorFactor;
        fire_engine::Material imported{
            .baseColor = {.r = static_cast<float>(factor[0]),
                          .g = static_cast<float>(factor[1]),
                          .b = static_cast<float>(factor[2]),
                          .a = static_cast<float>(factor[3])},
            .baseColorTexture = std::nullopt,
        };
        if (material.pbrData.baseColorTexture.has_value())
        {
            const fastgltf::TextureInfo& texture = *material.pbrData.baseColorTexture;
            if (texture.texCoordIndex != 0)
            {
                unsupported("base-color textures must use TEXCOORD_0");
            }
            if (texture.textureIndex >= source.textures.size())
            {
                throw std::runtime_error("glTF material refers to an invalid texture");
            }
            imported.baseColorTexture = fire_engine::TextureId{.value = texture.textureIndex};
        }
        result.push_back(destination.addMaterial(imported));
    }
    return result;
}

void validateVertexAccessor(const fastgltf::Accessor& accessor, fastgltf::AccessorType type,
                            std::string_view semantic)
{
    if (accessor.type != type || accessor.componentType != fastgltf::ComponentType::Float ||
        accessor.normalized || accessor.sparse.has_value() || !accessor.bufferViewIndex.has_value())
    {
        unsupported(std::string{semantic} + " must be a non-sparse float accessor");
    }
}

[[nodiscard]] fire_engine::Mesh loadPrimitive(const fastgltf::Asset& source,
                                              const fastgltf::Primitive& primitive)
{
    if (primitive.type != fastgltf::PrimitiveType::Triangles)
    {
        unsupported("mesh primitives must use triangle-list mode");
    }
    if (!primitive.indicesAccessor.has_value())
    {
        unsupported("mesh primitives must be indexed");
    }

    const auto position = primitive.findAttribute("POSITION");
    const auto uv = primitive.findAttribute("TEXCOORD_0");
    if (position == primitive.attributes.end() || uv == primitive.attributes.end())
    {
        unsupported("mesh primitives require POSITION and TEXCOORD_0");
    }

    const fastgltf::Accessor& positions = source.accessors.at(position->accessorIndex);
    const fastgltf::Accessor& textureCoordinates = source.accessors.at(uv->accessorIndex);
    validateVertexAccessor(positions, fastgltf::AccessorType::Vec3, "POSITION");
    validateVertexAccessor(textureCoordinates, fastgltf::AccessorType::Vec2, "TEXCOORD_0");
    if (positions.count != textureCoordinates.count)
    {
        throw std::runtime_error("glTF POSITION and TEXCOORD_0 counts do not match");
    }

    fire_engine::Mesh result;
    result.vertices.resize(positions.count);
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec3>(
        source, positions,
        [&result](fastgltf::math::fvec3 value, std::size_t index)
        {
            result.vertices[index] = {
                .position = {.x = value[0], .y = value[1], .z = value[2]},
                .color = {.r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f},
                .textureCoordinate = {},
            };
        });
    fastgltf::iterateAccessorWithIndex<fastgltf::math::fvec2>(
        source, textureCoordinates,
        [&result](fastgltf::math::fvec2 value, std::size_t index)
        {
            result.vertices[index].textureCoordinate =
                fire_engine::Vec2{.x = value[0], .y = value[1]};
        });

    const fastgltf::Accessor& indices = source.accessors.at(*primitive.indicesAccessor);
    if (indices.type != fastgltf::AccessorType::Scalar || indices.sparse.has_value() ||
        !indices.bufferViewIndex.has_value() ||
        (indices.componentType != fastgltf::ComponentType::UnsignedShort &&
         indices.componentType != fastgltf::ComponentType::UnsignedInt))
    {
        unsupported("indices must be non-sparse unsigned 16-bit or 32-bit scalars");
    }
    result.indices.reserve(indices.count);
    fastgltf::iterateAccessor<std::uint32_t>(source, indices,
                                             [&result](std::uint32_t value)
                                             {
                                                 if (value >= result.vertices.size())
                                                 {
                                                     throw std::runtime_error(
                                                         "glTF index exceeds vertex count");
                                                 }
                                                 result.indices.push_back(value);
                                             });
    return result;
}

[[nodiscard]] MeshRenderObjects loadMeshes(const fastgltf::Asset& source,
                                           const std::vector<fire_engine::MaterialId>& materials,
                                           fire_engine::RenderAssets& destination)
{
    MeshRenderObjects result(source.meshes.size());
    std::optional<fire_engine::MaterialId> defaultMaterial;
    for (std::size_t meshIndex = 0; meshIndex < source.meshes.size(); ++meshIndex)
    {
        for (const fastgltf::Primitive& primitive : source.meshes[meshIndex].primitives)
        {
            fire_engine::MaterialId material;
            if (primitive.materialIndex.has_value())
            {
                if (*primitive.materialIndex >= materials.size())
                {
                    throw std::runtime_error("glTF primitive refers to an invalid material");
                }
                material = materials[*primitive.materialIndex];
            }
            else
            {
                if (!defaultMaterial.has_value())
                {
                    defaultMaterial = destination.addMaterial(fire_engine::Material{});
                }
                material = *defaultMaterial;
            }

            const fire_engine::MeshId mesh = destination.addMesh(loadPrimitive(source, primitive));
            result[meshIndex].push_back(
                destination.addRenderObject({.mesh = mesh, .material = material}));
        }
    }
    return result;
}

[[nodiscard]] fire_engine::AnimationChannel
loadAnimationChannel(const fastgltf::Asset& source, const fastgltf::AnimationSampler& sampler)
{
    if (sampler.interpolation != fastgltf::AnimationInterpolation::Linear)
    {
        unsupported("animation interpolation must be LINEAR");
    }
    const fastgltf::Accessor& timestamps = source.accessors.at(sampler.inputAccessor);
    const fastgltf::Accessor& values = source.accessors.at(sampler.outputAccessor);
    validateVertexAccessor(timestamps, fastgltf::AccessorType::Scalar, "animation timestamps");
    validateVertexAccessor(values, fastgltf::AccessorType::Vec4, "rotation values");
    if (timestamps.count != values.count)
    {
        throw std::runtime_error("glTF animation timestamp and value counts do not match");
    }

    fire_engine::AnimationChannel result;
    result.timestamps.reserve(timestamps.count);
    fastgltf::iterateAccessor<float>(
        source, timestamps,
        [&result](float value)
        {
            if (!result.timestamps.empty() && value <= result.timestamps.back())
            {
                throw std::runtime_error("glTF animation timestamps are not increasing");
            }
            result.timestamps.push_back(value);
        });
    result.values.reserve(values.count);
    fastgltf::iterateAccessor<fastgltf::math::fvec4>(
        source, values,
        [&result](fastgltf::math::fvec4 value)
        {
            const auto normalized =
                fire_engine::Quaternion{.x = value[0], .y = value[1], .z = value[2], .w = value[3]}
                    .normalized();
            if (!normalized.has_value())
            {
                throw std::runtime_error("glTF animation contains an invalid quaternion");
            }
            result.values.push_back(*normalized);
        });
    return result;
}

[[nodiscard]] std::vector<std::optional<NodeAnimationBinding>>
loadAnimations(const fastgltf::Asset& source, std::vector<fire_engine::Animation>& destination)
{
    std::vector<std::optional<NodeAnimationBinding>> bindings(source.nodes.size());
    destination.reserve(source.animations.size());
    for (std::size_t animationIndex = 0; animationIndex < source.animations.size();
         ++animationIndex)
    {
        const fastgltf::Animation& animation = source.animations[animationIndex];
        fire_engine::Animation imported{.name = std::string{animation.name}, .channels = {}};
        imported.channels.reserve(animation.channels.size());
        std::vector<std::optional<std::size_t>> importedSamplerChannels(animation.samplers.size());
        for (const fastgltf::AnimationChannel& channel : animation.channels)
        {
            if (channel.path != fastgltf::AnimationPath::Rotation)
            {
                unsupported("only rotation animation channels are supported");
            }
            if (!channel.nodeIndex.has_value() || *channel.nodeIndex >= source.nodes.size() ||
                channel.samplerIndex >= animation.samplers.size())
            {
                throw std::runtime_error("glTF animation channel has an invalid reference");
            }
            if (bindings[*channel.nodeIndex].has_value())
            {
                unsupported("one source node cannot have several animation bindings yet");
            }

            std::optional<std::size_t>& importedChannel =
                importedSamplerChannels[channel.samplerIndex];
            if (!importedChannel.has_value())
            {
                importedChannel = imported.channels.size();
                imported.channels.push_back(
                    loadAnimationChannel(source, animation.samplers[channel.samplerIndex]));
            }
            bindings[*channel.nodeIndex] = NodeAnimationBinding{
                .animator = {.animation = fire_engine::AnimationId{.value = animationIndex},
                             .channel =
                                 fire_engine::AnimationChannelId{.value = importedChannel.value()},
                             .targetPath = fire_engine::AnimationTargetPath::eRotation,
                             .playbackTime = 0.0f,
                             .looping = true},
            };
        }
        destination.push_back(std::move(imported));
    }
    return bindings;
}

[[nodiscard]] fire_engine::Transform loadTransform(const fastgltf::Node& node)
{
    const auto* transform = std::get_if<fastgltf::TRS>(&node.transform);
    if (transform == nullptr)
    {
        unsupported("node matrix could not be decomposed into TRS");
    }
    const auto normalized = fire_engine::Quaternion{.x = transform->rotation[0],
                                                    .y = transform->rotation[1],
                                                    .z = transform->rotation[2],
                                                    .w = transform->rotation[3]}
                                .normalized();
    if (!normalized.has_value())
    {
        throw std::runtime_error("glTF node contains an invalid rotation");
    }
    return {
        .translation = {.x = transform->translation[0],
                        .y = transform->translation[1],
                        .z = transform->translation[2]},
        .rotation = *normalized,
        .scale = {.x = transform->scale[0], .y = transform->scale[1], .z = transform->scale[2]},
    };
}

[[nodiscard]] std::unique_ptr<fire_engine::SceneNode>
loadNode(const fastgltf::Asset& source, std::size_t nodeIndex, const MeshRenderObjects& meshes,
         const std::vector<std::optional<NodeAnimationBinding>>& animationBindings)
{
    if (nodeIndex >= source.nodes.size())
    {
        throw std::runtime_error("glTF scene contains an invalid node reference");
    }
    const fastgltf::Node& sourceNode = source.nodes[nodeIndex];
    auto node = std::make_unique<fire_engine::SceneNode>(std::string{sourceNode.name});
    node->localTransform(loadTransform(sourceNode));
    const std::optional<NodeAnimationBinding>& animationBinding = animationBindings[nodeIndex];
    if (animationBinding.has_value())
    {
        node->component(animationBinding.value().animator);
    }

    if (sourceNode.meshIndex.has_value())
    {
        if (*sourceNode.meshIndex >= meshes.size())
        {
            throw std::runtime_error("glTF node refers to an invalid mesh");
        }
        for (const fire_engine::RenderObjectId renderObject : meshes[*sourceNode.meshIndex])
        {
            fire_engine::SceneNode& renderNode = node->addChild(
                sourceNode.name.empty() ? "Imported primitive" : std::string{sourceNode.name});
            renderNode.component(renderObject);
        }
    }
    for (const std::size_t child : sourceNode.children)
    {
        node->addChild(loadNode(source, child, meshes, animationBindings));
    }
    return node;
}

void loadScene(const fastgltf::Asset& source, const MeshRenderObjects& meshes,
               const std::vector<std::optional<NodeAnimationBinding>>& animationBindings,
               fire_engine::Scene& destination)
{
    if (source.scenes.empty())
    {
        throw std::runtime_error("glTF contains no scene");
    }
    const std::size_t sceneIndex = source.defaultScene.value_or(0);
    if (sceneIndex >= source.scenes.size())
    {
        throw std::runtime_error("glTF default scene index is invalid");
    }
    for (const std::size_t root : source.scenes[sceneIndex].nodeIndices)
    {
        destination.addRoot(loadNode(source, root, meshes, animationBindings));
    }
}
/** @endcond */
} // namespace
