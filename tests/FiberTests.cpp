#include <doctest/doctest.h>

#include "pnkr/core/Fiber.hpp"
#include "pnkr/core/TaggedHeap.hpp"

#include <atomic>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace pnkr::core;
y struct LifecycleTracker {
  static int s_constructed;
  static int s_destructed;

  int payload = 0;

  LifecycleTracker(int v) : payload(v) { s_constructed++; }
  ~LifecycleTracker() { s_destructed++; }

  static void Reset() {
    s_constructed = 0;
    s_destructed = 0;
  }
};
int LifecycleTracker::s_constructed = 0;
int LifecycleTracker::s_destructed = 0;

TEST_SUITE("Fiber System") {

  TEST_CASE("Ping-Pong Execution Flow") {
    std::vector<std::string> flow;

    Fiber fiber([&](Fiber *self) {
      flow.push_back("Fiber Start");
      self->Yield();
      flow.push_back("Fiber Resume");
      self->Yield();
      flow.push_back("Fiber End");
    });

    flow.push_back("Main Start");
    fiber.Resume();

    flow.push_back("Main Mid 1");
    fiber.Resume();

    flow.push_back("Main Mid 2");
    fiber.Resume();

    flow.push_back("Main End");

    std::vector<std::string> expected = {
        "Main Start", "Fiber Start", "Main Mid 1", "Fiber Resume",
        "Main Mid 2", "Fiber End",   "Main End"};

    REQUIRE(flow == expected);
  }

  TEST_CASE("Nested Fiber Switching") {
    std::vector<int> trace;

    Fiber *ptrA = nullptr;
    Fiber *ptrB = nullptr;

    Fiber fiberB([&](Fiber *self) {
      trace.push_back(3);
      self->Yield();
    });
    ptrB = &fiberB;

    Fiber fiberA([&](Fiber *self) {
      trace.push_back(2);
      ptrB->Resume();
      trace.push_back(4);
      self->Yield();
    });
    ptrA = &fiberA;

    trace.push_back(1);
    ptrA->Resume();
    trace.push_back(5);

    std::vector<int> expected = {1, 2, 3, 4, 5};
    REQUIRE(trace == expected);
  }

  TEST_CASE("Floating Point State Preservation") {
    double result = 0.0;

    Fiber fiber([&](Fiber *self) {
      double a = 1.23456789;
      double b = 9.87654321;
      double intermediate = a * b;

      self->Yield();

      result = intermediate + (a / b);
    });

    fiber.Resume();

    volatile double clobber = 0.0;
    for (int i = 0; i < 100; ++i) {
      clobber += std::sin((double)i) * std::cos((double)i);
    }
    (void)clobber;

    fiber.Resume();

    double expected = (1.23456789 * 9.87654321) + (1.23456789 / 9.87654321);
    CHECK(result == doctest::Approx(expected).epsilon(0.000001));
  }
}

TEST_SUITE("Tagged Heap Logic") {

  TEST_CASE("Linear Allocator Alignment Constraints") {
    LinearAllocator allocator;
    allocator.Init(1);

    size_t alignments[] = {1, 2, 4, 8, 16, 32, 64, 128, 256};

    for (size_t align : alignments) {
      void *ptr = allocator.Alloc(1, align);
      uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);

      CHECK_MESSAGE((addr & (align - 1)) == 0,
                    "Failed alignment for: " << align);
    }
  }

  TEST_CASE("Large Page Overflow") {
    LinearAllocator allocator;
    allocator.Init(2);

    constexpr size_t PAGE_SIZE = 2 * 1024 * 1024;

    void *chunk1 = allocator.Alloc(PAGE_SIZE - 64, 1);
    REQUIRE(chunk1 != nullptr);

    void *chunk2 = allocator.Alloc(32, 1);
    REQUIRE(chunk2 != nullptr);

    CHECK((uintptr_t)chunk2 == (uintptr_t)chunk1 + (PAGE_SIZE - 64));

    void *chunk3 = allocator.Alloc(1024, 1);
    REQUIRE(chunk3 != nullptr);

    size_t dist = std::abs((int64_t)chunk3 - (int64_t)chunk2);
    CHECK_MESSAGE(dist >= 64, "Pointer should have jumped to new page");
  }

  TEST_CASE("Oversized Allocation Failure") {
    LinearAllocator allocator;
    allocator.Init(3);

    constexpr size_t HUGE_SIZE = 3 * 1024 * 1024;
    void *ptr = allocator.Alloc(HUGE_SIZE, 1);

    CHECK(ptr == nullptr);
  }
}

TEST_SUITE("Tagged Heap Lifecycle") {

  TEST_CASE("Placement New Construction") {
    LinearAllocator allocator;
    allocator.Init(4);

    LifecycleTracker::Reset();

    // Allocate and Construct
    LifecycleTracker *obj = allocator.New<LifecycleTracker>(123);

    CHECK(obj->payload == 123);
    CHECK(LifecycleTracker::s_constructed == 1);
    CHECK(LifecycleTracker::s_destructed == 0);

    obj->~LifecycleTracker();
    CHECK(LifecycleTracker::s_destructed == 1);
  }

  TEST_CASE("Frame Reset and Memory Reuse") {
    uint64_t frameID = 100;
    void *ptrA = nullptr;
    void *ptrB = nullptr;

    {
      LinearAllocator allocA;
      allocA.Init(frameID);
      ptrA = allocA.Alloc(1024, 1);
      *(int *)ptrA = 0xCAFEBABE;
    }

    TaggedHeapBackend::Get().FreeFrame(frameID);

    {
      LinearAllocator allocB;
      allocB.Init(frameID + 1);
      ptrB = allocB.Alloc(1024, 1);

      *(int *)ptrB = 0xDEADBEEF;
      CHECK(*(int *)ptrB == 0xDEADBEEF);
    }
  }
}

TEST_SUITE("Concurrency") {

  TEST_CASE("Thread Safety of Global Backend") {
    constexpr int NUM_THREADS = 8;
    constexpr int ALLOCS_PER_THREAD = 100;

    std::vector<std::thread> threads;
    std::atomic<bool> startFlag{false};

    for (int i = 0; i < NUM_THREADS; ++i) {
      threads.emplace_back([&, i]() {
        while (!startFlag.load())
          ;

        LinearAllocator localAlloc;
        localAlloc.Init(200 + i);

        for (int j = 0; j < ALLOCS_PER_THREAD; ++j) {
          volatile void *ptr = localAlloc.Alloc(1024 * 512, 16);
          (void)ptr;
          CHECK((void *)ptr != nullptr);
        }
      });
    }

    startFlag.store(true);

    for (auto &t : threads) {
      if (t.joinable())
        t.join();
    }

    for (int i = 0; i < NUM_THREADS; ++i) {
      TaggedHeapBackend::Get().FreeFrame(200 + i);
    }
  }
}
