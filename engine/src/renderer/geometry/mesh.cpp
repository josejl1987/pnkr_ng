//
// Created by Jose on 12/13/2025.
//

#include "pnkr/renderer/geometry/mesh.h"

#include "pnkr/renderer/geometry/Vertex.h"
#include "pnkr/renderer/vulkan/vulkan_buffer.hpp"

namespace pnkr::renderer {
class VulkanDevice;
}

pnkr::renderer::Mesh::Mesh(VulkanDevice &device,
                           const std::vector<Vertex> &vertices,
                           const std::vector<std::uint32_t> &indices)
    : m_vertexBuffer(VulkanBuffer::CreateDeviceLocalAndUpload(
          device, vertices.data(), vertices.size() * sizeof(Vertex),
          vk::BufferUsageFlagBits::eVertexBuffer)),
      m_indexBuffer(VulkanBuffer::CreateDeviceLocalAndUpload(
          device, indices.data(), indices.size() * sizeof(std::uint32_t),
          vk::BufferUsageFlagBits::eIndexBuffer)),
      m_indexCount(static_cast<std::uint32_t>(indices.size())) {}

void pnkr::renderer::Mesh::bind(vk::CommandBuffer cmd) const {
  vk::DeviceSize offsets[] = {0};
  cmd.bindVertexBuffers(0, 1, &m_vertexBuffer.buffer(), offsets);
  cmd.bindIndexBuffer(m_indexBuffer.buffer(), 0, vk::IndexType::eUint32);
}

void pnkr::renderer::Mesh::draw(vk::CommandBuffer cmd) const {
  cmd.drawIndexed(m_indexCount, 1, 0, 0, 0);
}