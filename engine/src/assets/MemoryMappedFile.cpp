#include "pnkr/assets/MemoryMappedFile.hpp"

namespace pnkr::assets {

    MemoryMappedFile::MemoryMappedFile(const std::filesystem::path& path)
        : mFileHandle(CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ, nullptr, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, nullptr)) {
#ifdef _WIN32

        if (mFileHandle == INVALID_HANDLE_VALUE) {
            return;
        }

        LARGE_INTEGER fileSize;
        GetFileSizeEx(mFileHandle, &fileSize);
        m_mSize = static_cast<size_t>(fileSize.QuadPart);

        m_mMapHandle = CreateFileMappingW(mFileHandle, nullptr,
            PAGE_READONLY, 0, 0, nullptr);
        if (m_mMapHandle == nullptr) {
            CloseHandle(mFileHandle);
            return;
        }

        m_mData = static_cast<const uint8_t*>(
            MapViewOfFile(m_mMapHandle, FILE_MAP_READ, 0, 0, 0));
#else
        int fd = open(path.c_str(), O_RDONLY);
        if (fd == -1) return;

        struct stat sb;
        if (fstat(fd, &sb) == -1)
        {
            close(fd);
            return;
        }
        mSize = sb.st_size;

        mData = static_cast<const uint8_t*>(
            mmap(nullptr, mSize, PROT_READ, MAP_PRIVATE, fd, 0)
            );
        close(fd);

        if (mData == MAP_FAILED) mData = nullptr;
#endif
    }

    MemoryMappedFile::~MemoryMappedFile()
    {
#ifdef _WIN32
        if (m_mData != nullptr) {
            UnmapViewOfFile(m_mData);
        }
        if (m_mMapHandle != nullptr) {
            CloseHandle(m_mMapHandle);
        }
        if (mFileHandle != INVALID_HANDLE_VALUE) {
            CloseHandle(mFileHandle);
        }
#else
        if (mData && mData != MAP_FAILED)
        {
            munmap(const_cast<uint8_t*>(mData), mSize);
        }
#endif
    }

}
