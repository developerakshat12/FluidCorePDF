#pragma once

#include "storage/AnnotationStore.h"
#include "undo/Command.h"

#include <cstddef>
#include <string>
#include <vector>

namespace FluidCore {

// Command representing the addition of an ink stroke to an AnnotationStore page.
class AddStrokeCommand : public Command {
  public:
    AddStrokeCommand(AnnotationStore& store, std::size_t pageIdx, Stroke stroke);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Add Stroke"; }
    std::size_t estimatedSizeBytes() const override;

    const Stroke& stroke() const { return m_stroke; }
    std::size_t pageIndex() const { return m_pageIdx; }

  private:
    AnnotationStore& m_store;
    std::size_t m_pageIdx;
    Stroke m_stroke;
};

// Command representing the deletion/erasure of an ink stroke from an AnnotationStore page.
class RemoveStrokeCommand : public Command {
  public:
    RemoveStrokeCommand(AnnotationStore& store, std::size_t pageIdx, Stroke stroke);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Erase Stroke"; }
    std::size_t estimatedSizeBytes() const override;

    const Stroke& stroke() const { return m_stroke; }
    std::size_t pageIndex() const { return m_pageIdx; }

  private:
    AnnotationStore& m_store;
    std::size_t m_pageIdx;
    Stroke m_stroke;
};

// Command representing clearing all strokes from a specific page.
class ClearPageStrokesCommand : public Command {
  public:
    ClearPageStrokesCommand(AnnotationStore& store, std::size_t pageIdx);

    bool execute() override;
    bool undo() override;
    bool redo() override;

    std::string description() const override { return "Clear Page Annotations"; }
    std::size_t estimatedSizeBytes() const override;

    std::size_t pageIndex() const { return m_pageIdx; }
    const std::vector<Stroke>& savedStrokes() const { return m_savedStrokes; }

  private:
    AnnotationStore& m_store;
    std::size_t m_pageIdx;
    std::vector<Stroke> m_savedStrokes;
};

} // namespace FluidCore
