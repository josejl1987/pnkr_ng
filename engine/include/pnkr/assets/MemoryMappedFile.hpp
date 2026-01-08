#pragma once

#include <filesystem>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace pnkr::assets {

    class MemoryMappedFile {
    public:
        MemoryMappedFile(const std::filesystem::path& path);
        ~MemoryMappedFile();

        [[nodiscard]] const uint8_t* data() const { return m_mData; }
        [[nodiscard]] size_t size() const { return m_mSize; }
        [[nodiscard]] bool isValid() const { return m_mData != nullptr; }

    private:
        const uint8_t* m_mData = nullptr;
        size_t m_mSize = 0;
#ifdef _WIN32
        HANDLE mFileHandle = INVALID_HANDLE_VALUE;
        HANDLE m_mMapHandle = nullptr;
#endif
    };

}
