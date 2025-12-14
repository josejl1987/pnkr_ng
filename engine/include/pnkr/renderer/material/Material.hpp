#pragma once
#include "pnkr/core/Handle.h"

namespace pnkr::renderer::scene
{
    struct Material {
        PipelineHandle pipeline;
        // Future: textures, uniforms, etc.
    };
    using MaterialHandle = Handle;
};