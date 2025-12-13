//
// Created by Jose on 12/13/2025.
//

#ifndef PNKR_MESH_H
#define PNKR_MESH_H
#include "Vertex.h"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"
#include "pnkr/renderer/vulkan/vulkan_device.hpp"
namespace pnkr::renderer {
class Mesh {
public:
  Mesh(VulkanDevice &device, const std::vector<Vertex> &vertices,
       const std::vector<std::uint32_t> &indices);
  void bind(vk::CommandBuffer cmd) const;
  void draw(vk::CommandBuffer cmd) const;

private:
  VulkanBuffer m_vertexBuffer;
  VulkanBuffer m_indexBuffer;
  std::uint32_t m_indexCount = 0;
};
} // namespace pnkr::renderer
#endif // PNKR_MESH_H
