#pragma once
#include <atomic>
#include <string>
#include <mutex>

namespace pnkr::assets {

enum class LoadStage {
    ReadingFile,
    ParsingGLTF,
    LoadingTextures,
    ProcessingMeshes,
    UploadingToGPU,
    Complete
};

struct LoadProgress {
    std::atomic<LoadStage> currentStage{LoadStage::ReadingFile};
    std::atomic<uint32_t> texturesTotal{0};
    std::atomic<uint32_t> texturesLoaded{0};
    std::atomic<uint32_t> meshesTotal{0};
    std::atomic<uint32_t> meshesProcessed{0};
    std::atomic<uint64_t> bytesRead{0};
    std::atomic<uint64_t> bytesTotal{0};

    void reset() {
        currentStage.store(LoadStage::ReadingFile, std::memory_order_relaxed);
        texturesTotal.store(0, std::memory_order_relaxed);
        texturesLoaded.store(0, std::memory_order_relaxed);
        meshesTotal.store(0, std::memory_order_relaxed);
        meshesProcessed.store(0, std::memory_order_relaxed);
        bytesRead.store(0, std::memory_order_relaxed);
        bytesTotal.store(0, std::memory_order_relaxed);

        {
            std::lock_guard<std::mutex> lock(m_messageMutex);
            m_statusMessage.clear();
        }
    }

    float getProgress() const {
        switch (currentStage.load(std::memory_order_relaxed)) {
            case LoadStage::ReadingFile:
            {
                uint64_t total = bytesTotal.load(std::memory_order_relaxed);
                return total > 0 ? (float)bytesRead.load(std::memory_order_relaxed) / total * 0.1f : 0.0f;
            }
            case LoadStage::ParsingGLTF:
                return 0.1f;
            case LoadStage::LoadingTextures:
            {
                uint32_t total = texturesTotal.load(std::memory_order_relaxed);
                return 0.1f + (total > 0 ?
                    (float)texturesLoaded.load(std::memory_order_relaxed) / total * 0.6f : 0.0f);
            }
            case LoadStage::ProcessingMeshes:
            {
                uint32_t total = meshesTotal.load(std::memory_order_relaxed);
                return 0.7f + (total > 0 ?
                    (float)meshesProcessed.load(std::memory_order_relaxed) / total * 0.2f : 0.0f);
            }
            case LoadStage::UploadingToGPU:
                return 0.9f;
            case LoadStage::Complete:
                return 1.0f;
        }
        return 0.0f;
    }

    std::string getCurrentStageString() const {
        switch (currentStage.load(std::memory_order_relaxed)) {
            case LoadStage::ReadingFile: return "Reading file...";
            case LoadStage::ParsingGLTF: return "Parsing glTF...";
            case LoadStage::LoadingTextures:
            {
                uint32_t loaded = texturesLoaded.load(std::memory_order_relaxed);
                uint32_t total = texturesTotal.load(std::memory_order_relaxed);
                return "Loading textures (" + std::to_string(loaded) +
                       "/" + std::to_string(total) + ")";
            }
            case LoadStage::ProcessingMeshes:
            {
                uint32_t processed = meshesProcessed.load(std::memory_order_relaxed);
                uint32_t total = meshesTotal.load(std::memory_order_relaxed);
                return "Processing meshes (" + std::to_string(processed) +
                       "/" + std::to_string(total) + ")";
            }
            case LoadStage::UploadingToGPU: return "Uploading to GPU...";
            case LoadStage::Complete: return "Complete";
        }
        return "Unknown";
    }

    void setStatusMessage(std::string message) {
        std::lock_guard<std::mutex> lock(m_messageMutex);
        m_statusMessage = std::move(message);
    }

    std::string getStatusMessage() const {
        std::lock_guard<std::mutex> lock(m_messageMutex);
        return m_statusMessage;
    }

private:
    mutable std::mutex m_messageMutex;
    std::string m_statusMessage;
};

}
