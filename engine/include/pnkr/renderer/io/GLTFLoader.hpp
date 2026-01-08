#pragma once

#include <filesystem>
#include <memory>

namespace pnkr::renderer
{
    class RHIRenderer;
}

namespace pnkr::renderer::scene
{
    class ModelDOD;
}

namespace pnkr::renderer::io
{
    class GLTFLoader
    {
    public:
        static std::unique_ptr<scene::ModelDOD> load(RHIRenderer& renderer,
                                                     const std::filesystem::path& path,
                                                     bool vertexPulling = false);
    };
}
