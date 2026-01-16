#include "pnkr/assets/BC7Encoder.hpp"
#include "pnkr/core/logger.hpp"

#include "cmp_core.h"
#include <algorithm>
#include <cstring>

namespace pnkr::assets {

bool BC7Encoder::compress(const uint8_t *srcRGBA, uint32_t width,
                          uint32_t height, BC7EncoderConfig config,
                          std::vector<uint8_t> &outCompressedData) {
  if (!srcRGBA || width == 0 || height == 0) {
    return false;
  }

  // BC7 works on 4x4 blocks
  uint32_t blocksX = (width + 3) / 4;
  uint32_t blocksY = (height + 3) / 4;

  outCompressedData.resize(blocksX * blocksY * 16);

  void *options = nullptr;
  CreateOptionsBC7(&options);
  if (options) {
    float quality = std::clamp((float)config.qualityLevel / 10.0f, 0.0f, 1.0f);
    if (quality == 0.0f)
      quality = 0.05f;
    SetQualityBC7(options, quality);
  }

  // Compressonator Core BC7 encoder works block by block
  for (uint32_t by = 0; by < blocksY; ++by) {
    for (uint32_t bx = 0; bx < blocksX; ++bx) {
      uint8_t blockRGBA[64]; // 4x4x4

      // Fill 4x4 block, handle padding if image size is not multiple of 4
      for (uint32_t y = 0; y < 4; ++y) {
        uint32_t py = std::min(by * 4 + y, height - 1);
        for (uint32_t x = 0; x < 4; ++x) {
          uint32_t px = std::min(bx * 4 + x, width - 1);
          const uint8_t *pixel = srcRGBA + (py * width + px) * 4;
          memcpy(blockRGBA + (y * 4 + x) * 4, pixel, 4);
        }
      }

      uint8_t *dstBlock = outCompressedData.data() + (by * blocksX + bx) * 16;

      // CompressBlockBC7 expects 4x4 block in RGBA8888
      CompressBlockBC7(blockRGBA, 16, dstBlock, options);
    }
  }

  if (options) {
    DestroyOptionsBC7(options);
  }

  return true;
}

} // namespace pnkr::assets
