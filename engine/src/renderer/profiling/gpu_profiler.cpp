#include "pnkr/renderer/profiling/gpu_profiler.hpp"

namespace pnkr::renderer {

GpuTimeQueryTree::GpuTimeQueryTree(GpuTimeQueryTree&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.m_mutex);
    mTimeQueries = std::move(other.mTimeQueries);
    mCurrentTimeQuery = other.mCurrentTimeQuery;
    mAllocatedTimeQuery = other.mAllocatedTimeQuery;
    mCompletedTimeQuery = other.mCompletedTimeQuery;
}

GpuTimeQueryTree& GpuTimeQueryTree::operator=(GpuTimeQueryTree&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(m_mutex, other.m_mutex);
        mTimeQueries = std::move(other.mTimeQueries);
        mCurrentTimeQuery = other.mCurrentTimeQuery;
        mAllocatedTimeQuery = other.mAllocatedTimeQuery;
        mCompletedTimeQuery = other.mCompletedTimeQuery;
    }
    return *this;
}

void GpuTimeQueryTree::init(uint32_t maxQueries) {
    mTimeQueries.resize(maxQueries);
    reset();
}

void GpuTimeQueryTree::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);
    mCurrentTimeQuery = 0;
    mAllocatedTimeQuery = 0;
    mCompletedTimeQuery = 0;
}

GPUTimeQuery* GpuTimeQueryTree::push(const char* name, uint16_t parentIndex, uint16_t depth) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (mAllocatedTimeQuery >= mTimeQueries.size()) {
        return nullptr;
    }

    const uint16_t index = mAllocatedTimeQuery++;
    GPUTimeQuery& query = mTimeQueries[index];

    query.name = (name != nullptr) ? name : "";
    query.startMs = 0.0;
    query.elapsedMs = 0.0;

    query.startQueryIndex = static_cast<uint16_t>(index * 2);
    query.endQueryIndex = static_cast<uint16_t>((index * 2) + 1);

    query.depth = depth;
    query.parentIndex = parentIndex;

    mCompletedTimeQuery = std::max<uint16_t>(mCompletedTimeQuery, index + 1);

    return &query;
}

GPUTimeQuery* GpuTimeQueryTree::getQuery(uint16_t index) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (index >= mAllocatedTimeQuery) {
        return nullptr;
    }
    return &mTimeQueries[index];
}

}
