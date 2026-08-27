#include "undo/AnnotationCommands.h"

namespace FluidCore {

AddStrokeCommand::AddStrokeCommand(AnnotationStore& store, std::size_t pageIdx, Stroke stroke)
    : m_store(store), m_pageIdx(pageIdx), m_stroke(std::move(stroke)) {}

bool AddStrokeCommand::execute() {
    m_stroke.id = m_store.addStroke(m_pageIdx, m_stroke);
    return true;
}

bool AddStrokeCommand::undo() {
    return m_store.removeStroke(m_stroke.id);
}

bool AddStrokeCommand::redo() {
    m_store.addStroke(m_pageIdx, m_stroke);
    return true;
}

std::size_t AddStrokeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_stroke.points.capacity() * sizeof(XoppPoint) +
           m_stroke.pressures.capacity() * sizeof(double) + m_stroke.id.capacity();
}

RemoveStrokeCommand::RemoveStrokeCommand(AnnotationStore& store, std::size_t pageIdx, Stroke stroke)
    : m_store(store), m_pageIdx(pageIdx), m_stroke(std::move(stroke)) {}

bool RemoveStrokeCommand::execute() {
    return m_store.removeStroke(m_stroke.id);
}

bool RemoveStrokeCommand::undo() {
    m_store.addStroke(m_pageIdx, m_stroke);
    return true;
}

bool RemoveStrokeCommand::redo() {
    return m_store.removeStroke(m_stroke.id);
}

std::size_t RemoveStrokeCommand::estimatedSizeBytes() const {
    return sizeof(*this) + m_stroke.points.capacity() * sizeof(XoppPoint) +
           m_stroke.pressures.capacity() * sizeof(double) + m_stroke.id.capacity();
}

ClearPageStrokesCommand::ClearPageStrokesCommand(AnnotationStore& store, std::size_t pageIdx)
    : m_store(store), m_pageIdx(pageIdx) {
    m_savedStrokes = m_store.strokesForPage(m_pageIdx);
}

bool ClearPageStrokesCommand::execute() {
    for (const auto& s : m_savedStrokes) {
        m_store.removeStroke(s.id);
    }
    return true;
}

bool ClearPageStrokesCommand::undo() {
    for (const auto& s : m_savedStrokes) {
        m_store.addStroke(m_pageIdx, s);
    }
    return true;
}

bool ClearPageStrokesCommand::redo() {
    for (const auto& s : m_savedStrokes) {
        m_store.removeStroke(s.id);
    }
    return true;
}

std::size_t ClearPageStrokesCommand::estimatedSizeBytes() const {
    std::size_t total = sizeof(*this) + m_savedStrokes.capacity() * sizeof(Stroke);
    for (const auto& s : m_savedStrokes) {
        total += s.points.capacity() * sizeof(XoppPoint) + s.pressures.capacity() * sizeof(double) +
                 s.id.capacity();
    }
    return total;
}

} // namespace FluidCore
