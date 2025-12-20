#include "pnkr/renderer/scene/VtxData.hpp"
#include "pnkr/core/common.hpp"
#include <cstdio>
#include <stdexcept>

namespace pnkr::renderer::scene
{
    static constexpr uint32_t kMagic = 0x12345678;

    bool loadUnifiedMeshData(const char* meshFile, UnifiedMeshData& out)
    {
        FILE* f = nullptr;
        // Check for fopen_s availability or use standard fopen
#ifdef _MSC_VER
        fopen_s(&f, meshFile, "rb");
#else
        f = fopen(meshFile, "rb");
#endif
        if (!f) return false;
        auto guard = util::makeScopeGuard([&] { fclose(f); });

        MeshFileHeader header{}; // value-init
        if (fread(&header, 1, sizeof(header), f) != sizeof(header)) return false;
        if (header.m_magicValue != kMagic) return false;
        if ((header.m_indexDataSize % sizeof(uint32_t)) != 0) return false;

        out = {}; // avoid partial outputs on failure

        out.m_meshes.resize(header.m_meshCount);
        out.m_boxes.resize(header.m_meshCount);

        if (header.m_meshCount > 0)
        {
            if (fread(out.m_meshes.data(), sizeof(UnifiedMesh), header.m_meshCount, f) != header.m_meshCount) return false;
            if (fread(out.m_boxes.data(),  sizeof(BoundingBox), header.m_meshCount, f) != header.m_meshCount) return false;
        }

        if (header.m_indexDataSize > 0)
        {
            const size_t indexCount = header.m_indexDataSize / sizeof(uint32_t);
            out.m_indexData.resize(indexCount);
            if (fread(out.m_indexData.data(), 1, header.m_indexDataSize, f) != header.m_indexDataSize) return false;
        }

        if (header.m_vertexDataSize > 0)
        {
            out.m_vertexData.resize(header.m_vertexDataSize);
            if (fread(out.m_vertexData.data(), 1, header.m_vertexDataSize, f) != header.m_vertexDataSize) return false;
        }

        return true;
    }

    void saveUnifiedMeshData(const char* filename, const UnifiedMeshData& data)
    {
        if (data.m_boxes.size() != data.m_meshes.size())
            throw std::runtime_error("UnifiedMeshData invariant failed: m_boxes.size() must equal m_meshes.size()");

        FILE* f = nullptr;
#ifdef _MSC_VER
        fopen_s(&f, filename, "wb");
#else
        f = fopen(filename, "wb");
#endif
        if (!f) throw std::runtime_error("Failed to open file for writing");
        auto guard = util::makeScopeGuard([&] { fclose(f); });

        MeshFileHeader header{};
        header.m_magicValue     = kMagic;
        header.m_meshCount      = static_cast<uint32_t>(data.m_meshes.size());
        header.m_indexDataSize  = static_cast<uint32_t>(data.m_indexData.size() * sizeof(uint32_t));
        header.m_vertexDataSize = static_cast<uint32_t>(data.m_vertexData.size());

        if (fwrite(&header, 1, sizeof(header), f) != sizeof(header))
            throw std::runtime_error("Write failed (header)");

        if (header.m_meshCount > 0)
        {
            if (fwrite(data.m_meshes.data(), sizeof(UnifiedMesh), header.m_meshCount, f) != header.m_meshCount)
                throw std::runtime_error("Write failed (meshes)");
            if (fwrite(data.m_boxes.data(), sizeof(BoundingBox), header.m_meshCount, f) != header.m_meshCount)
                throw std::runtime_error("Write failed (boxes)");
        }

        if (header.m_indexDataSize > 0)
        {
            if (fwrite(data.m_indexData.data(), 1, header.m_indexDataSize, f) != header.m_indexDataSize)
                throw std::runtime_error("Write failed (index data)");
        }

        if (header.m_vertexDataSize > 0)
        {
            if (fwrite(data.m_vertexData.data(), 1, header.m_vertexDataSize, f) != header.m_vertexDataSize)
                throw std::runtime_error("Write failed (vertex data)");
        }
    }
}