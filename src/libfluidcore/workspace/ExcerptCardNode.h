#pragma once

#include "FluidCoreAPI.h"

#include <cstdint>
#include <memory>
#include <string>

namespace FluidCore {

// Spatial Excerpt Card placed on the infinite workspace canvas (specs/integration.md §2, TRD §3.4).
// Represents extracted text snippets or lassoed image regions from source PDF documents.
// Pure C++ domain model with zero Poppler/Cairo/GTK dependencies (ADR-0001).
class ExcerptCardNode final : public WorkspaceNode {
  public:
    ExcerptCardNode(std::string id, Rectangle bounds, std::string sourceDocId, size_t sourcePageNo,
                    Rectangle sourceNormalizedRect, std::string textSnippet = "",
                    bool isImageExcerpt = false, Color color = {255, 255, 255, 255},
                    uint64_t creationTimestamp = 0);

    ~ExcerptCardNode() override = default;

    const std::string& id() const override { return m_id; }
    Rectangle bounds() const override { return m_bounds; }
    std::unique_ptr<WorkspaceNode> clone() const override;

    void setBounds(const Rectangle& bounds) { m_bounds = bounds; }
    void setPosition(double x, double y) override {
        m_bounds.x = x;
        m_bounds.y = y;
    }

    const std::string& sourceDocId() const { return m_sourceDocId; }
    void setSourceDocId(std::string docId) { m_sourceDocId = std::move(docId); }

    size_t sourcePageNo() const { return m_sourcePageNo; }
    void setSourcePageNo(size_t pageNo) { m_sourcePageNo = pageNo; }

    Rectangle sourceNormalizedRect() const { return m_sourceNormalizedRect; }
    void setSourceNormalizedRect(const Rectangle& rect);

    const std::string& textSnippet() const { return m_textSnippet; }
    void setTextSnippet(std::string text) { m_textSnippet = std::move(text); }

    bool isImageExcerpt() const { return m_isImageExcerpt; }
    void setIsImageExcerpt(bool isImage) { m_isImageExcerpt = isImage; }

    Color color() const { return m_color; }
    void setColor(const Color& color) { m_color = color; }

    uint64_t creationTimestamp() const { return m_creationTimestamp; }
    void setCreationTimestamp(uint64_t timestamp) { m_creationTimestamp = timestamp; }

  private:
    std::string m_id;
    Rectangle m_bounds;
    std::string m_sourceDocId;
    size_t m_sourcePageNo = 0;
    Rectangle m_sourceNormalizedRect{0.0, 0.0, 1.0, 1.0};
    std::string m_textSnippet;
    bool m_isImageExcerpt = false;
    Color m_color{255, 255, 255, 255};
    uint64_t m_creationTimestamp = 0;
};

} // namespace FluidCore
