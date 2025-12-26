#include "pnkr/engine.hpp"
#include "pnkr/renderer/rhi_renderer.hpp"
#include "pnkr/renderer/scene/VtxData.hpp"
#include "pnkr/renderer/scene/RHIScene.hpp"
#include "pnkr/renderer/scene/Camera.hpp"
#include "pnkr/renderer/scene/transform.hpp"
#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/rhi/rhi_pipeline_builder.hpp"
#include "pnkr/rhi/rhi_shader.hpp"
#include "pnkr/app/Application.hpp"

// GLM must be included BEFORE fastgltf/glm_element_traits.hpp
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <meshoptimizer.h>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/glm_element_traits.hpp>

#include <iostream>
#include <cstring>
#include <filesystem>
#include <algorithm>
#include <vector>
#include <limits>

#include "generated/unified.vert.h"

using namespace pnkr;
namespace fs = std::filesystem;

// Match VkDrawIndexedIndirectCommand layout
struct DrawIndexedIndirectCommand {
    uint32_t indexCount;
    uint32_t instanceCount;
    uint32_t firstIndex;
    int32_t  vertexOffset;
    uint32_t firstInstance;
};

// Helper to generate LODs
void processLods(std::vector<uint32_t>& indices, const std::vector<renderer::Vertex>& vertices, std::vector<std::vector<uint32_t>>& outLods)
{
    size_t targetIndices = indices.size();
    uint8_t lodLevel = 1;

    core::Logger::info("   LOD0: {} indices", indices.size());
    outLods.push_back(indices); // LOD 0 is original

    // Generate up to kMaxLODs (8)
    while (targetIndices > 1024 && lodLevel < renderer::scene::kMaxLODs) {
        targetIndices = indices.size() / 2; // Target 50% reduction per level

        // 1. Simplify
        std::vector<uint32_t> lodIndices(indices.size());
        
        size_t numOptIndices = meshopt_simplify(
            lodIndices.data(),
            indices.data(),
            indices.size(),
            &vertices[0].m_position.x, // Pointer to positions
            vertices.size(),
            sizeof(renderer::Vertex), // Stride
            targetIndices,
            1e-2f
        );

        // 2. Sloppy Simplification (if standard fails to reach target)
        if (static_cast<float>(numOptIndices) > static_cast<float>(indices.size()) * 0.9f) {
            if (lodLevel > 1) {
                numOptIndices = meshopt_simplifySloppy(
                    lodIndices.data(),
                    indices.data(),
                    indices.size(),
                    &vertices[0].m_position.x,
                    vertices.size(),
                    sizeof(renderer::Vertex),
                    targetIndices,
                    1e-1f
                );
            }
        }

        // Stop if we couldn't simplify significantly
        if (static_cast<float>(numOptIndices) > static_cast<float>(indices.size()) * 0.9f) break;

        lodIndices.resize(numOptIndices);

        // 3. Optimize Vertex Cache
        meshopt_optimizeVertexCache(lodIndices.data(), lodIndices.data(), lodIndices.size(), vertices.size());

        core::Logger::info("   LOD{}: {} indices", lodLevel, numOptIndices);
        outLods.push_back(lodIndices);
        
        // Use this LOD as base for next
        indices = lodIndices; 
        lodLevel++;
    }
}

class UnifiedMeshSample : public app::Application {
public:
    UnifiedMeshSample() 
        : app::Application({.title="RHI Unified Mesh (Indirect Rendering)", .width=1280, .height=720}) {}

    ~UnifiedMeshSample() override {
        if (m_renderer) {
            m_renderer->device()->waitIdle();
        }
    }

    renderer::scene::UnifiedMeshData m_meshData;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_globalVertexBuffer;
    std::unique_ptr<renderer::rhi::RHIBuffer> m_globalIndexBuffer;

    // Indirect Command Buffer
    std::unique_ptr<renderer::rhi::RHIBuffer> m_indirectCommandBuffer;
    uint32_t m_drawCount = 0;

    PipelineHandle m_pipeline;
    std::unique_ptr<renderer::scene::RHIScene> m_scene;

    float m_lodBias = 0.0f; 

    void onInit() override {
        // Init Scene (manages Camera, Skybox, Grid)
        m_scene = std::make_unique<renderer::scene::RHIScene>(*m_renderer);
        m_scene->initGrid();
        m_scene->enableGrid(true);

        // Setup Camera
        m_scene->cameraController().setPosition({2.0f, 2.0f, 2.0f});
        m_scene->cameraController().applyToCamera(m_scene->camera());
        
        // Load Skybox
        std::vector<fs::path> skyboxFaces = {
            baseDir() / "assets/skybox/posx.jpg",
            baseDir() / "assets/skybox/negx.jpg",
            baseDir() / "assets/skybox/negy.jpg",
            baseDir() / "assets/skybox/posy.jpg",
            baseDir() / "assets/skybox/posz.jpg",
            baseDir() / "assets/skybox/negz.jpg"
        };
        // Check if first exists
        if (fs::exists(skyboxFaces[0])) {
            m_scene->loadSkybox(skyboxFaces);
        } else {
            core::Logger::warn("Skybox assets not found at {}", skyboxFaces[0].string());
        }

        // --- Unified Mesh Loading ---
        fs::path cacheFile = baseDir() / "scene.mesh";
        fs::path sourceFile = baseDir() / "assets" / "Bistro.glb";
        
        // 1. Convert if cache doesn't exist
        if (!fs::exists(cacheFile)) {
            if (fs::exists(sourceFile)) {
                core::Logger::info("Generating cache from {}...", sourceFile.string());
                convertGLTFToUnified(sourceFile, cacheFile);
            } else {
                 core::Logger::warn("Asset not found: {}", sourceFile.string());
                 sourceFile = baseDir() / "assets" / "Bistro.glb";
                 if (fs::exists(sourceFile)) {
                     core::Logger::info("Generating cache from {}...", sourceFile.string());
                     convertGLTFToUnified(sourceFile, cacheFile);
                 } else {
                     throw cpptrace::runtime_error("No source asset (Bistro.glb or Duck.glb) found to generate cache.");
                 }
            }
        }

        // 2. Load the Monolithic Cache
        if (!renderer::scene::loadUnifiedMeshData(cacheFile.string().c_str(), m_meshData)) {
            throw cpptrace::runtime_error("Failed to load unified mesh data");
        }

        core::Logger::info("Loaded Unified Mesh: {} meshes, {} indices, {} KB vertices", 
            m_meshData.m_meshes.size(), m_meshData.m_indexData.size(), m_meshData.m_vertexData.size() / 1024);

        // 3. Upload to GPU
        m_globalIndexBuffer = m_renderer->device()->createBuffer({
            .size = m_meshData.m_indexData.size() * sizeof(uint32_t),
            .usage = renderer::rhi::BufferUsage::IndexBuffer,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
            .data = m_meshData.m_indexData.data(),
            .debugName = "Unified_IndexBuffer"
        });

        m_globalVertexBuffer = m_renderer->device()->createBuffer({
            .size = m_meshData.m_vertexData.size(),
            .usage = renderer::rhi::BufferUsage::VertexBuffer,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
            .data = m_meshData.m_vertexData.data(),
            .debugName = "Unified_VertexBuffer"
        });

        // 4. Create Indirect Command Buffer
        buildIndirectCommands();

        createPipeline();
    }

    void buildIndirectCommands() {
        std::vector<DrawIndexedIndirectCommand> commands;
        commands.reserve(m_meshData.m_meshes.size());

        // For this sample, we just pick LOD 0 for every mesh.
        // A real GPU-driven renderer would compute these commands on the GPU (Cull/LOD)
        for (const auto& mesh : m_meshData.m_meshes) {
            DrawIndexedIndirectCommand cmd{};
            
            // Default to LOD 0
            uint32_t lodIndex = 0; 
            
            cmd.indexCount = mesh.getLODIndicesCount(lodIndex);
            cmd.instanceCount = 1;
            cmd.firstIndex = mesh.indexOffset + mesh.m_lodOffset[lodIndex];
            cmd.vertexOffset = (int32_t)mesh.vertexOffset;
            cmd.firstInstance = 0;

            commands.push_back(cmd);
        }

        m_drawCount = (uint32_t)commands.size();

        // Create buffer with IndirectBuffer usage
        m_indirectCommandBuffer = m_renderer->device()->createBuffer({
            .size = commands.size() * sizeof(DrawIndexedIndirectCommand),
            .usage = renderer::rhi::BufferUsage::IndirectBuffer,
            .memoryUsage = renderer::rhi::MemoryUsage::GPUOnly,
            .data = commands.data(),
            .debugName = "Unified_IndirectBuffer"
        });
        
        core::Logger::info("Generated {} indirect draw commands", m_drawCount);
    }

    void convertGLTFToUnified(const fs::path& inputPath, const fs::path& outputPath) {
        fastgltf::Parser parser;
        auto data = fastgltf::GltfDataBuffer::FromPath(inputPath);
        if (data.error() != fastgltf::Error::None) {
            throw cpptrace::runtime_error("Failed to load GLTF data");
        }

        auto asset = parser.loadGltf(data.get(), inputPath.parent_path(), fastgltf::Options::LoadExternalBuffers);
        if (asset.error() != fastgltf::Error::None) {
            throw cpptrace::runtime_error("Failed to parse GLTF");
        }

        auto& gltf = asset.get();
        renderer::scene::UnifiedMeshData unified;
        std::vector<renderer::Vertex> allVertices;

        auto computeBounds = [](const std::vector<renderer::Vertex>& verts) -> renderer::scene::BoundingBox
        {
            renderer::scene::BoundingBox b{};
            if (verts.empty()) {
                b.m_min = glm::vec3(0.0f);
                b.m_max = glm::vec3(0.0f);
                return b;
            }

            glm::vec3 mn(std::numeric_limits<float>::max());
            glm::vec3 mx(std::numeric_limits<float>::lowest());

            for (const auto& v : verts) {
                mn = glm::min(mn, v.m_position);
                mx = glm::max(mx, v.m_position);
            }

            b.m_min = mn;
            b.m_max = mx;
            return b;
        };

        // Iterate all meshes
        for (const auto& mesh : gltf.meshes) {
            for (const auto& prim : mesh.primitives) {
                std::vector<renderer::Vertex> localVertices;
                std::vector<uint32_t> localIndices;

                auto* posIt = prim.findAttribute("POSITION");
                if (posIt == prim.attributes.end()) continue;

                size_t vertCount = gltf.accessors[posIt->accessorIndex].count;
                localVertices.resize(vertCount);

                fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[posIt->accessorIndex],
                    [&](glm::vec3 p, size_t i) {
                        localVertices[i].m_position = p;
                        localVertices[i].m_color = {1.0f, 1.0f, 1.0f};
                        localVertices[i].m_normal = {0, 1, 0};
                        localVertices[i].m_texCoord0 = {0, 0};
                        localVertices[i].m_texCoord1 = {0, 0};
                        localVertices[i].m_tangent = {0, 0, 0, 0};
                    });

                if (auto* normIt = prim.findAttribute("NORMAL"); normIt != prim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec3>(gltf, gltf.accessors[normIt->accessorIndex],
                        [&](glm::vec3 n, size_t i) { localVertices[i].m_normal = n; });
                }

                if (auto* uvIt = prim.findAttribute("TEXCOORD_0"); uvIt != prim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uvIt->accessorIndex],
                        [&](glm::vec2 uv, size_t i) { localVertices[i].m_texCoord0 = uv; });
                }

                if (auto* uvIt1 = prim.findAttribute("TEXCOORD_1"); uvIt1 != prim.attributes.end()) {
                    fastgltf::iterateAccessorWithIndex<glm::vec2>(gltf, gltf.accessors[uvIt1->accessorIndex],
                        [&](glm::vec2 uv, size_t i) { localVertices[i].m_texCoord1 = uv; });
                }

                if (prim.indicesAccessor.has_value()) {
                    auto& acc = gltf.accessors[prim.indicesAccessor.value()];
                    localIndices.reserve(acc.count);
                    
                     if (acc.componentType == fastgltf::ComponentType::UnsignedShort) {
                        fastgltf::iterateAccessor<uint16_t>(gltf, acc, [&](uint16_t v) { localIndices.push_back(v); });
                    } else if (acc.componentType == fastgltf::ComponentType::UnsignedByte) {
                        fastgltf::iterateAccessor<uint8_t>(gltf, acc, [&](uint8_t v) { localIndices.push_back(v); });
                    } else {
                        fastgltf::iterateAccessor<uint32_t>(gltf, acc, [&](uint32_t v) { localIndices.push_back(v); });
                    }

                } else {
                    for(size_t i=0; i<vertCount; ++i) localIndices.push_back((uint32_t)i);
                }

                std::vector<std::vector<uint32_t>> lods;
                processLods(localIndices, localVertices, lods);

                renderer::scene::UnifiedMesh uMesh{};
                uMesh.vertexOffset = (uint32_t)allVertices.size();
                uMesh.indexOffset = (uint32_t)unified.m_indexData.size();
                uMesh.vertexCount = (uint32_t)localVertices.size();
                uMesh.lodCount = (uint32_t)lods.size();

                uint32_t currentLodOffset = 0;
                for (size_t l = 0; l < lods.size(); ++l) {
                    uMesh.m_lodOffset[l] = currentLodOffset;
                    unified.m_indexData.insert(unified.m_indexData.end(), lods[l].begin(), lods[l].end());
                    currentLodOffset += (uint32_t)lods[l].size();
                }
                uMesh.m_lodOffset[lods.size()] = currentLodOffset;

                allVertices.insert(allVertices.end(), localVertices.begin(), localVertices.end());
                unified.m_meshes.push_back(uMesh);
                unified.m_boxes.push_back(computeBounds(localVertices));
            }
        }

        unified.m_vertexData.resize(allVertices.size() * sizeof(renderer::Vertex));
        std::memcpy(unified.m_vertexData.data(), allVertices.data(), unified.m_vertexData.size());

        renderer::scene::saveUnifiedMeshData(outputPath.string().c_str(), unified);
        core::Logger::info("Conversion complete. Saved to {}", outputPath.string());
    }

    void createPipeline() {
        renderer::rhi::ReflectionConfig reflect;
        auto vs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Vertex, getShaderPath("unified.vert.spv"), reflect);
        auto gs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Geometry, getShaderPath("unified.geom.spv"), reflect);
        auto fs = renderer::rhi::Shader::load(renderer::rhi::ShaderStage::Fragment, getShaderPath("unified.frag.spv"), reflect);

        auto builder = renderer::rhi::RHIPipelineBuilder()
            .setShaders(vs.get(), fs.get(), gs.get())
            .useVertexType<renderer::Vertex>()
            .setTopology(renderer::rhi::PrimitiveTopology::TriangleList)
            .setCullMode(renderer::rhi::CullMode::Back, true)
            .enableDepthTest()
            .setColorFormat(m_renderer->getDrawColorFormat())
            .setDepthFormat(m_renderer->getDrawDepthFormat())
            .setName("UnifiedPipeline");

        m_pipeline = m_renderer->createGraphicsPipeline(builder.buildGraphics());
    }

    void onUpdate(float dt) override {

        m_scene->cameraController().update(m_input, dt);
        m_scene->cameraController().applyToCamera(m_scene->camera());

        float aspect = (float)m_window.width() / (float)m_window.height();
        m_scene->camera().setPerspective(glm::radians(45.0F), aspect, 0.1F, 1000.0F);

        m_scene->update(dt, m_window.width(), m_window.height());

    }

    void onRecord(const renderer::RHIFrameContext& ctx) override {
        auto* cmd = ctx.commandBuffer;

        m_renderer->bindPipeline(ctx.commandBuffer, m_pipeline);
        cmd->bindVertexBuffer(0, m_globalVertexBuffer.get(), 0);
        cmd->bindIndexBuffer(m_globalIndexBuffer.get(), 0, false); 

        ShaderGen::unified_vert::unified_vert_PushConstants pc{};
        pc.viewProj = m_scene->camera().viewProj();
        pc.model = glm::mat4(1.0f); // Identity for the whole batch

        m_renderer->pushConstants(ctx.commandBuffer, m_pipeline, 
                                   renderer::rhi::ShaderStage::Vertex, 
                                   pc);

        // Execute Indirect Draw
        if (m_indirectCommandBuffer) {
            cmd->drawIndexedIndirect(
                m_indirectCommandBuffer.get(), 
                0, // offset
                m_drawCount, 
                sizeof(DrawIndexedIndirectCommand) // stride
            );
        }

        // 2. Draw Scene Elements (Skybox, Grid)
        // These will draw on top/behind based on depth test
        m_scene->render(cmd);
    }
};

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    
    UnifiedMeshSample app;
    return app.run();
}

