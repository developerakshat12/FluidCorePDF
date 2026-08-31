#include "workspace/ExcerptCardNode.h"

#include <algorithm>
#include <utility>

namespace FluidCore {

namespace {

Rectangle clampNormalizedRect(const Rectangle& r) {
    double x = std::clamp(r.x, 0.0, 1.0);
    double y = std::clamp(r.y, 0.0, 1.0);
    double maxW = 1.0 - x;
    double maxH = 1.0 - y;
    double w = std::clamp(r.w, 0.0, maxW);
    double h = std::clamp(r.h, 0.0, maxH);
    return {x, y, w, h};
}

} // namespace

ExcerptCardNode::ExcerptCardNode(std::string id, Rectangle bounds, std::string sourceDocId,
                                 size_t sourcePageNo, Rectangle sourceNormalizedRect,
                                 std::string textSnippet, bool isImageExcerpt, Color color,
                                 uint64_t creationTimestamp)
    : m_id(std::move(id)), m_bounds(bounds), m_sourceDocId(std::move(sourceDocId)),
      m_sourcePageNo(sourcePageNo),
      m_sourceNormalizedRect(clampNormalizedRect(sourceNormalizedRect)),
      m_textSnippet(std::move(textSnippet)), m_isImageExcerpt(isImageExcerpt), m_color(color),
      m_creationTimestamp(creationTimestamp) {}

std::unique_ptr<WorkspaceNode> ExcerptCardNode::clone() const {
    auto copy = std::make_unique<ExcerptCardNode>(m_id, m_bounds, m_sourceDocId, m_sourcePageNo,
                                                  m_sourceNormalizedRect, m_textSnippet,
                                                  m_isImageExcerpt, m_color, m_creationTimestamp);
    copy->setTags(m_tags);
    return copy;
}

void ExcerptCardNode::setSourceNormalizedRect(const Rectangle& rect) {
    m_sourceNormalizedRect = clampNormalizedRect(rect);
}

void ExcerptCardNode::addTag(std::string tag) {
    if (tag.empty()) {
        return;
    }
    if (!hasTag(tag)) {
        m_tags.push_back(std::move(tag));
    }
}

bool ExcerptCardNode::hasTag(const std::string& tag) const {
    return std::find(m_tags.begin(), m_tags.end(), tag) != m_tags.end();
}

} // namespace FluidCore
