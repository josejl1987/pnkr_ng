#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
#include <limits>
#include <type_traits>
#include "pnkr/core/logger.hpp"

namespace pnkr::core {

    struct CacheHeader {
        uint32_t magic   = 0x504E4B52; // 'PNKR'
        uint16_t version = 1;
        uint16_t endian  = 1; // 1 little
        uint32_t chunkCount = 0;
    };

    struct ChunkHeader {
        uint32_t fourcc;   // e.g. 'MATL','TXFN','SCNH','SLOC', etc.
        uint16_t version;  // per-chunk version
        uint16_t flags;    // compression, etc. (optional)
        uint64_t sizeBytes;
    };

    class CacheWriter {
    public:
        explicit CacheWriter(const std::string& path) {
            m_path = path;
            m_file.open(path, std::ios::binary);
            if (m_file.is_open()) {
                // Placeholder for header
                m_file.write(reinterpret_cast<const char*>(&m_header), sizeof(CacheHeader));
            }
        }

        ~CacheWriter() {
            if (m_file.is_open()) {
                // Update header with final chunk count
                m_file.seekp(0);
                m_file.write(reinterpret_cast<const char*>(&m_header), sizeof(CacheHeader));
                m_file.close();
            }
        }

        bool isOpen() const { return m_file.is_open(); }

        template <typename T>
        void writeChunk(uint32_t fourcc, uint16_t version, const std::vector<T>& data) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for writeChunk");
            
            ChunkHeader chunk{};
            chunk.fourcc = fourcc;
            chunk.version = version;
            chunk.sizeBytes = data.size() * sizeof(T);
            
            m_file.write(reinterpret_cast<const char*>(&chunk), sizeof(ChunkHeader));
            if (!data.empty()) {
                m_file.write(reinterpret_cast<const char*>(data.data()), chunk.sizeBytes);
            }
            m_header.chunkCount++;
        }

        template <typename T>
        void writeSparseSet(uint32_t fourcc, uint16_t version, const ecs::SparseSet<T>& ss) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for writeSparseSet");
            
            const auto& dense = ss.getDense();
            const auto& packed = ss.entities();
            
            uint64_t denseSize = (uint64_t)dense.size();
            uint64_t totalBytes = sizeof(uint64_t) + denseSize * sizeof(T) + denseSize * sizeof(ecs::Entity);

            ChunkHeader chunk{};
            chunk.fourcc = fourcc;
            chunk.version = version;
            chunk.sizeBytes = totalBytes;

            m_file.write(reinterpret_cast<const char*>(&chunk), sizeof(ChunkHeader));
            m_file.write(reinterpret_cast<const char*>(&denseSize), sizeof(denseSize));
            if (denseSize > 0) {
                m_file.write(reinterpret_cast<const char*>(dense.data()), denseSize * sizeof(T));
                m_file.write(reinterpret_cast<const char*>(packed.data()), denseSize * sizeof(ecs::Entity));
            }
            m_header.chunkCount++;
        }

        void writeStringListChunk(uint32_t fourcc, uint16_t version, const std::vector<std::string>& strings) {
            // First calculate total size
            uint64_t totalBytes = sizeof(uint64_t); // num strings
            for (const auto& s : strings) {
                totalBytes += sizeof(uint64_t) + s.size();
            }

            ChunkHeader chunk{};
            chunk.fourcc = fourcc;
            chunk.version = version;
            chunk.sizeBytes = totalBytes;

            m_file.write(reinterpret_cast<const char*>(&chunk), sizeof(ChunkHeader));
            
            uint64_t n = strings.size();
            m_file.write(reinterpret_cast<const char*>(&n), sizeof(n));
            for (const auto& s : strings) {
                uint64_t len = s.size();
                m_file.write(reinterpret_cast<const char*>(&len), sizeof(len));
                m_file.write(s.data(), len);
            }
            m_header.chunkCount++;
        }

        template <typename T, typename Serializer>
        void writeCustomSparseSet(uint32_t fourcc, uint16_t version, const ecs::SparseSet<T>& ss, Serializer serializer) {
            const auto& dense = ss.getDense();
            const auto& packed = ss.entities();
            uint64_t denseSize = (uint64_t)dense.size();

            ChunkHeader chunk{};
            chunk.fourcc = fourcc;
            chunk.version = version;
            
            size_t headerPos = m_file.tellp();
            m_file.write(reinterpret_cast<const char*>(&chunk), sizeof(ChunkHeader));
            
            size_t dataStart = m_file.tellp();
            m_file.write(reinterpret_cast<const char*>(&denseSize), sizeof(denseSize));
            for (size_t i = 0; i < dense.size(); ++i) {
                m_file.write(reinterpret_cast<const char*>(&packed[i]), sizeof(ecs::Entity));
                serializer(m_file, dense[i]);
            }
            size_t dataEnd = m_file.tellp();
            
            chunk.sizeBytes = (uint64_t)(dataEnd - dataStart);
            m_file.seekp(headerPos);
            m_file.write(reinterpret_cast<const char*>(&chunk), sizeof(ChunkHeader));
            m_file.seekp(dataEnd);

            m_header.chunkCount++;
        }

    private:
        std::string m_path;
        std::ofstream m_file;
        CacheHeader m_header;
    };

    class CacheReader {
    public:
        explicit CacheReader(const std::string& path) {
            m_file.open(path, std::ios::binary);
            if (m_file.is_open()) {
                m_file.seekg(0, std::ios::end);
                const std::streamoff fileSize = m_file.tellg();
                if (fileSize < static_cast<std::streamoff>(sizeof(CacheHeader))) {
                    ::pnkr::core::Logger::error("Cache file too small: {}", path);
                    m_file.close();
                    return;
                }
                m_fileSize = static_cast<uint64_t>(fileSize);
                m_file.seekg(0, std::ios::beg);

                if (!m_file.read(reinterpret_cast<char*>(&m_header), sizeof(CacheHeader))) {
                    ::pnkr::core::Logger::error("Failed to read cache header: {}", path);
                    m_file.close();
                    return;
                }
                if (m_header.magic != 0x504E4B52) {
                    ::pnkr::core::Logger::error("Invalid cache magic in {}", path);
                    m_file.close();
                    return;
                }
                if (m_header.chunkCount > kMaxChunkCount) {
                    ::pnkr::core::Logger::error("Cache chunkCount too large in {} ({} > {})",
                                                path, m_header.chunkCount, kMaxChunkCount);
                    m_file.close();
                    return;
                }
                m_valid = true;
            }
        }

        bool isOpen() const { return m_file.is_open(); }
        const CacheHeader& header() const { return m_header; }

        struct ChunkInfo {
            ChunkHeader header;
            uint64_t offset;
        };

        std::vector<ChunkInfo> listChunks() {
            std::vector<ChunkInfo> chunks;
            if (!m_valid) {
                return chunks;
            }
            m_file.seekg(sizeof(CacheHeader));
            for (uint32_t i = 0; i < m_header.chunkCount; ++i) {
                ChunkInfo info{};
                const std::streamoff offset = m_file.tellg();
                if (offset < 0) {
                    return {};
                }
                info.offset = static_cast<uint64_t>(offset);
                if (!canRead(info.offset, sizeof(ChunkHeader))) {
                    return {};
                }
                if (!m_file.read(reinterpret_cast<char*>(&info.header), sizeof(ChunkHeader))) {
                    return {};
                }
                if (info.header.sizeBytes > kMaxChunkBytes ||
                    !canRead(info.offset + sizeof(ChunkHeader), info.header.sizeBytes)) {
                    ::pnkr::core::Logger::error("Invalid cache chunk size {}", info.header.sizeBytes);
                    return {};
                }
                chunks.push_back(info);
                m_file.seekg(info.header.sizeBytes, std::ios::cur);
            }
            return chunks;
        }

        template <typename T>
        bool readChunk(const ChunkInfo& info, std::vector<T>& data) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for readChunk");
            if (info.header.sizeBytes % sizeof(T) != 0) return false;
            if (!m_valid || info.header.sizeBytes > kMaxChunkBytes) return false;
            if (!canRead(info.offset + sizeof(ChunkHeader), info.header.sizeBytes)) return false;
            if (info.header.sizeBytes > std::numeric_limits<size_t>::max()) return false;

            const size_t count = static_cast<size_t>(info.header.sizeBytes / sizeof(T));
            data.resize(count);
            m_file.seekg(info.offset + sizeof(ChunkHeader));
            if (count > 0) {
                if (!m_file.read(reinterpret_cast<char*>(data.data()), info.header.sizeBytes)) {
                    return false;
                }
            }
            return true;
        }

        template <typename T>
        bool readSparseSet(const ChunkInfo& info, ecs::SparseSet<T>& ss) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for readSparseSet");
            if (!m_valid || info.header.sizeBytes > kMaxChunkBytes) return false;
            if (!canRead(info.offset + sizeof(ChunkHeader), info.header.sizeBytes)) return false;

            m_file.seekg(info.offset + sizeof(ChunkHeader));
            uint64_t denseSize = 0;
            if (!m_file.read(reinterpret_cast<char*>(&denseSize), sizeof(denseSize))) return false;

            if (denseSize > 0) {
                std::vector<T> dense(denseSize);
                std::vector<ecs::Entity> packed(denseSize);
                if (!m_file.read(reinterpret_cast<char*>(dense.data()), denseSize * sizeof(T))) return false;
                if (!m_file.read(reinterpret_cast<char*>(packed.data()), denseSize * sizeof(ecs::Entity))) return false;

                for (size_t i = 0; i < denseSize; ++i) {
                    ss.emplace(packed[i], dense[i]);
                }
            }
            return true;
        }

        bool readStringListChunk(const ChunkInfo& info, std::vector<std::string>& strings) {
            if (!m_valid || info.header.sizeBytes > kMaxChunkBytes) return false;
            if (!canRead(info.offset + sizeof(ChunkHeader), info.header.sizeBytes)) return false;
            m_file.seekg(info.offset + sizeof(ChunkHeader));
            uint64_t n = 0;
            if (!m_file.read(reinterpret_cast<char*>(&n), sizeof(n))) {
                return false;
            }
            if (n > kMaxStringCount) {
                return false;
            }
            strings.resize(static_cast<size_t>(n));
            uint64_t bytesRead = sizeof(n);
            for (uint64_t i = 0; i < n; ++i) {
                uint64_t len = 0;
                if (!m_file.read(reinterpret_cast<char*>(&len), sizeof(len))) {
                    return false;
                }
                bytesRead += sizeof(len);
                if (len > kMaxStringBytes || bytesRead + len > info.header.sizeBytes) {
                    return false;
                }
                strings[static_cast<size_t>(i)].resize(static_cast<size_t>(len));
                if (!m_file.read(strings[static_cast<size_t>(i)].data(),
                                 static_cast<std::streamsize>(len))) {
                    return false;
                }
                bytesRead += len;
            }
            return true;
        }

        template <typename T, typename Deserializer>
        bool readCustomSparseSet(const ChunkInfo& info, ecs::SparseSet<T>& ss, Deserializer deserializer) {
            if (!m_valid) return false;
            m_file.seekg(info.offset + sizeof(ChunkHeader));
            
            uint64_t denseSize = 0;
            if (!m_file.read(reinterpret_cast<char*>(&denseSize), sizeof(denseSize))) return false;

            for (size_t i = 0; i < denseSize; ++i) {
                ecs::Entity e;
                if (!m_file.read(reinterpret_cast<char*>(&e), sizeof(ecs::Entity))) return false;
                T comp;
                deserializer(m_file, comp);
                ss.emplace(e, std::move(comp));
            }
            return true;
        }

    private:
        static constexpr uint32_t kMaxChunkCount = 16384;
        static constexpr uint64_t kMaxChunkBytes = 256ull * 1024ull * 1024ull;
        static constexpr uint64_t kMaxStringCount = 65535;
        static constexpr uint64_t kMaxStringBytes = 16ull * 1024ull * 1024ull;

        bool canRead(uint64_t offset, uint64_t size) const {
            if (!m_file.is_open()) return false;
            if (offset > m_fileSize) return false;
            if (size > m_fileSize) return false;
            return offset + size <= m_fileSize;
        }

        std::ifstream m_file;
        CacheHeader m_header;
        uint64_t m_fileSize = 0;
        bool m_valid = false;
    };

    // Helper to create FourCC
    constexpr uint32_t makeFourCC(const char* s) {
        return (static_cast<uint32_t>(s[0]) << 0) |
               (static_cast<uint32_t>(s[1]) << 8) |
               (static_cast<uint32_t>(s[2]) << 16) |
               (static_cast<uint32_t>(s[3]) << 24);
    }

} // namespace pnkr::core
