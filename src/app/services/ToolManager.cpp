#include "services/ToolManager.h"

#include <algorithm>
#include <cctype>

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

    if (lower == "pen")
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
    if (m_currentTool == tool) {
        return;
    }
    m_currentTool = tool;
    for (const auto& listener : m_listeners) {
        if (listener) {
            listener(m_currentTool);
        }
    }
}

void ToolManager::setActiveToolByName(const std::string& name) {
    setActiveTool(toolFromString(name));
}

void ToolManager::addChangeListener(ChangeListener listener) {
    if (listener) {
        m_listeners.push_back(std::move(listener));
    }
}

} // namespace FluidCoreApp
