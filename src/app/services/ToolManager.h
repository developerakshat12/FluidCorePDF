#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace FluidCoreApp {

enum class Tool { Select, Pen, Highlighter, Eraser, Crop, Connector };

struct InkProperties {
    std::uint32_t color = 0x000000;
    double width = 2.0;

    bool operator==(const InkProperties&) const = default;
};

namespace InkingLimits {
constexpr double PenMinWidth = 0.5;
constexpr double PenMaxWidth = 30.0;
constexpr double PenStepWidth = 0.5;

constexpr double HighlighterMinWidth = 4.0;
constexpr double HighlighterMaxWidth = 50.0;
constexpr double HighlighterStepWidth = 2.0;
} // namespace InkingLimits

class ToolManager {
  public:
    using ChangeListener = std::function<void(Tool)>;
    using PropertyChangeListener = std::function<void(Tool, const InkProperties&)>;

    ToolManager() = default;
    ~ToolManager() = default;

    Tool activeTool() const { return m_currentTool; }
    Tool lastInkingTool() const { return m_lastInkingTool; }
    void setActiveTool(Tool tool);
    void setActiveToolByName(const std::string& name);

    /// Toggles between Eraser and the last inking tool (Pen or Highlighter).
    /// If currently on Eraser, switches to m_lastInkingTool.
    /// If currently on another tool, switches to Eraser.
    void toggleEraser();

    static bool isInkingTool(Tool tool) { return tool == Tool::Pen || tool == Tool::Highlighter; }

    InkProperties propertiesForTool(Tool tool) const;
    void setPropertiesForTool(Tool tool, const InkProperties& properties);

    InkProperties activeProperties() const;
    std::uint32_t activeColor() const { return activeProperties().color; }
    double activeWidth() const { return activeProperties().width; }

    void setActiveColor(std::uint32_t color);
    void setActiveWidth(double width);
    void stepActiveWidth(double delta);

    void addChangeListener(ChangeListener listener);
    void addPropertyChangeListener(PropertyChangeListener listener);

    static const char* toolToString(Tool tool);
    static Tool toolFromString(const std::string& name);

  private:
    void notifyPropertyChange(Tool tool, const InkProperties& props);

    Tool m_currentTool = Tool::Select;
    Tool m_lastInkingTool = Tool::Pen;
    InkProperties m_penProperties{0x0F172A, 2.0};
    InkProperties m_highlighterProperties{0xFACC15, 14.0};

    std::vector<ChangeListener> m_listeners;
    std::vector<PropertyChangeListener> m_propertyListeners;
};

} // namespace FluidCoreApp
