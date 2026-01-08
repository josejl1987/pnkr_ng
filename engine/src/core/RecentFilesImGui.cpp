#include "pnkr/core/RecentFilesImGui.hpp"

#include <imgui.h>

#include <string>

namespace pnkr::core {

std::optional<std::filesystem::path> RecentFilesImGui::drawMenu(RecentFilesStore& store,
                                                                const char* label)
{
    std::optional<std::filesystem::path> chosen;

    if (ImGui::BeginMenu(label))
    {
        const auto& items = store.items();
        if (items.empty()) {
            ImGui::MenuItem("(empty)", nullptr, false, false);
        } else {
            for (size_t i = 0; i < items.size(); ++i) {
                const auto& itemPath = items[i];
                const bool exists = std::filesystem::exists(itemPath);
                std::string menuLabel = std::to_string(i + 1) + "  " + itemPath.filename().string();

                if (ImGui::MenuItem(menuLabel.c_str(), nullptr, false, exists)) {
                    chosen = itemPath;
                }
                if (ImGui::IsItemHovered()) {
                    const std::string pathStr = itemPath.string();
                    ImGui::SetTooltip("%s", pathStr.c_str());
                }
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear")) {
                store.clear();
            }
        }

        ImGui::EndMenu();
    }

    return chosen;
}

}
