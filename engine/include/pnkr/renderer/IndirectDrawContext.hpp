#pragma once

#include "pnkr/renderer/FrameManager.hpp"
#include "pnkr/renderer/UploadSlice.hpp"
#include "pnkr/renderer/scene/GLTFUnifiedDOD.hpp"

namespace pnkr::renderer {

struct IndirectDrawContext {
    UploadSlice cameraDataSlice;
    UploadSlice sceneDataSlice;
    UploadSlice transformSlice;
    UploadSlice lightSlice;
    UploadSlice environmentSlice;
    UploadSlice instanceXformSlice;

    uint64_t cameraDataAddr = 0;
    uint64_t sceneDataAddr = 0;
    uint64_t transformAddr = 0;
    uint64_t lightAddr = 0;
    uint64_t environmentAddr = 0;
    uint64_t shadowDataAddr = 0;
    uint64_t instanceXformAddr = 0;
    uint64_t skinningMeshXformAddr = 0;

    uint32_t lightCount = 0;
    uint32_t visibleMeshCount = 0;
    uint64_t materialBufferAddr = 0;

    TransientAllocation indirectOpaqueAlloc;
    TransientAllocation indirectTransmissionAlloc;
    TransientAllocation indirectTransparentAlloc;

    scene::GLTFUnifiedDODContext dodContext;
};

}
