#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <cstdint>
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
        explicit CacheWriter(const std::string& path) : m_path(path) {
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
                m_file.read(reinterpret_cast<char*>(&m_header), sizeof(CacheHeader));
                if (m_header.magic != 0x504E4B52) {
                    ::pnkr::core::Logger::error("Invalid cache magic in {}", path);
                    m_file.close();
                }
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
            m_file.seekg(sizeof(CacheHeader));
            for (uint32_t i = 0; i < m_header.chunkCount; ++i) {
                ChunkInfo info{};
                info.offset = m_file.tellg();
                m_file.read(reinterpret_cast<char*>(&info.header), sizeof(ChunkHeader));
                chunks.push_back(info);
                m_file.seekg(info.header.sizeBytes, std::ios::cur);
            }
            return chunks;
        }

        template <typename T>
        bool readChunk(const ChunkInfo& info, std::vector<T>& data) {
            static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable for readChunk");
            if (info.header.sizeBytes % sizeof(T) != 0) return false;
            
            size_t count = info.header.sizeBytes / sizeof(T);
            data.resize(count);
            m_file.seekg(info.offset + sizeof(ChunkHeader));
            if (count > 0) {
                m_file.read(reinterpret_cast<char*>(data.data()), info.header.sizeBytes);
            }
            return true;
        }

        bool readStringListChunk(const ChunkInfo& info, std::vector<std::string>& strings) {
            m_file.seekg(info.offset + sizeof(ChunkHeader));
            uint64_t n = 0;
            m_file.read(reinterpret_cast<char*>(&n), sizeof(n));
            strings.resize(static_cast<size_t>(n));
            for (uint64_t i = 0; i < n; ++i) {
                uint64_t len = 0;
                m_file.read(reinterpret_cast<char*>(&len), sizeof(len));
                strings[static_cast<size_t>(i)].resize(static_cast<size_t>(len));
                m_file.read(strings[static_cast<size_t>(i)].data(), static_cast<std::streamsize>(len));
            }
            return true;
        }

    private:
        std::ifstream m_file;
        CacheHeader m_header;
    };

    // Helper to create FourCC
    constexpr uint32_t makeFourCC(const char* s) {
        return (static_cast<uint32_t>(s[0]) << 0) |
               (static_cast<uint32_t>(s[1]) << 8) |
               (static_cast<uint32_t>(s[2]) << 16) |
               (static_cast<uint32_t>(s[3]) << 24);
    }

} // namespace pnkr::core
