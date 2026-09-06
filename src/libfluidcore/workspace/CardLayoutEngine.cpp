#include "workspace/CardLayoutEngine.h"
#include "workspace/CardStackNode.h"

#include <algorithm>
#include <cmath>

namespace FluidCore {

std::pair<double, double>
CardLayoutEngine::computeExcerptCardDimensions(const ExcerptDropPayload& payload) {
    if (!payload.isImageExcerpt) {
        double cardW = 260.0;
        double cardH = 140.0;
        if (payload.textSnippet.size() > 250) {
            cardH = 220.0;
        } else if (payload.textSnippet.size() > 120) {
            cardH = 170.0;
        }
        return {cardW, cardH};
    }

    // Intentional defensive fallback for in-memory payloads that bypass string deserialization
    const double pw = (payload.sourcePageWidth > 0.0) ? payload.sourcePageWidth : 612.0;
    const double ph = (payload.sourcePageHeight > 0.0) ? payload.sourcePageHeight : 792.0;

    const double cropW_pt = std::max(1.0, payload.sourceNormalizedRect.w * pw);
    const double cropH_pt = std::max(1.0, payload.sourceNormalizedRect.h * ph);

    // Uniform scalar sizing to guarantee W_img / H_img == cropW_pt / cropH_pt == AR
    constexpr double kMaxInnerW = 450.0;
    constexpr double kMaxInnerH = 380.0;
    constexpr double kMinInnerTarget = 180.0;

    double s = std::min(1.0, std::min(kMaxInnerW / cropW_pt, kMaxInnerH / cropH_pt));
    const double maxDim = std::max(cropW_pt, cropH_pt);
    if (maxDim < 160.0) {
        const double upscale = kMinInnerTarget / maxDim;
        s = std::min(upscale, std::min(kMaxInnerW / cropW_pt, kMaxInnerH / cropH_pt));
    }

    const double imgW = s * cropW_pt;
    const double imgH = s * cropH_pt;

    // Outer card container: width floor of 200pt ensures title and left anchor bar fit cleanly
    const double cardW = std::max(200.0, imgW + 28.0);
    const double cardH = imgH + 46.0; // 28pt header + 6pt top gap + 12pt bottom margin

    return {cardW, cardH};
}

Rectangle CardLayoutEngine::getExcerptAnchorRect(const Rectangle& cardWorldBounds, double originX,
                                                 double originY, double zoom) {
    const double sx = (cardWorldBounds.x - originX) * zoom;
    const double sy = (cardWorldBounds.y - originY) * zoom;
    const double sh = cardWorldBounds.h * zoom;
    const double anchorW = 16.0 * zoom;
    return {sx, sy, anchorW, sh};
}

Rectangle CardLayoutEngine::getExcerptAnchorPillRect(const Rectangle& cardWorldBounds,
                                                     double originX, double originY, double zoom) {
    return getExcerptAnchorRect(cardWorldBounds, originX, originY, zoom);
}

Rectangle CardLayoutEngine::getStackHeaderRect(const Rectangle& stackWorldBounds, double originX,
                                               double originY, double zoom) {
    const double sx = (stackWorldBounds.x - originX) * zoom;
    const double sy = (stackWorldBounds.y - originY) * zoom;
    const double sw = stackWorldBounds.w * zoom;
    const double headerH = CardStackNode::kHeaderHeight * zoom;
    return {sx, sy, sw, headerH};
}

Rectangle CardLayoutEngine::getStackChevronRect(const Rectangle& stackWorldBounds, double originX,
                                                double originY, double zoom) {
    const double sx = (stackWorldBounds.x - originX) * zoom;
    const double sy = (stackWorldBounds.y - originY) * zoom;
    const double headerH = CardStackNode::kHeaderHeight * zoom;
    const double btnSize = std::min(24.0 * zoom, headerH);
    const double btnX = sx + 6.0 * zoom;
    const double btnY = sy + (headerH - btnSize) / 2.0;
    return {btnX, btnY, btnSize, btnSize};
}

} // namespace FluidCore
