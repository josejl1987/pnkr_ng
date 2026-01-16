#include "pnkr/core/MemoryMappedFile.hpp"

namespace pnkr::core {

MemoryMappedFile::MemoryMappedFile(const std::filesystem::path &path) {
#ifdef _WIN32
  m_fileHandle =
      CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (m_fileHandle == (HANDLE)(void *)-1) {
    return;
  }

  LARGE_INTEGER fileSize;
  if (!GetFileSizeEx(m_fileHandle, &fileSize)) {
    CloseHandle(m_fileHandle);
    m_fileHandle = (HANDLE)(void *)-1;
    return;
  }
  m_mSize = static_cast<size_t>(fileSize.QuadPart);

  if (m_mSize == 0) {
    m_mData = nullptr;
    return;
  }

  m_mMapHandle =
      CreateFileMappingW(m_fileHandle, nullptr, PAGE_READONLY, 0, 0, nullptr);
  if (m_mMapHandle == nullptr) {
    CloseHandle(m_fileHandle);
    m_fileHandle = (HANDLE)(void *)-1;
    return;
  }

  m_mData = static_cast<const uint8_t *>(
      MapViewOfFile(m_mMapHandle, FILE_MAP_READ, 0, 0, 0));
#else
  int fd = open(path.string().c_str(), O_RDONLY);
  if (fd == -1)
    return;

  struct stat sb;
  if (fstat(fd, &sb) == -1) {
    close(fd);
    return;
  }
  m_mSize = static_cast<size_t>(sb.st_size);

  if (m_mSize == 0) {
    m_mData = nullptr;
    close(fd);
    return;
  }

  m_mData = static_cast<const uint8_t *>(
      mmap(nullptr, m_mSize, PROT_READ, MAP_PRIVATE, fd, 0));
  close(fd);

  if (m_mData == MAP_FAILED)
    m_mData = nullptr;
#endif
}

MemoryMappedFile::~MemoryMappedFile() {
#ifdef _WIN32
  if (m_mData != nullptr) {
    UnmapViewOfFile(m_mData);
  }
  if (m_mMapHandle != nullptr) {
    CloseHandle(m_mMapHandle);
  }
  if (m_fileHandle != (HANDLE)(void *)-1) {
    CloseHandle(m_fileHandle);
  }
#else
  if (m_mData && m_mData != MAP_FAILED) {
    munmap(const_cast<uint8_t *>(m_mData), m_mSize);
  }
#endif
}

} // namespace pnkr::core
