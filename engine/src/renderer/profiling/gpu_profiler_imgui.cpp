#include "pnkr/renderer/profiling/gpu_profiler_imgui.hpp"
#include "pnkr/rhi/BindlessManager.hpp"
#include "pnkr/rhi/rhi_texture.hpp"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <imgui.h>
#include <utility>

namespace pnkr::renderer {

namespace {
const char* formatToString(rhi::Format format) {
    switch (format) {
        case rhi::Format::R8_UNORM: return "R8_UNORM";
        case rhi::Format::R8_SNORM: return "R8_SNORM";
        case rhi::Format::R8G8_UNORM: return "R8G8_UNORM";
        case rhi::Format::R8G8_SNORM: return "R8G8_SNORM";
        case rhi::Format::R8G8B8_UNORM: return "R8G8B8_UNORM";
        case rhi::Format::R8G8B8_SNORM: return "R8G8B8_SNORM";
        case rhi::Format::R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case rhi::Format::R8G8B8A8_SNORM: return "R8G8B8A8_SNORM";
        case rhi::Format::R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
        case rhi::Format::B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case rhi::Format::B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
        case rhi::Format::B10G11R11_UFLOAT_PACK32: return "B10G11R11_UFLOAT";
        case rhi::Format::R16_SFLOAT: return "R16_SFLOAT";
        case rhi::Format::R16G16_SFLOAT: return "R16G16_SFLOAT";
        case rhi::Format::R16G16B16A16_SFLOAT: return "R16G16B16A16_SFLOAT";
        case rhi::Format::R32_UINT: return "R32_UINT";
        case rhi::Format::R32_SFLOAT: return "R32_SFLOAT";
        case rhi::Format::R32G32_SFLOAT: return "R32G32_SFLOAT";
        case rhi::Format::R32G32B32_SFLOAT: return "R32G32B32_SFLOAT";
        case rhi::Format::R32G32B32A32_SFLOAT: return "R32G32B32A32_SFLOAT";
        case rhi::Format::R32G32B32A32_UINT: return "R32G32B32A32_UINT";
        case rhi::Format::D16_UNORM: return "D16_UNORM";
        case rhi::Format::D32_SFLOAT: return "D32_SFLOAT";
        case rhi::Format::D24_UNORM_S8_UINT: return "D24_UNORM_S8";
        case rhi::Format::D32_SFLOAT_S8_UINT: return "D32_SFLOAT_S8";
        case rhi::Format::BC1_RGB_UNORM: return "BC1_RGB_UNORM";
        case rhi::Format::BC1_RGB_SRGB: return "BC1_RGB_SRGB";
        case rhi::Format::BC3_UNORM: return "BC3_UNORM";
        case rhi::Format::BC3_SRGB: return "BC3_SRGB";
        case rhi::Format::BC7_UNORM: return "BC7_UNORM";
        case rhi::Format::BC7_SRGB: return "BC7_SRGB";
        default: return "Unknown";
    }
}

void drawMetricCard(const char *label, const char *value,
                    const ImVec4 &color = ImVec4(1, 1, 1, 1)) {
  ImGui::BeginGroup();
  ImGui::TextColored(ImVec4(0.7F, 0.7F, 0.7F, 1.0F), "%s", label);
  ImGui::SetWindowFontScale(1.5F);
  ImGui::TextColored(color, "%s", value);
  ImGui::SetWindowFontScale(1.0F);
  ImGui::EndGroup();
}
}

GPUProfilerImGui::GPUProfilerImGui() = default;

void GPUProfilerImGui::draw(const char* windowName, GPUTimeQueriesManager* profiler,
                             uint32_t currentFrame, uint32_t framebufferWidth, uint32_t framebufferHeight) {
  if (profiler == nullptr) {
    return;
  }
    (void)currentFrame;

    ImGui::SetNextWindowSize(ImVec2(1000, 600), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(windowName, nullptr, ImGuiWindowFlags_NoScrollbar)) {
        ImGui::End();
        return;
    }

    if (!mPaused && profiler->hasResolvedFrame()) {
        const auto& frameData = profiler->getLastResolvedFrameData();
        if (frameData.totalFrameTimeMs > 0.001) {
            updateFrameHistory(frameData);
        }
    }

    if (mFrameHistory.empty()) {
        ImGui::Text("Waiting for GPU data...");
        ImGui::End();
        return;
    }

    const auto& frame = mFrameHistory.back();

    {
      if (ImGui::Button(mPaused ? " RESUME " : " PAUSE ")) {
        mPaused = !mPaused;
      }
        ImGui::SameLine();
        if (ImGui::Button(" Reset Max ")) {
          resetStatistics();
        }
        ImGui::SameLine();

        ImGui::Checkbox("Timeline", &mShowTimeline);
        ImGui::SameLine();

        float samples[120];
        int count = std::min((int)mFrameHistory.size(), 120);
        for (int i = 0; i < count; ++i) {
          samples[i] = (float)mFrameHistory[mFrameHistory.size() - 1 - i]
                           .totalFrameTimeMs;
        }

        ImGui::PushItemWidth(200);
        ImGui::PlotLines("##FrameTimePlot", samples, count, 0, nullptr, 0.0F,
                         33.0F, ImVec2(200, 20));
        ImGui::PopItemWidth();
    }

    ImGui::Separator();

    {
        char buf[64];

        snprintf(buf, 64, "%.2f ms", frame.totalFrameTimeMs);
        drawMetricCard("GPU Time", buf,
                       frame.totalFrameTimeMs > 16.0
                           ? ImVec4(1, 0.3F, 0.3F, 1)
                           : ImVec4(0.3F, 1, 0.3F, 1));

        ImGui::SameLine(0, 40);

        snprintf(buf, 64, "%u", frame.drawCallStats.drawCalls);
        drawMetricCard("Draw Calls", buf);

        ImGui::SameLine(0, 40);

        snprintf(buf, 64, "%u", frame.drawCallStats.drawIndirectCalls);
        drawMetricCard("Indirect Calls", buf);

        ImGui::SameLine(0, 40);

        float vramMb = frame.memoryStats.usedBytes / (1024.0F * 1024.0F);
        snprintf(buf, 64, "%.0f MB", vramMb);
        drawMetricCard("VRAM", buf,
                       frame.memoryStats.getUsagePercent() > 90.0F
                           ? ImVec4(1, 0.5F, 0, 1)
                           : ImVec4(1, 1, 1, 1));

        ImGui::SameLine(0, 40);

        if (frame.drawCallStats.trianglesDrawn > 1000000) {
          snprintf(buf, 64, "%.1f M",
                   frame.drawCallStats.trianglesDrawn / 1000000.0F);
        } else {
          snprintf(buf, 64, "%.1f K",
                   frame.drawCallStats.trianglesDrawn / 1000.0F);
        }
        drawMetricCard("Triangles", buf);

        ImGui::SameLine(0, 40);

        const char* bn = bottleneckToString(frame.primaryBottleneck);
        ImVec4 bnColor = frame.primaryBottleneck == GPUBottleneck::None
                             ? ImVec4(0.5F, 1, 0.5F, 1)
                             : ImVec4(1, 0.5F, 0.5F, 1);
        drawMetricCard("Bottleneck", bn, bnColor);
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (mShowTimeline) {
        ImGui::TextDisabled("Frame Timeline");
        drawStackedTimeline();
        drawLegend();
        ImGui::Separator();
    }

    if (ImGui::BeginTabBar("ProfilerDetails")) {

        if (ImGui::BeginTabItem("Tree View")) {
            drawTimingTree(frame.queries);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Stats")) {
            drawStatisticsTable();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Pipeline")) {
            drawPipelineStatistics(frame.pipelineStats, profiler->pipelineStatisticsSupported(), framebufferWidth, framebufferHeight);
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Memory & Streaming")) {
            if (ImGui::BeginTable("LayoutTable", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV)) {
              ImGui::TableSetupColumn(
                  "MemoryStats", ImGuiTableColumnFlags_WidthStretch, 0.65F);
              ImGui::TableSetupColumn(
                  "StreamingStats", ImGuiTableColumnFlags_WidthStretch, 0.35F);
              ImGui::TableNextRow();

              ImGui::TableSetColumnIndex(0);
              drawMemoryStatistics(frame.memoryStats);

              ImGui::TableSetColumnIndex(1);
              const auto &s = frame.streamingStats;

              ImGui::Text("Asset Streaming");
              ImGui::Separator();
              ImGui::Text("Queued:    %u", s.queuedAssets);
              ImGui::Text("In-Flight: %u", s.inFlightAssets);

              float mbUsed = s.stagingUsedBytes / (1024.F * 1024.F);
              float mbTotal = s.stagingTotalBytes / (1024.F * 1024.F);
              ImGui::Text("Staging Buffer:");
              ImGui::ProgressBar(s.stagingTotalBytes > 0
                                     ? (float)s.stagingUsedBytes /
                                           s.stagingTotalBytes
                                     : 0.0F,
                                 ImVec2(-1, 0), "##StagingBuffer");
              ImGui::Text("%.1f / %.1f MB", mbUsed, mbTotal);

              ImGui::Spacing();
              ImGui::Text("Throughput");
              ImGui::Separator();
              ImGui::Text("Bandwidth: %.1f MB/s", s.uploadBandwidthMBps);
              ImGui::Text("This Frame: %u tex, %.2f MB",
                          s.texturesCompletedThisFrame,
                          s.bytesUploadedThisFrame / (1024.0 * 1024.0));
              ImGui::Text("Total: %u tex, %.2f GB", s.texturesCompletedTotal,
                          s.bytesUploadedTotal / (1024.0 * 1024.0 * 1024.0));

              ImGui::Spacing();
              ImGui::Text("Latency (Req to GPU)");
              ImGui::Separator();
              ImGui::Text("Avg: %.1f ms | P95: %.1f ms", s.avgLatencyMs,
                          s.p95LatencyMs);
              ImGui::Text("Min: %.1f ms | Max: %.1f ms", s.minLatencyMs,
                          s.maxLatencyMs);
              ImGui::Text("Samples: %u", s.latencySampleCount);

              ImGui::Spacing();
              ImGui::Text("Streaming Pool");
              ImGui::Separator();
              float poolUtil = (float)s.poolUtilizationPercent / 100.0F;
              ImVec4 poolColor = s.poolOverBudget ? ImVec4(1, 0.3F, 0.3F, 1)
                                                  : ImVec4(0.3F, 1, 0.3F, 1);
              ImGui::ProgressBar(poolUtil, ImVec2(-1, 0), "##StreamingPool");
              ImGui::TextColored(poolColor, "%.1f MB / %.1f MB (Budget)",
                                 s.streamingPoolUsed / (1024.0 * 1024.0),
                                 s.streamingPoolBudget / (1024.0 * 1024.0));

              ImGui::Spacing();
              ImGui::Text("Transfer Thread");
              ImGui::Separator();
              ImGui::Text("Utilization: %.1f%%", s.transferThreadUtilization);
              ImGui::Text("Batches: %u submitted", s.batchesSubmittedTotal);
              ImGui::Text("Avg Batch: %.2f MB", s.avgBatchSizeMB);
              ImGui::Text("Failed: %u", s.failedLoads);

              if (s.activeTempBuffers > 0) {
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "Temp Buffers: %u",
                                   s.activeTempBuffers);
              }

                ImGui::EndTable();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Bindless")) {
            drawBindlessStatistics();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void GPUProfilerImGui::drawPipelineStatistics(const GPUPipelineStatistics& stats, bool supported, uint32_t framebufferWidth, uint32_t framebufferHeight) {
    if (!supported) {
        ImGui::TextDisabled("Pipeline statistics are not supported on this device.");
        return;
    }

    ImGui::Text("Vertex Processing");
    ImGui::Separator();
    ImGui::Text("IA Vertices: %llu", stats.inputAssemblyVertices);
    ImGui::Text("IA Primitives: %llu", stats.inputAssemblyPrimitives);
    ImGui::Text("VS Invocations: %llu", stats.vertexShaderInvocations);

    ImGui::Spacing();
    ImGui::Text("Geometry Processing");
    ImGui::Separator();
    ImGui::Text("GS Invocations: %llu", stats.geometryShaderInvocations);
    ImGui::Text("GS Primitives: %llu", stats.geometryShaderPrimitives);
    ImGui::Text("Clipping Invocations: %llu", stats.clippingInvocations);
    ImGui::Text("Clipping Primitives: %llu", stats.clippingPrimitives);

    ImGui::Spacing();
    ImGui::Text("Fragment Processing");
    ImGui::Separator();
    ImGui::Text("FS Invocations: %llu", stats.fragmentShaderInvocations);

    if (stats.inputAssemblyVertices > 0) {
        double vertexReuse = 100.0 * (1.0 - (double)stats.vertexShaderInvocations / stats.inputAssemblyVertices);
        ImGui::Text("Vertex Reuse: %.1f%%", vertexReuse);
    }

    if (stats.inputAssemblyPrimitives > 0 && stats.clippingPrimitives > 0) {
        double cullRate = 100.0 * (1.0 - (double)stats.clippingPrimitives / stats.inputAssemblyPrimitives);
        ImGui::Text("Cull Rate: %.1f%%", cullRate);
    }

    if (framebufferWidth > 0 && framebufferHeight > 0) {
        uint64_t totalPixels = static_cast<uint64_t>(framebufferWidth) * framebufferHeight;
        double averageOverdraw = static_cast<double>(stats.fragmentShaderInvocations) / totalPixels;
        double overdrawPercent = (averageOverdraw - 1.0) * 100.0;

        ImGui::Text("Overdraw: %.2fx (%.1f%%)", averageOverdraw, overdrawPercent);

        if (overdrawPercent > 100.0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0F, 0.0F, 0.0F, 1.0F), "[HIGH]");
        } else if (overdrawPercent > 50.0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0F, 1.0F, 0.0F, 1.0F), "[MEDIUM]");
        }
    }

    if (stats.clippingPrimitives > 0 && framebufferWidth > 0 && framebufferHeight > 0) {
        uint64_t totalPixels = static_cast<uint64_t>(framebufferWidth) * framebufferHeight;
        double avgPixelsPerTriangle = static_cast<double>(totalPixels) / stats.clippingPrimitives;
        ImGui::Text("Avg Triangle Coverage: %.1f pixels", avgPixelsPerTriangle);
    }

    ImGui::Spacing();
    ImGui::Text("Compute");
    ImGui::Separator();
    ImGui::Text("CS Invocations: %llu", stats.computeShaderInvocations);

    ImGui::Spacing();
    ImGui::Text("Tessellation");
    ImGui::Separator();
    ImGui::Text("Tess Control Patches: %llu", stats.tessControlPatches);
    ImGui::Text("Tess Eval Invocations: %llu", stats.tessEvalInvocations);
}

void GPUProfilerImGui::drawMemoryStatistics(const GPUMemoryStatistics& stats) {
    ImGui::Text("VRAM Usage");
    ImGui::Separator();

    float usagePercent = static_cast<float>(stats.getUsagePercent()) / 100.0F;
    ImGui::Text("Used: %.2f MB / %.2f MB",
                stats.usedBytes / (1024.0 * 1024.0),
                stats.budgetBytes / (1024.0 * 1024.0));

    ImGui::ProgressBar(usagePercent, ImVec2(-1, 0), "##VRAMUsage");
    ImGui::Text("Usage: %.1f%%", stats.getUsagePercent());

    ImGui::Spacing();
    ImGui::Text("Allocations");
    ImGui::Separator();
    ImGui::Text("Total Allocations: %u", stats.allocationCount);
    ImGui::Text("Buffers: %u", stats.bufferCount);
    ImGui::Text("Textures: %u", stats.textureCount);

    ImGui::Spacing();
    ImGui::Text("Allocated: %.2f MB", stats.allocatedBytes / (1024.0 * 1024.0));
    ImGui::Text("Average Allocation: %.2f KB",
                stats.allocationCount > 0 ? (double(stats.allocatedBytes) / stats.allocationCount) / 1024.0 : 0.0);

    ImGui::Spacing();
    ImGui::Text("Texture Memory");
    ImGui::Separator();
    ImGui::Text("Textures: %u", stats.textureCount);
    ImGui::Text("Texture VRAM: %.2f MB (%.1f%% of used)",
                stats.textureBytes / (1024.0 * 1024.0),
                stats.getTexturePercent());
    ImGui::Text("Buffer VRAM (approx): %.2f MB", stats.bufferBytes / (1024.0 * 1024.0));

    ImGui::Spacing();
    if (ImGui::BeginTable("TextureMemory", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_ScrollY,
                          ImVec2(0, 300))) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Size (MB)", ImGuiTableColumnFlags_WidthFixed,
                                90.0F);
        ImGui::TableSetupColumn("Resolution", ImGuiTableColumnFlags_WidthFixed,
                                110.0F);
        ImGui::TableSetupColumn("Mips", ImGuiTableColumnFlags_WidthFixed,
                                50.0F);
        ImGui::TableSetupColumn("Format", ImGuiTableColumnFlags_WidthFixed,
                                120.0F);
        ImGui::TableHeadersRow();

        std::vector<TextureMemoryInfo> sorted = stats.textureList;
        std::ranges::sort(
            sorted, [](const TextureMemoryInfo &a, const TextureMemoryInfo &b) {
              return a.sizeBytes > b.sizeBytes;
            });

        for (const auto& tex : sorted) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", tex.name.c_str());
            if (m_textureResolver && tex.handle != INVALID_TEXTURE_HANDLE && ImGui::IsItemHovered()) {
                ImTextureID previewId = m_textureResolver(tex.handle);
                if (previewId != ~0U) {
                  const float maxSize = 128.0F;
                  const float w =
                      tex.width > 0 ? static_cast<float>(tex.width) : 1.0F;
                  const float h =
                      tex.height > 0 ? static_cast<float>(tex.height) : 1.0F;
                  const float scale = maxSize / std::max(w, h);
                  ImVec2 size(w * scale, h * scale);

                  ImGui::BeginTooltip();
                  ImGui::Text("%s", tex.name.c_str());
                  ImGui::Image(previewId, size);
                  ImGui::EndTooltip();
                }
            }

            ImGui::TableSetColumnIndex(1);
            const auto sizeMB = float(tex.sizeBytes / (1024.0 * 1024.0));
            if (sizeMB > 10.0F) {
              ImGui::TextColored(ImVec4(1.0F, 0.5F, 0.0F, 1.0F), "%.2f",
                                 sizeMB);
            } else {
              ImGui::Text("%.2f", sizeMB);
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u x %u", tex.width, tex.height);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", tex.mipLevels);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%s", formatToString(tex.format));
        }

        ImGui::EndTable();
    }
}

void GPUProfilerImGui::drawStackedTimeline() {
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    canvasSize.x -= mLegendWidth;
    canvasSize.y = mGraphHeight;

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(
        canvasPos,
        ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y),
        IM_COL32(20, 20, 20, 255)
    );

    const int numGridLines = 5;
    for (int i = 0; i <= numGridLines; i++) {
      float y = canvasPos.y + ((canvasSize.y / numGridLines) * i);
      drawList->AddLine(ImVec2(canvasPos.x, y),
                        ImVec2(canvasPos.x + canvasSize.x, y),
                        IM_COL32(50, 50, 50, 255));
    }

    double maxFrameTime = 16.67;
    for (const auto& frame : mFrameHistory) {
        maxFrameTime = std::max(maxFrameTime, frame.totalFrameTimeMs);
    }
    maxFrameTime *= 1.1;

    const float frameWidth = canvasSize.x / static_cast<float>(kMaxHistoryFrames);

    for (size_t frameIdx = 0; frameIdx < mFrameHistory.size(); frameIdx++) {
        const auto& frame = mFrameHistory[frameIdx];
        float x = canvasPos.x + (frameIdx * frameWidth);

        float currentY = canvasPos.y + canvasSize.y;

        for (const auto& query : frame.queries) {
          if (query.name.empty() || query.elapsedMs <= 0.0) {
            continue;
          }

          auto barHeight =
              float((query.elapsedMs / maxFrameTime) * canvasSize.y);
          float y1 = currentY - barHeight;
          float y2 = currentY;

          uint32_t color = hashStringToColor(query.name);

          drawList->AddRectFilled(ImVec2(x, y1), ImVec2(x + frameWidth - 1, y2),
                                  color);

          currentY = y1;
        }
    }

    for (int i = 0; i <= numGridLines; i++) {
      float y = canvasPos.y + ((canvasSize.y / numGridLines) * i);
      auto timeMs = float(maxFrameTime * (1.0F - (float)i / numGridLines));
      char label[32];
      std::snprintf(label, sizeof(label), "%.1fms", timeMs);
      drawList->AddText(ImVec2(canvasPos.x - 45, y - 7),
                        IM_COL32(200, 200, 200, 255), label);
    }

    auto targetY = float(canvasPos.y + canvasSize.y -
                         ((16.67 / maxFrameTime) * canvasSize.y));
    drawList->AddLine(ImVec2(canvasPos.x, targetY),
                      ImVec2(canvasPos.x + canvasSize.x, targetY),
                      IM_COL32(255, 255, 0, 150), 2.0F);

    ImVec2 mousePos = ImGui::GetMousePos();
    if (ImGui::IsMouseHoveringRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y))) {
        int hoveredFrame = static_cast<int>((mousePos.x - canvasPos.x) / frameWidth);
        if (hoveredFrame >= 0 &&
            std::cmp_less(hoveredFrame, mFrameHistory.size())) {
            const auto& frame = mFrameHistory[hoveredFrame];

            ImGui::BeginTooltip();
            ImGui::Text("Frame %d", hoveredFrame);
            ImGui::Text("Total: %.3f ms", frame.totalFrameTimeMs);
            ImGui::Separator();

            for (const auto& query : frame.queries) {
                if (!query.name.empty() && query.elapsedMs > 0.0) {
                    ImGui::Text("%s: %.3f ms", query.name.c_str(), query.elapsedMs);
                }
            }
            ImGui::EndTooltip();
          }
    }

    ImGui::Dummy(ImVec2(canvasSize.x, canvasSize.y));
}

void GPUProfilerImGui::drawTimingTree(const std::vector<GPUTimeQuery>& queries) {
    if (ImGui::BeginTable("GPUTimingTree", 3,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Time (ms)", ImGuiTableColumnFlags_WidthFixed,
                                80.0F);
        ImGui::TableSetupColumn("% of Frame", ImGuiTableColumnFlags_WidthFixed,
                                80.0F);
        ImGui::TableHeadersRow();

        double totalTimeMs = 0.0;
        for (const auto& query : queries) {
            if (!query.name.empty() && query.depth == 0) {
                totalTimeMs += query.elapsedMs;
            }
        }

        if (totalTimeMs <= 0.0) {
          totalTimeMs = 16.67;
        }

        for (const auto& query : queries) {
          if (query.name.empty() || query.elapsedMs <= 0.0) {
            continue;
          }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            std::string indent(static_cast<size_t>(query.depth * 2), ' ');

            uint32_t color = hashStringToColor(query.name);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
            ImGui::Text("%s%s", indent.c_str(), query.name.c_str());
            ImGui::PopStyleColor();

            ImGui::TableSetColumnIndex(1);
            if (query.elapsedMs > 1.0) {
              ImGui::TextColored(ImVec4(1.0F, 0.3F, 0.3F, 1.0F), "%.3f",
                                 query.elapsedMs);
            } else {
                ImGui::Text("%.3f", query.elapsedMs);
            }

            ImGui::TableSetColumnIndex(2);
            auto percentage = float((query.elapsedMs / totalTimeMs) * 100.0F);
            ImGui::Text("%.1f%%", percentage);
        }

        ImGui::EndTable();
    }
}

void GPUProfilerImGui::drawStatisticsTable() {
    if (ImGui::BeginTable("GPUStats", 5,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable)) {
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed,
                                80.0F);
        ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_WidthFixed,
                                80.0F);
        ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed,
                                80.0F);
        ImGui::TableSetupColumn("Samples", ImGuiTableColumnFlags_WidthFixed,
                                70.0F);
        ImGui::TableHeadersRow();

        std::vector<std::pair<std::string, GPUTimingStatistics>> sortedStats;
        sortedStats.reserve(mStatistics.size());
        for (const auto &item : mStatistics) {
          sortedStats.emplace_back(item);
        }

        std::ranges::sort(sortedStats, [](const auto &a, const auto &b) {
          return a.second.avgMs > b.second.avgMs;
        });

        for (const auto& [name, stats] : sortedStats) {
          if (stats.sampleCount == 0) {
            continue;
          }

            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImVec2 cursorPos = ImGui::GetCursorScreenPos();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                ImVec2(cursorPos.x, cursorPos.y + 2),
                ImVec2(cursorPos.x + 8, cursorPos.y + ImGui::GetTextLineHeight() - 2),
                stats.color
            );
            ImGui::Dummy(ImVec2(10, 0));
            ImGui::SameLine();
            ImGui::Text("%s", name.c_str());

            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%.3f", stats.avgMs);

            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%.3f", stats.minMs);

            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%.3f", stats.maxMs);

            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%u", stats.sampleCount);
        }

        ImGui::EndTable();
    }
}

void GPUProfilerImGui::drawLegend() {
  if (mStatistics.empty()) {
    return;
  }

    ImGui::SameLine();
    ImGui::BeginChild("Legend", ImVec2(mLegendWidth, mGraphHeight), 1);

    ImGui::Text("Passes:");
    ImGui::Separator();

    for (const auto& [name, stats] : mStatistics) {

        ImVec2 cursorPos = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            cursorPos,
            ImVec2(cursorPos.x + 12, cursorPos.y + 12),
            stats.color
        );

        ImGui::Dummy(ImVec2(15, 12));
        ImGui::SameLine();
        ImGui::Text("%s", name.c_str());
        ImGui::Text("  %.2f ms", stats.avgMs);
    }

    ImGui::EndChild();
}

const char* GPUProfilerImGui::bottleneckToString(GPUBottleneck bottleneck) {
    switch (bottleneck) {
        case GPUBottleneck::None: return "None - Balanced";
        case GPUBottleneck::MemoryBound: return "Memory Bandwidth Bound";
        case GPUBottleneck::ComputeBound: return "ALU/Compute Bound";
        case GPUBottleneck::LatencyBound: return "Memory Latency Bound";
        case GPUBottleneck::RasterBound: return "Rasterization Bound";
        case GPUBottleneck::TextureBound: return "Texture Sampling Bound";
        case GPUBottleneck::ROPBound: return "ROP Throughput Bound";
        case GPUBottleneck::VertexProcessingBound: return "Vertex Processing Bound";
        case GPUBottleneck::GeometryBound: return "Geometry Processing Bound";
        case GPUBottleneck::SynchronizationBound: return "Synchronization Bound";
        default: return "Unknown";
    }
}

void GPUProfilerImGui::updateFrameHistory(const GPUFrameData& frameData) {
    mFrameHistory.push_back(frameData);

    if (mFrameHistory.size() > kMaxHistoryFrames) {
        mFrameHistory.pop_front();
    }

    for (const auto& query : frameData.queries) {
        if (!query.name.empty()) {
            auto& stats = mStatistics[query.name];
            stats.update(query.elapsedMs);
            stats.color = hashStringToColor(query.name);
        }
    }
}

void GPUProfilerImGui::resetStatistics() {
    mStatistics.clear();
    mFrameHistory.clear();
    mColorCache.clear();
}

uint32_t GPUProfilerImGui::hashStringToColor(const std::string& str) {
    auto it = mColorCache.find(str);
    if (it != mColorCache.end()) {
      return it->second;
    }

    uint32_t hash = 5381;
    for (char c : str) {
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
    }

    float h = std::fmod(float(hash % 360) / 360.0F, 1.0F);
    float s = 0.5F;
    float v = 0.85F;

    int i = int(h * 6);
    float f = (h * 6) - i;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r;
    float g;
    float b;
    switch (i % 6) {
        case 0: r = v, g = t, b = p; break;
        case 1: r = q, g = v, b = p; break;
        case 2: r = p, g = v, b = t; break;
        case 3: r = p, g = q, b = v; break;
        case 4: r = t, g = p, b = v; break;
        case 5: r = v, g = p, b = q; break;
    }

    uint32_t color = IM_COL32(int(r * 255), int(g * 255), int(b * 255), 255);
    mColorCache[str] = color;
    return color;
}

void GPUProfilerImGui::drawBindlessStatistics()
{
  if (m_bindlessManager == nullptr) {
    ImGui::TextDisabled("Bindless Manager not set.");
    return;
  }

    static rhi::BindlessStatistics cachedStats;
    static double lastRefreshTime = -1.0;
    double currentTime = ImGui::GetTime();
    if (currentTime - lastRefreshTime > 1.0) {
        cachedStats = m_bindlessManager->getStatistics();
        lastRefreshTime = currentTime;
    }
    const auto& stats = cachedStats;

    ImGui::Text("Bindless Resource Utilization");
    ImGui::Separator();

    if (ImGui::BeginTable("BindlessArraySummary", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("Array Name");
        ImGui::TableSetupColumn("Capacity");
        ImGui::TableSetupColumn("Occupied");
        ImGui::TableSetupColumn("Free (List)");
        ImGui::TableSetupColumn("Utilization");
        ImGui::TableHeadersRow();

        for (const auto& array : stats.arrays) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s", array.name.c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::Text("%u", array.capacity);
            ImGui::TableSetColumnIndex(2);
            ImGui::Text("%u", array.occupied);
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%u", array.freeListSize);
            ImGui::TableSetColumnIndex(4);
            float util = (float)array.occupied / (float)array.capacity;
            char utilBuf[32];
            snprintf(utilBuf, 32, "%.1f%%", util * 100.0F);
            ImGui::ProgressBar(util, ImVec2(-1, 0), utilBuf);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Detailed Array View");
    ImGui::Spacing();

    for (const auto& array : stats.arrays) {
        if (ImGui::TreeNode(array.name.c_str())) {
            if (array.occupied == 0) {
                ImGui::TextDisabled("No resources registered.");
            } else {
                if (ImGui::BeginTable("ArrayDetails", 5, ImGuiTableFlags_Resizable | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 300))) {
                  ImGui::TableSetupColumn(
                      "Slot", ImGuiTableColumnFlags_WidthFixed, 40.0F);
                  ImGui::TableSetupColumn("Name");
                  ImGui::TableSetupColumn("Dimensions");
                  ImGui::TableSetupColumn("Format");
                  ImGui::TableSetupColumn(
                      "Preview", ImGuiTableColumnFlags_WidthFixed, 40.0F);
                  ImGui::TableHeadersRow();

                  for (const auto &slot : array.slots) {

                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%u", slot.slotIndex);
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", slot.name.empty() ? "(unnamed)"
                                                        : slot.name.c_str());
                    ImGui::TableSetColumnIndex(2);
                    if (slot.width > 0) {
                      if (slot.height > 0) {
                        ImGui::Text("%ux%u", slot.width, slot.height);
                      } else {
                        ImGui::Text("%u Bytes", slot.width);
                      }
                    } else {
                      ImGui::Text("-");
                    }
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%s", formatToString(slot.format));

                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextDisabled("-");
                  }
                    ImGui::EndTable();
                }
            }
            ImGui::TreePop();
        }
    }
}

}
