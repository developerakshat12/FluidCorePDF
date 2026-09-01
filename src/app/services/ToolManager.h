#pragma once

#include <functional>
#include <string>
#include <vector>

namespace FluidCoreApp {

enum class Tool {
    Select,
    Pen,
    Highlighter,
    Eraser,
    Crop,
    Connector
};

class ToolManager {
  public:
    using ChangeListener = std::function<void(Tool)>;

    ToolManager() = default;
    ~ToolManager() = default;

    Tool activeTool() const { return m_currentTool; }
    void setActiveTool(Tool tool);
    void setActiveToolByName(const std::string& name);

    void addChangeListener(ChangeListener listener);

    static const char* toolToString(Tool tool);
    static Tool toolFromString(const std::string& name);

  private:
    Tool m_currentTool = Tool::Select;
    std::vector<ChangeListener> m_listeners;
};

} // namespace FluidCoreApp
