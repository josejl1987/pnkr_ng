#include "pnkr/platform/FileDialog.hpp"
#include <nfd.h>

namespace pnkr::platform::FileDialog
{
    std::optional<std::filesystem::path> OpenGLTFDialog()
    {
      if (NFD_Init() != NFD_OKAY) {
        return std::nullopt;
      }

        nfdu8char_t* outPath = nullptr;

        nfdu8filteritem_t filters[] = {
            { "glTF files", "gltf,glb" }
        };

        nfdopendialogu8args_t args{};
        args.filterList  = filters;
        args.filterCount = 1;

        const nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

        std::optional<std::filesystem::path> chosen;
        if (result == NFD_OKAY && (outPath != nullptr)) {
          chosen =
              std::filesystem::path(reinterpret_cast<const char *>(outPath));
          NFD_FreePathU8(outPath);
        }

        NFD_Quit();
        return chosen;
    }

    std::optional<std::filesystem::path> OpenImageDialog()
    {
      if (NFD_Init() != NFD_OKAY) {
        return std::nullopt;
      }

        nfdu8char_t* outPath = nullptr;

        nfdu8filteritem_t filters[] = {
            { "Environment Maps", "ktx,ktx2,hdr,exr,png,jpg" }
        };

        nfdopendialogu8args_t args{};
        args.filterList  = filters;
        args.filterCount = 1;

        const nfdresult_t result = NFD_OpenDialogU8_With(&outPath, &args);

        std::optional<std::filesystem::path> chosen;
        if (result == NFD_OKAY && (outPath != nullptr)) {
          chosen =
              std::filesystem::path(reinterpret_cast<const char *>(outPath));
          NFD_FreePathU8(outPath);
        }

        NFD_Quit();
        return chosen;
    }
}
