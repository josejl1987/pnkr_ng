#pragma once

#include "pnkr/core/Handle.h"
#include "pnkr/renderer/scene/RenderBatcher.hpp" // For SortingType

namespace pnkr::renderer::scene
{
    struct MaterialPipelineMap
    {
        PipelineHandle pipelineSolid = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineSolidDoubleSided = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransmission = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransmissionDoubleSided = INVALID_PIPELINE_HANDLE;
        PipelineHandle pipelineTransparent = INVALID_PIPELINE_HANDLE;

        PipelineHandle getPipeline(SortingType type) const {
            switch(type) {
                case SortingType::Opaque: return pipelineSolid;
                case SortingType::OpaqueDoubleSided: return pipelineSolidDoubleSided;
                case SortingType::Transmission: return pipelineTransmission;
                case SortingType::TransmissionDoubleSided: return pipelineTransmissionDoubleSided;
                case SortingType::Transparent: return pipelineTransparent;
                default: return INVALID_PIPELINE_HANDLE;
            }
        }
    };
}
