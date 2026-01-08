#pragma once

#include "pnkr/renderer/RHIResourceManager.hpp"
#include <string>

namespace pnkr::renderer
{
    class RHIRenderer;

    class BRDFLutGenerator
    {
    public:
        static bool generateAndSave(RHIRenderer* renderer, const std::string& outputPath, uint32_t width = 256, uint32_t height = 256, uint32_t numSamples = 1024);
    };
}
