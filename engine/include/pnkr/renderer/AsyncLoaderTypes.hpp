#pragma once

#include "pnkr/assets/ImportedData.hpp"
#include "pnkr/core/Handle.h"
#include "pnkr/renderer/ktx_utils.hpp"
#include "pnkr/renderer/RHIResourceManager.hpp"
#include "pnkr/renderer/ResourceStateMachine.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include <string>
#include <vector>

namespace pnkr::renderer {

using LoadPriority = assets::LoadPriority;

enum class UploadDirection { HighToLowRes, LowToHighRes };

struct StreamRequestState {
  uint32_t baseMip = 0;
  UploadDirection direction = UploadDirection::LowToHighRes;
  int32_t currentLevel = -1;
  uint32_t currentLayer = 0;
  uint32_t currentFace = 0;
  uint32_t currentRow = 0;
};

struct LoadRequest {
  std::string path;
  TextureHandle targetHandle;
  bool srgb = true;
  LoadPriority priority = LoadPriority::Medium;
  uint32_t baseMip = 0;
  double timestampStart = 0.0;
};

struct UploadRequest {
  LoadRequest req;
  KTXTextureData textureData;
  bool isRawImage = false;
  uint64_t totalSize = 0;
  uint32_t targetMipLevels = 0;

  StreamRequestState state;

  bool layoutInitialized = false;
  bool layoutFinalized = false;
  bool needsMipmapGeneration = false;

  core::ScopeSnapshot scopeSnapshot;
  ResourceStateMachine stateMachine;
  rhi::TextureDescriptor intermediateDesc;
  bool isHighPriority = false;

  TexturePtr intermediateTexture;
  std::vector<std::pair<uint64_t, uint64_t>> stagingReferences;

  UploadRequest() = default;
  UploadRequest(UploadRequest &&) noexcept = default;
  UploadRequest &operator=(UploadRequest &&) noexcept = default;

  UploadRequest(const UploadRequest &) = delete;
  UploadRequest &operator=(const UploadRequest &) = delete;
};

// Ensure UploadRequest is noexcept move constructible
static_assert(std::is_nothrow_move_constructible_v<UploadRequest>);

} // namespace pnkr::renderer
