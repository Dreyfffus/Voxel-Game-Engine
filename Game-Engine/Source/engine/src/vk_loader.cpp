#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include <vk_loader.h>
#include "vk_engine.h"
#include "vk_initializers.h"
#include "vk_types.h"
#include <glm/gtx/quaternion.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/parser.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/util.hpp>

const std::filesystem::path MODEL_ROOT = "Source/models";

constexpr std::string_view to_string(fastgltf::Error e) noexcept {
    using E = fastgltf::Error;
    switch (e) {
    case E::None:                         return "None";
    case E::InvalidPath:                  return "InvalidPath";
    case E::MissingExtensions:            return "MissingExtensions";
    case E::UnknownRequiredExtension:     return "UnknownRequiredExtension";
    case E::InvalidJson:                  return "InvalidJson";
    case E::InvalidGltf:                  return "InvalidGltf";
    case E::InvalidOrMissingAssetField:   return "InvalidOrMissingAssetField";
    case E::InvalidGLB:                   return "InvalidGLB";
    case E::MissingField:                 return "MissingField";
    case E::MissingExternalBuffer:        return "MissingExternalBuffer";
    case E::UnsupportedVersion:           return "UnsupportedVersion";
    case E::InvalidURI:                   return "InvalidURI";
    }
    return "UnknownError";
}

void LoadedGLTF::Draw(const glm::mat4& topMatrix, DrawContext& ctx) {
    for (auto& node : topNodes) {
        node->Draw(topMatrix, ctx);
    }
}

void LoadedGLTF::clearAll()
{
    VkDevice dv = creator->driver;

    for (auto& [k, v] : meshes) {

        creator->destroyBuffer(v->meshBuffers.indexBuffer);
        creator->destroyBuffer(v->meshBuffers.vertexBuffer);
    }

    for (auto& [k, v] : images) {

        if (v.image == creator->errorTexture.image) {
            continue;
        }
        creator->destroyImage(v);
    }

    for (auto& sampler : samplers) {
        vkDestroySampler(dv, sampler, nullptr);
    }

    auto materialBuffer = materialDataBuffer;
    auto samplersToDestroy = samplers;

    descriptorPool.destroyPools(dv);

    creator->destroyBuffer(materialBuffer);
}

VkFilter extract_filter(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::Nearest:
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::NearestMipMapLinear:
        return VK_FILTER_NEAREST;

    case fastgltf::Filter::Linear:
    case fastgltf::Filter::LinearMipMapNearest:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_FILTER_LINEAR;
    }
}

VkSamplerMipmapMode extract_mipmap_mode(fastgltf::Filter filter)
{
    switch (filter) {
    case fastgltf::Filter::NearestMipMapNearest:
    case fastgltf::Filter::LinearMipMapNearest:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;

    case fastgltf::Filter::NearestMipMapLinear:
    case fastgltf::Filter::LinearMipMapLinear:
    default:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }
}


std::optional<std::vector<std::shared_ptr<MeshAsset>>> load_gltf_meshes(VulkanEngine* engine, const std::filesystem::path& path) {
        std::filesystem::path filePath = MODEL_ROOT / path;

        fmt::print("Loading GLTF file {}\n", (MODEL_ROOT / path).string());

        fastgltf::GltfDataBuffer data;
        data.loadFromFile(filePath);

        constexpr auto gltfOptions = fastgltf::Options::LoadGLBBuffers
            | fastgltf::Options::LoadExternalBuffers;

        fastgltf::Asset gltf;
        fastgltf::Parser parser{};

        auto load = parser.loadBinaryGLTF(&data, filePath.parent_path(), gltfOptions);
        if (load) {
            gltf = std::move(load.get());
        }
        else {
            fmt::print("Failed to load glTF: {} \n", to_string(load.error()));
            return {};
        }

        std::vector<std::shared_ptr<MeshAsset>> meshes;

        // use the same vectors for all meshes so that the memory doesnt reallocate as
        // often
        std::vector<uint32_t> indices;
        std::vector<Vertex> vertices;
        for (fastgltf::Mesh& mesh : gltf.meshes) {
            MeshAsset newmesh;

            newmesh.name = mesh.name;

            // clear the mesh arrays each mesh, we dont want to merge them by error
            indices.clear();
            vertices.clear();

            for (auto&& p : mesh.primitives) {
                GeoSurface newSurface;
                newSurface.startIndex = (uint32_t)indices.size();
                newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

                size_t initial_vtx = vertices.size();

                // load indexes
                {
                    fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                    indices.reserve(indices.size() + indexaccessor.count);

                    fastgltf::iterateAccessor<uint32_t>(gltf, indexaccessor,
                        [&](uint32_t idx) {
                            indices.push_back(idx + static_cast<uint32_t>(initial_vtx));
                        });
                }

                // load vertex positions
                {
                    fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                    vertices.resize(vertices.size() + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                        [&](glm::vec3 v, size_t index) {
                            Vertex newvtx;
                            newvtx.position = v;
                            newvtx.normal = { 1, 0, 0 };
                            newvtx.color = glm::vec4{ 1.f };
                            newvtx.uv_x = 0;
                            newvtx.uv_y = 0;
                            vertices[initial_vtx + index] = newvtx;
                        });
                }

                // load vertex normals
                auto normals = p.findAttribute("NORMAL");
                if (normals != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                        [&](glm::vec3 v, size_t index) {
                            vertices[initial_vtx + index].normal = v;
                        });
                }

                // load UVs
                auto uv = p.findAttribute("TEXCOORD_0");
                if (uv != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                        [&](glm::vec2 v, size_t index) {
                            vertices[initial_vtx + index].uv_x = v.x;
                            vertices[initial_vtx + index].uv_y = v.y;
                        });
                }

                // load vertex colors
                auto colors = p.findAttribute("COLOR_0");
                if (colors != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                        [&](glm::vec4 v, size_t index) {
                            vertices[initial_vtx + index].color = v;
                        });
                }
                newmesh.surfaces.push_back(newSurface);
            }

            // display the vertex normals
#ifdef OVERRIDE_COLORS
            constexpr bool OverrideColors = true;
#else
            constexpr bool OverrideColors = false;
#endif
            if (OverrideColors) {
                for (Vertex& vtx : vertices) {
                    vtx.color = glm::vec4(vtx.normal, 1.f);
                }
            }
            newmesh.meshBuffers = engine->uploadMesh(indices, vertices);

            meshes.emplace_back(std::make_shared<MeshAsset>(std::move(newmesh)));
        }

        return meshes;
    }

std::optional<AllocatedImage> load_image(VulkanEngine* engine, fastgltf::Asset& asset, fastgltf::Image& image) {
        AllocatedImage newImage{};

        int width, height, nrChannels;

        std::visit(
            fastgltf::visitor{
            [](auto& arg) {},

            [&](fastgltf::sources::URI& filePath) {
                assert(filePath.fileByteOffset == 0);
                assert(filePath.uri.isLocalPath());

                const std::string path(filePath.uri.path().begin(), filePath.uri.path().end());
                unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 4);
                if (data) {
                    VkExtent3D imageSize;
                    imageSize.width = width;
                    imageSize.height = height;
                    imageSize.depth = 1;

                    newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
                    stbi_image_free(data);
                }
            },

            [&](fastgltf::sources::Vector& vector) {
                 unsigned char* data = stbi_load_from_memory(vector.bytes.data(), static_cast<int>(vector.bytes.size()), &width, &height, &nrChannels, 4);
                 if (data) {
                     VkExtent3D imageSize;
                     imageSize.width = width;
                     imageSize.height = height;
                     imageSize.depth = 1;

                     newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
                     stbi_image_free(data);
                 }
            },

            [&](fastgltf::sources::BufferView& view) {
                auto& bufferView = asset.bufferViews[view.bufferViewIndex];
                auto& buffer = asset.buffers[bufferView.bufferIndex];

                std::visit(fastgltf::visitor{
                    [](auto& arg) {},
                    [&](fastgltf::sources::Vector& vector) {
                        unsigned char* data = stbi_load_from_memory(vector.bytes.data() + bufferView.byteOffset, static_cast<int>(bufferView.byteLength), &width, &height, &nrChannels, 4);
                        if (data) {
                            VkExtent3D imageSize{};
                            imageSize.width = width;
                            imageSize.height = height;
                            imageSize.depth = 1;
                            newImage = engine->createImage(data, imageSize, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT);
                            stbi_image_free(data);
                        }
                    } },
                    buffer.data);
            } },
            image.data);

        if (newImage.image == VK_NULL_HANDLE) {
            return{};
        }
        else {
            return newImage;
        }
    }

std::optional<std::shared_ptr<LoadedGLTF>> load_gltf(VulkanEngine* engine, const std::filesystem::path& name) {
        std::filesystem::path path = MODEL_ROOT / name;
        fmt::print("[INFO] Loading GLTF: {}\n", path.string());
        std::shared_ptr<LoadedGLTF> scene = std::make_shared<LoadedGLTF>();
        scene->creator = engine;
        LoadedGLTF& file = *scene.get();
        fastgltf::Parser parser{};

        constexpr auto gltfOptions = fastgltf::Options::DontRequireValidAssetMember
            | fastgltf::Options::AllowDouble
            | fastgltf::Options::LoadExternalBuffers
            | fastgltf::Options::LoadGLBBuffers;

        fastgltf::GltfDataBuffer data;
        data.loadFromFile(path);
        fastgltf::Asset gltf;
        auto type = fastgltf::determineGltfFileType(&data);
        if (type == fastgltf::GltfType::glTF) {
            auto load = parser.loadGLTF(&data, path.parent_path(), gltfOptions);
            if (load) {
                gltf = std::move(load.get());
            }
            else {
                fmt::print("[GLTF ERROR] failed to load glTF {}\n", to_string(load.error()));
                return {};
            }
        }
        else if (type == fastgltf::GltfType::GLB) {
            auto load = parser.loadBinaryGLTF(&data, path.parent_path(), gltfOptions);
            if (load) {
                gltf = std::move(load.get());
            }
            else {
                fmt::print("[GLTF ERROR] failed to load glTF {}\n", to_string(load.error()));
                return {};
            }
        }
        else {
            fmt::print("[GLTF ERROR] failed to determine glTF container for {}\n", path.string());
            return {};
        }

        std::vector<DynamicDescriptorAllocator::PoolSizeRatio> sizes = {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 3 },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3 },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1 }
        };

        file.descriptorPool.init(engine->driver, gltf.materials.size(), sizes);

        VkSamplerCreateInfo sampl = { .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

        for (fastgltf::Sampler& sampler : gltf.samplers) {
            sampl.maxLod = VK_LOD_CLAMP_NONE;
            sampl.minLod = 0;
            sampl.magFilter = extract_filter(sampler.magFilter.value_or(fastgltf::Filter::Nearest));
            sampl.minFilter = extract_filter(sampler.minFilter.value_or(fastgltf::Filter::Nearest));
            sampl.mipmapMode = extract_mipmap_mode(sampler.minFilter.value_or(fastgltf::Filter::Nearest));

            VkSampler newSampler;
            VK_CHECK(vkCreateSampler(engine->driver, &sampl, nullptr, &newSampler));
            file.samplers.push_back(newSampler);
        }

        std::vector<std::shared_ptr<MeshAsset>> meshes;
        std::vector<std::shared_ptr<Node>> nodes;
        std::vector<AllocatedImage> images;
        std::vector<std::shared_ptr<GLTFMaterial>> materials;

        for (fastgltf::Image& image : gltf.images) {
            std::optional<AllocatedImage> img = load_image(engine, gltf, image);

            if (img.has_value()) {
                images.push_back(*img);
                file.images[image.name.c_str()] = *img;
            }
            else {
                images.push_back(engine->errorTexture);
                fmt::print("[GLTF ERROR] failed to load texture {}\n", image.name);
            }
        }

        file.materialDataBuffer = engine->createBuffer(sizeof(GLTFMetallic_Roughness::MaterialConstants) * gltf.materials.size(), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
        int data_index = 0;
        GLTFMetallic_Roughness::MaterialConstants* sceneMaterialConstants = (GLTFMetallic_Roughness::MaterialConstants*)file.materialDataBuffer.allocInfo.pMappedData;

        for (fastgltf::Material& mat : gltf.materials) {
            std::shared_ptr<GLTFMaterial> newMat = std::make_shared<GLTFMaterial>();
            materials.push_back(newMat);
            file.materials[mat.name.c_str()] = newMat;

            GLTFMetallic_Roughness::MaterialConstants constants;
            constants.colorFactor.x = mat.pbrData.baseColorFactor[0];
            constants.colorFactor.y = mat.pbrData.baseColorFactor[1];
            constants.colorFactor.z = mat.pbrData.baseColorFactor[2];
            constants.colorFactor.w = mat.pbrData.baseColorFactor[3];

            constants.metalRoughFactor.x = mat.pbrData.metallicFactor;
            constants.metalRoughFactor.y = mat.pbrData.roughnessFactor;

            sceneMaterialConstants[data_index] = constants;

            MaterialPass passType = MaterialPass::MAIN_COLOR;
            if (mat.alphaMode == fastgltf::AlphaMode::Blend) {
                passType = MaterialPass::TRASNPARENT;
            }

            GLTFMetallic_Roughness::MaterialResources materialResources;
            materialResources.colorImage = engine->whiteImage;
            materialResources.colorSampler = engine->defaultSamplerLinear;
            materialResources.metalRoughImage = engine->whiteImage;
            materialResources.metalRoughSampler = engine->defaultSamplerLinear;

            materialResources.dataBuffer = file.materialDataBuffer.buffer;
            materialResources.bufferOffset = data_index * sizeof(GLTFMetallic_Roughness::MaterialConstants);
            if (mat.pbrData.baseColorTexture.has_value()) {
                size_t img = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].imageIndex.value();
                size_t sampler = gltf.textures[mat.pbrData.baseColorTexture.value().textureIndex].samplerIndex.value();

                materialResources.colorImage = images[img];
                materialResources.colorSampler = file.samplers[sampler];
            }
            newMat->data = engine->metalRoughMat.writeMaterial(engine->driver, passType, materialResources, file.descriptorPool);

            data_index++;
        }

        std::vector<uint32_t> indices;
        std::vector<Vertex> vertices;

        for (fastgltf::Mesh& mesh : gltf.meshes) {
            std::shared_ptr<MeshAsset> newmesh = std::make_shared<MeshAsset>();
            meshes.push_back(newmesh);
            file.meshes[mesh.name.c_str()] = newmesh;
            newmesh->name = mesh.name;

            // clear the mesh arrays each mesh, we dont want to merge them by error
            indices.clear();
            vertices.clear();

            for (auto&& p : mesh.primitives) {
                GeoSurface newSurface;
                newSurface.startIndex = (uint32_t)indices.size();
                newSurface.count = (uint32_t)gltf.accessors[p.indicesAccessor.value()].count;

                size_t initial_vtx = vertices.size();

                // load indexes
                {
                    fastgltf::Accessor& indexaccessor = gltf.accessors[p.indicesAccessor.value()];
                    indices.reserve(indices.size() + indexaccessor.count);

                    fastgltf::iterateAccessor<std::uint32_t>(gltf, indexaccessor,
                        [&](std::uint32_t idx) {
                            indices.push_back(idx + initial_vtx);
                        });
                }

                // load vertex positions
                {
                    fastgltf::Accessor& posAccessor = gltf.accessors[p.findAttribute("POSITION")->second];
                    vertices.resize(vertices.size() + posAccessor.count);

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, posAccessor,
                        [&](glm::vec3 v, size_t index) {
                            Vertex newvtx;
                            newvtx.position = v;
                            newvtx.normal = { 1, 0, 0 };
                            newvtx.color = glm::vec4{ 1.f };
                            newvtx.uv_x = 0;
                            newvtx.uv_y = 0;
                            vertices[initial_vtx + index] = newvtx;
                        });
                }

                // load vertex normals
                auto normals = p.findAttribute("NORMAL");
                if (normals != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[(*normals).second],
                        [&](glm::vec3 v, size_t index) {
                            vertices[initial_vtx + index].normal = v;
                        });
                }

                // load UVs
                auto uv = p.findAttribute("TEXCOORD_0");
                if (uv != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[(*uv).second],
                        [&](glm::vec2 v, size_t index) {
                            vertices[initial_vtx + index].uv_x = v.x;
                            vertices[initial_vtx + index].uv_y = v.y;
                        });
                }

                // load vertex colors
                auto colors = p.findAttribute("COLOR_0");
                if (colors != p.attributes.end()) {

                    fastgltf::iterateAccessorWithIndex<glm::vec4>(gltf, gltf.accessors[(*colors).second],
                        [&](glm::vec4 v, size_t index) {
                            vertices[initial_vtx + index].color = v;
                        });
                }

                if (p.materialIndex.has_value()) {
                    newSurface.material = materials[p.materialIndex.value()];
                }
                else {
                    newSurface.material = materials[0];
                }

                glm::vec3 minpos = vertices[initial_vtx].position;
                glm::vec3 maxpos = vertices[initial_vtx].position;
                for (int i = initial_vtx; i < vertices.size(); i++) {
                    minpos = glm::min(minpos, vertices[i].position);
                    maxpos = glm::max(maxpos, vertices[i].position);
                }

                newSurface.bounds.origin = (maxpos + minpos) / 2.f;
                newSurface.bounds.extents = (maxpos - minpos) / 2.f;
                newSurface.bounds.sphereRadius = glm::length(newSurface.bounds.extents);

                newmesh->surfaces.push_back(newSurface);
            }

            newmesh->meshBuffers = engine->uploadMesh(indices, vertices);
        }

        for (fastgltf::Node& node : gltf.nodes) {
            std::shared_ptr<Node> newNode;
            if (node.meshIndex.has_value()) {
                newNode = std::make_shared<MeshNode>();
                static_cast<MeshNode*>(newNode.get())->mesh = meshes[*node.meshIndex];
            }
            else {
                newNode = std::make_shared<Node>();
            }

            nodes.push_back(newNode);
            file.nodes[node.name.c_str()];

            std::visit(fastgltf::visitor{

                [&](fastgltf::Node::TransformMatrix matrix) {
                    memcpy(&newNode->localTransform, matrix.data(), sizeof(matrix));
                },

                [&](fastgltf::Node::TRS transform) {
                    glm::vec3 tl(transform.translation[0], transform.translation[1],
                        transform.translation[2]);
                    glm::quat rot(transform.rotation[3], transform.rotation[0], transform.rotation[1],
                        transform.rotation[2]);
                    glm::vec3 sc(transform.scale[0], transform.scale[1], transform.scale[2]);

                    glm::mat4 tm = glm::translate(glm::mat4(1.f), tl);
                    glm::mat4 rm = glm::toMat4(rot);
                    glm::mat4 sm = glm::scale(glm::mat4(1.f), sc);
                    newNode->localTransform = tm * rm * sm;
                }

            }, node.transform);
        }

        for (int i = 0; i < gltf.nodes.size(); i++) {
            fastgltf::Node& node = gltf.nodes[i];
            std::shared_ptr<Node>& sceneNode = nodes[i];

            for (auto& c : node.children) {
                sceneNode->children.push_back(nodes[c]);
                nodes[c]->parent = sceneNode;
            }
        }

        for (auto& node : nodes) {
            if (node->parent.lock() == nullptr) {
                file.topNodes.push_back(node);
                node->refreshTransform(glm::mat4{ 1.f });
            }
        }

        return scene;
    }