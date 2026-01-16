#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace pnkr::assets {

struct BC7EncoderConfig {
  bool perceptual = true;
  int qualityLevel = 0;
  bool useSRGB = false;
};

class BC7Encoder {
public:
  static bool compress(const uint8_t *srcRGBA, uint32_t width, uint32_t height,
                       BC7EncoderConfig config,
                       std::vector<uint8_t> &outCompressedData);
};

} // namespace pnkr::assets
