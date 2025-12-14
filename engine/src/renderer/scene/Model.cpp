#include "pnkr/renderer/scene/Model.hpp"
#include "pnkr/renderer/renderer.hpp"
#include "pnkr/renderer/vulkan/geometry/Vertex.h"
#include "pnkr/core/logger.hpp"


#include <cstdint>
#include <functional>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/vec4.hpp>

namespace pnkr::renderer::scene {

// --- Helpers ---

static glm::mat4 toMat4(const std::vector<double>& matrix) {
    if (matrix.size() != 16) return glm::mat4(1.0f);
    // glTF is column-major, glm::make_mat4 expects column-major.
    float fMat[16];
    for(int i=0; i<16; ++i) fMat[i] = static_cast<float>(matrix[i]);
    return glm::make_mat4(fMat);
}

static Transform toTransform(const tinygltf::Node& node) {
    Transform t;
    if (node.matrix.size() == 16) {
        // Decompose matrix if provided
        glm::mat4 m = toMat4(node.matrix);
        glm::vec3 skew; 
        glm::vec4 perspective;
        glm::decompose(m, t.m_scale, t.m_rotation, t.m_translation, skew, perspective);
    } else {
        if (node.translation.size() == 3) {
            t.m_translation = glm::vec3(static_cast<float>(node.translation[0]),
                                        static_cast<float>(node.translation[1]),
                                        static_cast<float>(node.translation[2]));
        }
        if (node.rotation.size() == 4) {
            // glTF is (x, y, z, w)
            t.m_rotation = glm::quat(
                static_cast<float>(node.rotation[3]), // w
                static_cast<float>(node.rotation[0]), // x
                static_cast<float>(node.rotation[1]), // y
                static_cast<float>(node.rotation[2])  // z
            );
        }
        if (node.scale.size() == 3) {
            t.m_scale = glm::vec3(static_cast<float>(node.scale[0]),
                                  static_cast<float>(node.scale[1]),
                                  static_cast<float>(node.scale[2]));
        }
    }
    return t;
}

// Helper to access buffer data
const unsigned char* getBufferData(const tinygltf::Model& model, int accessorIndex) {
    const auto& accessor = model.accessors[accessorIndex];
    if (accessor.bufferView < 0) return nullptr;
    const auto& bufferView = model.bufferViews[accessor.bufferView];
    const auto& buffer = model.buffers[bufferView.buffer];
    return buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
}

// --- Main Loader ---

std::unique_ptr<Model> Model::load(Renderer& renderer, const std::filesystem::path& path) {
    tinygltf::Model gltfModel;
    tinygltf::TinyGLTF loader;
    std::string err, warn;
    bool ret = false;

    if (path.extension() == ".glb") 
        ret = loader.LoadBinaryFromFile(&gltfModel, &err, &warn, path.string());
    else 
        ret = loader.LoadASCIIFromFile(&gltfModel, &err, &warn, path.string());

    if (!warn.empty()) core::Logger::warn("glTF Warning: {}", warn);
    if (!err.empty()) core::Logger::error("glTF Error: {}", err);
    if (!ret) {
        core::Logger::error("Failed to load glTF: {}", path.string());
        return nullptr;
    }

    auto model = std::make_unique<Model>();
    core::Logger::info("Loading Model: {} (Nodes: {})", path.string(), gltfModel.nodes.size());

    // Determine texture color space usage per glTF material references
    std::vector<bool> textureIsSrgb(gltfModel.textures.size(), true);
    for (const auto& mat : gltfModel.materials) {
        const auto& pbr = mat.pbrMetallicRoughness;
        if (pbr.baseColorTexture.index >= 0 &&
            static_cast<size_t>(pbr.baseColorTexture.index) < textureIsSrgb.size()) {
            textureIsSrgb[pbr.baseColorTexture.index] = true;
        }
        if (pbr.metallicRoughnessTexture.index >= 0 &&
            static_cast<size_t>(pbr.metallicRoughnessTexture.index) < textureIsSrgb.size()) {
            textureIsSrgb[pbr.metallicRoughnessTexture.index] = false;
        }
        if (mat.normalTexture.index >= 0 &&
            static_cast<size_t>(mat.normalTexture.index) < textureIsSrgb.size()) {
            textureIsSrgb[mat.normalTexture.index] = false;
        }
        if (mat.occlusionTexture.index >= 0 &&
            static_cast<size_t>(mat.occlusionTexture.index) < textureIsSrgb.size()) {
            textureIsSrgb[mat.occlusionTexture.index] = false;
        }
        if (mat.emissiveTexture.index >= 0 &&
            static_cast<size_t>(mat.emissiveTexture.index) < textureIsSrgb.size()) {
            textureIsSrgb[mat.emissiveTexture.index] = true;
        }
    }

    // 1. Load Textures
    // This is simplified. Real engine needs path resolution.
    // 1. Load Textures
    for (size_t texIdx = 0; texIdx < gltfModel.textures.size(); ++texIdx) {
        const auto& tex = gltfModel.textures[texIdx];
        const bool srgb = (texIdx < textureIsSrgb.size()) ? textureIsSrgb[texIdx] : true;
        if (tex.source > -1) {
            const auto& img = gltfModel.images[tex.source];

            // Check if image data is already loaded in 'image' buffer
            // tinygltf automatically loads embedded images into img.image vector
            if (!img.image.empty()) {
                // Determine format
                // tinygltf usually decodes to RGBA 8-bit
                // width = img.width, height = img.height, components = img.component

                // We need a new function in Renderer: createTextureFromMemory
                // But for now, let's look at how we can reuse loadTexture or add a new method.

                // OPTION A: Add createTextureFromPixels to Renderer (CLEANEST)
                TextureHandle hTex = renderer.createTextureFromPixels(
                    img.image.data(),
                    img.width,
                    img.height,
                    img.component, // usually 4 for RGBA
                    srgb
                );
                model->m_textures.push_back(hTex);
                core::Logger::info("Loaded embedded texture: {}x{}", img.width, img.height);

            } else if (!img.uri.empty()) {
                // External file
                auto texPath = path.parent_path() / img.uri;
                model->m_textures.push_back(renderer.loadTexture(texPath.string(), srgb));
            } else {
                core::Logger::warn("Texture has no data or URI");
                model->m_textures.push_back(INVALID_TEXTURE_HANDLE);
            }
        } else {
            model->m_textures.push_back(INVALID_TEXTURE_HANDLE);
        }
    }


    // 2. Load Materials
    for (const auto& mat : gltfModel.materials) {
        MaterialData md;
        auto& pbr = mat.pbrMetallicRoughness;
        md.baseColorFactor = glm::vec4(
            static_cast<float>(pbr.baseColorFactor[0]),
            static_cast<float>(pbr.baseColorFactor[1]),
            static_cast<float>(pbr.baseColorFactor[2]),
            static_cast<float>(pbr.baseColorFactor[3]));
        
        if (pbr.baseColorTexture.index > -1) {
             md.baseColorTexture = model->m_textures[pbr.baseColorTexture.index];
        } else {
             // Use invalid handle to signal "use default white texture" or "use color only"
             md.baseColorTexture = INVALID_TEXTURE_HANDLE;
        }
        model->m_materials.push_back(md);
    }
    // Ensure default material if none exist
    if (model->m_materials.empty()) {
        model->m_materials.push_back(MaterialData{});
    }

    // 3. Load Meshes & Nodes
    model->m_nodes.resize(gltfModel.nodes.size());
    
    for (size_t i = 0; i < gltfModel.nodes.size(); ++i) {
        const auto& gNode = gltfModel.nodes[i];
        auto& myNode = model->m_nodes[i];
        
        myNode.name = gNode.name;
        myNode.localTransform = toTransform(gNode);
        myNode.children = gNode.children;

        // Set parent index
        for (int childIdx : myNode.children) {
            if (childIdx >= 0 && static_cast<size_t>(childIdx) < model->m_nodes.size()) {
                model->m_nodes[childIdx].parentIndex = static_cast<int>(i);
            }
        }

        // Process Mesh
        if (gNode.mesh > -1) {
            const auto& gMesh = gltfModel.meshes[gNode.mesh];
            std::vector<MeshPrimitive> primitives;

            for (const auto& gPrim : gMesh.primitives) {
                // Extract Attributes
                std::vector<Vertex> vertices;
                std::vector<uint32_t> indices;

                // --- Vertex Extraction (same logic as before) ---
                const float* posBuffer = nullptr;
                const float* uvBuffer = nullptr;
                size_t vCount = 0;

                // Position
                auto itPos = gPrim.attributes.find("POSITION");
                if (itPos != gPrim.attributes.end()) {
                    const auto& acc = gltfModel.accessors[itPos->second];
                    vCount = acc.count;
                    posBuffer = reinterpret_cast<const float*>(getBufferData(gltfModel, itPos->second));
                }

                // UV
                auto itUV = gPrim.attributes.find("TEXCOORD_0");
                if (itUV != gPrim.attributes.end()) {
                    uvBuffer = reinterpret_cast<const float*>(getBufferData(gltfModel, itUV->second));
                }

                if (vCount == 0 || posBuffer == nullptr) continue;

                vertices.resize(vCount);
                for(size_t k=0; k<vCount; ++k) {
                    vertices[k].m_position = glm::vec3(posBuffer[k*3+0], posBuffer[k*3+1], posBuffer[k*3+2]);
                    vertices[k].m_color = glm::vec3(1.0f); // Default white
                    if (uvBuffer) {
                        vertices[k].m_texCoord = glm::vec2(uvBuffer[k*2+0], uvBuffer[k*2+1]);
                    } else {
                        vertices[k].m_texCoord = glm::vec2(0.0f);
                    }
                }

                // Indices
                if (gPrim.indices > -1) {
                    const auto& acc = gltfModel.accessors[gPrim.indices];
                    const void* data = getBufferData(gltfModel, gPrim.indices);
                    if (data != nullptr) {
                        if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT) {
                            const uint16_t* buf = static_cast<const uint16_t*>(data);
                            indices.assign(buf, buf + acc.count);
                        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT) {
                            const uint32_t* buf = static_cast<const uint32_t*>(data);
                            indices.assign(buf, buf + acc.count);
                        } else if (acc.componentType == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE) {
                            const uint8_t* buf = static_cast<const uint8_t*>(data);
                            indices.assign(buf, buf + acc.count);
                        }
                    }
                }

                MeshHandle hMesh = renderer.createMesh(vertices, indices);
                
                MeshPrimitive prim;
                prim.mesh = hMesh;
                prim.materialIndex = (gPrim.material > -1) ? static_cast<uint32_t>(gPrim.material) : 0;
                primitives.push_back(prim);
            }
            myNode.meshPrimitives = std::move(primitives);
        }
    }

    // Identify root nodes
    if (!gltfModel.scenes.empty()) {
        const int sceneIndex = (gltfModel.defaultScene >= 0 &&
                                gltfModel.defaultScene < static_cast<int>(gltfModel.scenes.size()))
                                   ? gltfModel.defaultScene
                                   : 0;
        const auto& scene = gltfModel.scenes[sceneIndex];
        model->m_rootNodes.assign(scene.nodes.begin(), scene.nodes.end());
    } else {
        for (size_t i = 0; i < model->m_nodes.size(); ++i) {
            if (model->m_nodes[i].parentIndex == -1) {
                model->m_rootNodes.push_back(static_cast<int>(i));
            }
        }
    }

    model->updateTransforms();

    return model;
}

void Model::updateTransforms() {
    std::function<void(int, const glm::mat4&)> updateNode = 
        [&](int nodeIdx, const glm::mat4& parentMat) {
            auto& node = m_nodes[nodeIdx];
            glm::mat4 localMat = node.localTransform.mat4();
            glm::mat4 worldMat = parentMat * localMat;
            
            // Decompose back to TRS for worldTransform
            // For efficiency, you might just store the mat4 in Node
            glm::vec3 s, t, skew; glm::vec4 p; glm::quat r;
            glm::decompose(worldMat, s, r, t, skew, p);
            node.worldTransform.m_translation = t;
            node.worldTransform.m_rotation = r;
            node.worldTransform.m_scale = s;

            for (int child : node.children) {
                if (child >= 0 && static_cast<size_t>(child) < m_nodes.size()) {
                    updateNode(child, worldMat);
                }
            }
        };

    for (int root : m_rootNodes) {
        updateNode(root, glm::mat4(1.0f));
    }
}

} // namespace pnkr::renderer::scene
