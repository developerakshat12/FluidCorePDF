#include "services/ToolManager.h"

#include <algorithm>
#include <cctype>
#include <cstdint>

namespace FluidCoreApp {

const char* ToolManager::toolToString(Tool tool) {
    switch (tool) {
    case Tool::Select:
        return "select";
    case Tool::Pen:
        return "pen";
    case Tool::Highlighter:
        return "highlighter";
    case Tool::Eraser:
        return "eraser";
    case Tool::Crop:
        return "crop";
    case Tool::Connector:
        return "connector";
    }
    return "select";
}

Tool ToolManager::toolFromString(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "pen" || lower == "brush")
        return Tool::Pen;
    if (lower == "highlighter" || lower == "highlight")
        return Tool::Highlighter;
    if (lower == "eraser" || lower == "erase")
        return Tool::Eraser;
    if (lower == "crop")
        return Tool::Crop;
    if (lower == "connector" || lower == "link")
        return Tool::Connector;
    return Tool::Select;
}

void ToolManager::setActiveTool(Tool tool) {
    if (isInkingTool(tool)) {
        m_lastInkingTool = tool;
    }
    if (m_currentTool == tool) {
        return;
    }
    m_currentTool = tool;
    for (const auto& listener : m_listeners) {
        if (listener) {
            listener(m_currentTool);
        }
    }
    if (isInkingTool(m_currentTool)) {
        notifyPropertyChange(m_currentTool, propertiesForTool(m_currentTool));
    }
}

void ToolManager::toggleEraser() {
    if (m_currentTool == Tool::Eraser) {
        setActiveTool(m_lastInkingTool);
    } else {
        setActiveTool(Tool::Eraser);
    }
}

void ToolManager::setActiveToolByName(const std::string& name) {
    setActiveTool(toolFromString(name));
}

InkProperties ToolManager::propertiesForTool(Tool tool) const {
    if (tool == Tool::Highlighter) {
        return m_highlighterProperties;
    }
    return m_penProperties;
}

void ToolManager::setPropertiesForTool(Tool tool, const InkProperties& properties) {
    InkProperties clamped = properties;
    if (tool == Tool::Highlighter) {
        clamped.width = std::clamp(clamped.width, InkingLimits::HighlighterMinWidth,
                                   InkingLimits::HighlighterMaxWidth);
        if (m_highlighterProperties == clamped) {
            return;
        }
        m_highlighterProperties = clamped;
    } else {
        clamped.width =
            std::clamp(clamped.width, InkingLimits::PenMinWidth, InkingLimits::PenMaxWidth);
        if (m_penProperties == clamped) {
            return;
        }
        m_penProperties = clamped;
    }

    notifyPropertyChange(tool, clamped);
}

InkProperties ToolManager::activeProperties() const {
    if (isInkingTool(m_currentTool)) {
        return propertiesForTool(m_currentTool);
    }
    return propertiesForTool(m_lastInkingTool);
}

void ToolManager::setActiveColor(std::uint32_t color) {
    Tool target = isInkingTool(m_currentTool) ? m_currentTool : m_lastInkingTool;
    InkProperties props = propertiesForTool(target);
    props.color = color;
    setPropertiesForTool(target, props);
}

void ToolManager::setActiveWidth(double width) {
    Tool target = isInkingTool(m_currentTool) ? m_currentTool : m_lastInkingTool;
    InkProperties props = propertiesForTool(target);
    props.width = width;
    setPropertiesForTool(target, props);
}

void ToolManager::stepActiveWidth(double delta) {
    Tool target = isInkingTool(m_currentTool) ? m_currentTool : m_lastInkingTool;
    InkProperties props = propertiesForTool(target);
    props.width += delta;
    setPropertiesForTool(target, props);
}

void ToolManager::notifyPropertyChange(Tool tool, const InkProperties& props) {
    for (const auto& listener : m_propertyListeners) {
        if (listener) {
            listener(tool, props);
        }
    }
}

void ToolManager::addChangeListener(ChangeListener listener) {
    if (listener) {
        m_listeners.push_back(std::move(listener));
    }
}

void ToolManager::addPropertyChangeListener(PropertyChangeListener listener) {
    if (listener) {
        m_propertyListeners.push_back(std::move(listener));
    }
}

} // namespace FluidCoreApp
