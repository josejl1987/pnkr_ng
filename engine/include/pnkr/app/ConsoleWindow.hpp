#pragma once
#include <string>
#include <vector>

namespace pnkr::app {

class ConsoleWindow {
public:
    void draw(bool* p_open);
private:
    char m_inputBuf[256] = "";
    std::vector<std::string> m_history;
    std::vector<std::string> m_log;
    bool m_scrollToBottom = false;

    void execCommand(const std::string& cmdLine);
    int textEditCallback(void* data);

    std::vector<std::string> m_cmdHistory;
    int m_historyPos = -1;
};

}
