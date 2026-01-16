#pragma once

#include <cstdint>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace pnkr::core {

class MemoryMappedFile {
public:
  MemoryMappedFile(const std::filesystem::path &path);
  ~MemoryMappedFile();

  [[nodiscard]] const uint8_t *data() const { return m_mData; }
  [[nodiscard]] size_t size() const { return m_mSize; }
  [[nodiscard]] bool isValid() const { return m_mData != nullptr; }

private:
  const uint8_t *m_mData = nullptr;
  size_t m_mSize = 0;
#ifdef _WIN32
  HANDLE m_fileHandle = (HANDLE)(void *)-1; // INVALID_HANDLE_VALUE
  HANDLE m_mMapHandle = nullptr;
#endif
};

} // namespace pnkr::core
