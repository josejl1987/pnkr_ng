#include "pnkr/app/ConsoleWindow.hpp"
#include "pnkr/core/cvar.hpp"
#include "pnkr/core/logger.hpp"
#include <algorithm>
#include <cctype>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <sstream>
#include <utility>

namespace pnkr::app {

static int stricmp(const char *s1, const char *s2) {
  int d = 0;
  while ((d = toupper(*s2) - toupper(*s1)) == 0 && (*s1 != 0)) {
    s1++;
    s2++;
  }
  return d;
}

    void ConsoleWindow::execCommand(const std::string& cmdLine)
    {
        m_log.push_back("# " + cmdLine);
        m_scrollToBottom = true;

        m_historyPos = -1;
        for (int i = (int)m_cmdHistory.size() - 1; i >= 0; i--)
        {
            if (m_cmdHistory[i] == cmdLine)
            {
                m_cmdHistory.erase(m_cmdHistory.begin() + i);
                break;
            }
        }
        m_cmdHistory.push_back(cmdLine);

        std::stringstream ss(cmdLine);
        std::string cmd;
        ss >> cmd;

        if (cmd.empty()) {
          return;
        }

        std::string valStr;
        std::string temp;
        std::getline(ss, valStr);
        if (!valStr.empty()) {
             size_t first = valStr.find_first_not_of(' ');
             if (std::string::npos != first)
             {
                 valStr = valStr.substr(first);
             }
        }

        if (stricmp(cmd.c_str(), "help") == 0) {
          m_log.emplace_back("Commands:");
          auto &all = core::CVarSystem::getAll();
          for (auto &[name, cv] : all) {
            m_log.push_back(name + ": " + cv->description);
          }
          m_log.emplace_back("clear: Clear console log");
        } else if (stricmp(cmd.c_str(), "clear") == 0) {
          m_log.clear();
        } else {
          auto *cv = core::CVarSystem::find(cmd);
          if (cv != nullptr) {
            if (valStr.empty()) {
              m_log.push_back(cv->name + " = " + cv->toString());
              m_log.push_back("  " + cv->description);
            } else {
              if ((cv->flags & core::CVarFlags::read_only)) {
                m_log.push_back("Error: " + cv->name + " is read-only.");
              } else {
                cv->setFromString(valStr);
                m_log.push_back(cv->name + " = " + cv->toString());
              }
            }
          } else {
            m_log.push_back("Unknown command: " + cmd);
          }
        }
    }

    int ConsoleWindow::textEditCallback(void* data)
    {
      auto *dataImgui = (ImGuiInputTextCallbackData *)data;
      switch (dataImgui->EventFlag) {
      case ImGuiInputTextFlags_CallbackHistory: {
        const int prevHistoryPos = m_historyPos;
        if (dataImgui->EventKey == ImGuiKey_UpArrow) {
          if (m_historyPos == -1) {
            m_historyPos = (int)m_cmdHistory.size() - 1;
          } else if (m_historyPos > 0) {
            m_historyPos--;
          }
        } else if (dataImgui->EventKey == ImGuiKey_DownArrow) {
          if (m_historyPos != -1) {
            if (std::cmp_greater_equal(++m_historyPos, m_cmdHistory.size())) {
              m_historyPos = -1;
            }
          }
        }

        if (prevHistoryPos != m_historyPos) {
          const char *historyStr =
              (m_historyPos >= 0) ? m_cmdHistory[m_historyPos].c_str() : "";
          dataImgui->DeleteChars(0, dataImgui->BufTextLen);
          dataImgui->InsertChars(0, historyStr);
        }
        }
        break;
        }
        return 0;
    }

    void ConsoleWindow::draw(bool *pOpen) {
      if (!ImGui::Begin("Console", pOpen)) {
        ImGui::End();
        return;
      }

      const float footerHeightToReserve =
          ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing();
      if (ImGui::BeginChild("ScrollingRegion",
                            ImVec2(0, -footerHeightToReserve), 0,
                            ImGuiWindowFlags_HorizontalScrollbar)) {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 1));
        for (const auto &item : m_log) {
          bool hasColor = false;
          if (item.starts_with("# ")) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0F, 0.8F, 0.6F, 1.0F));
            hasColor = true;
          } else if (item.starts_with("Error")) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0F, 0.4F, 0.4F, 1.0F));
            hasColor = true;
          }

          ImGui::TextUnformatted(item.c_str());
          if (hasColor) {
            ImGui::PopStyleColor();
          }
        }

        if (m_scrollToBottom ||
            (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())) {
          ImGui::SetScrollHereY(1.0F);
        }
        m_scrollToBottom = false;

        ImGui::PopStyleVar();
      }
      ImGui::EndChild();

      ImGui::Separator();

      bool reclaimFocus = false;
      ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue |
                                       ImGuiInputTextFlags_CallbackCompletion |
                                       ImGuiInputTextFlags_CallbackHistory;

      ImGui::SetKeyboardFocusHere();

      if (ImGui::InputText(
              "Input", m_inputBuf, IM_ARRAYSIZE(m_inputBuf), inputFlags,
              [](ImGuiInputTextCallbackData *data) {
                auto *console = (ConsoleWindow *)data->UserData;
                return console->textEditCallback(data);
              },
              (void *)this)) {
        std::string s = m_inputBuf;
        if (!s.empty()) {
          execCommand(s);
        }
        m_inputBuf[0] = 0;
        reclaimFocus = true;
      }

      if (reclaimFocus) {
        ImGui::SetKeyboardFocusHere(-1);
      }

      ImGui::End();
    }
}
