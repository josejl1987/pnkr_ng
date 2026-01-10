#pragma once
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace pnkr::core {

constexpr size_t PAGE_SIZE = 2 * 1024 * 1024;

struct MemoryPage {
  std::unique_ptr<uint8_t[]> memory;
  size_t cursor = 0;
  MemoryPage *next = nullptr;

  MemoryPage() { memory = std::make_unique<uint8_t[]>(PAGE_SIZE); }

  void Reset() { cursor = 0; }
};

class TaggedHeapBackend {
public:
  static TaggedHeapBackend &Get() {
    static TaggedHeapBackend instance;
    return instance;
  }

  MemoryPage *AcquirePage(uint64_t frameIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    MemoryPage *page = nullptr;

    if (!m_freePages.empty()) {
      page = m_freePages.back();
      m_freePages.pop_back();
    } else {
      page = new MemoryPage();
      m_allPages.push_back(page);
    }

    page->Reset();
    page->next = m_usedPages[frameIndex];
    m_usedPages[frameIndex] = page;

    return page;
  }

  void FreeFrame(uint64_t frameIndex) {
    std::lock_guard<std::mutex> lock(m_mutex);

    MemoryPage *head = m_usedPages[frameIndex];
    while (head) {
      MemoryPage *next = head->next;
      m_freePages.push_back(head);
      head = next;
    }
    m_usedPages.erase(frameIndex);
  }

private:
  std::mutex m_mutex;
  std::vector<MemoryPage *> m_allPages;
  std::vector<MemoryPage *> m_freePages;
  std::unordered_map<uint64_t, MemoryPage *> m_usedPages;
};

class LinearAllocator {
public:
  void Init(uint64_t frameIndex) {
    m_frameIndex = frameIndex;
    m_currentPage = nullptr;
  }

  void *Alloc(size_t size, size_t align) {
    if (!m_currentPage) {
      m_currentPage = TaggedHeapBackend::Get().AcquirePage(m_frameIndex);
    }

    uintptr_t currentPtr =
        (uintptr_t)m_currentPage->memory.get() + m_currentPage->cursor;
    uintptr_t alignedPtr = (currentPtr + (align - 1)) & ~(align - 1);
    size_t newCursor =
        alignedPtr - (uintptr_t)m_currentPage->memory.get() + size;

    if (newCursor > PAGE_SIZE) {
      if (size > PAGE_SIZE)
        return nullptr;

      MemoryPage *newPage = TaggedHeapBackend::Get().AcquirePage(m_frameIndex);
      m_currentPage = newPage;

      currentPtr = (uintptr_t)m_currentPage->memory.get();
      alignedPtr = (currentPtr + (align - 1)) & ~(align - 1);
      newCursor = alignedPtr - (uintptr_t)m_currentPage->memory.get() + size;
    }

    m_currentPage->cursor = newCursor;
    return (void *)alignedPtr;
  }

  template <typename T, typename... Args> T *New(Args &&...args) {
    void *ptr = Alloc(sizeof(T), alignof(T));
    return new (ptr) T(std::forward<Args>(args)...);
  }

private:
  uint64_t m_frameIndex = 0;
  MemoryPage *m_currentPage = nullptr;
};

} // namespace pnkr::core
