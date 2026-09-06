#include "workspace/CardLayoutEngine.h"
#include "workspace/ExcerptPayload.h"

#include <cmath>
#include <iostream>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAILED: " << what << "\n";
        return 1;
    }
    return 0;
}

int testTextSnippetDimensions() {
    int failed = 0;
    ExcerptDropPayload payload;
    payload.isImageExcerpt = false;
    payload.textSnippet = "Short snippet";

    auto dims = CardLayoutEngine::computeExcerptCardDimensions(payload);
    failed += check(dims.first == 260.0, "Short text card width is 260");
    failed += check(dims.second == 140.0, "Short text card height is 140");

    payload.textSnippet = std::string(150, 'a');
    dims = CardLayoutEngine::computeExcerptCardDimensions(payload);
    failed += check(dims.first == 260.0, "Medium text card width is 260");
    failed += check(dims.second == 170.0, "Medium text card height is 170");

    payload.textSnippet = std::string(300, 'a');
    dims = CardLayoutEngine::computeExcerptCardDimensions(payload);
    failed += check(dims.first == 260.0, "Long text card width is 260");
    failed += check(dims.second == 220.0, "Long text card height is 220");

    return failed;
}

int testImageExcerptDimensions() {
    int failed = 0;
    ExcerptDropPayload payload;
    payload.isImageExcerpt = true;
    payload.sourcePageWidth = 612.0;
    payload.sourcePageHeight = 792.0;
    payload.sourceNormalizedRect = {0.1, 0.1, 0.5, 0.4}; // 306 x 316.8 pt

    auto dims = CardLayoutEngine::computeExcerptCardDimensions(payload);
    failed += check(dims.first >= 200.0, "Image card width satisfies minimum floor");
    failed += check(dims.second > 46.0, "Image card height accommodates header and margins");

    return failed;
}

int testAnchorPillAndStackHeaderBounds() {
    int failed = 0;
    Rectangle cardBounds{100.0, 200.0, 260.0, 140.0};
    double originX = 50.0;
    double originY = 100.0;
    double zoom = 1.5;

    Rectangle anchorRect =
        CardLayoutEngine::getExcerptAnchorRect(cardBounds, originX, originY, zoom);
    failed += check(anchorRect.w == 16.0 * zoom, "Anchor width scaled with zoom");
    failed += check(anchorRect.h == 140.0 * zoom, "Anchor height matches card height");
    failed += check(anchorRect.x == (cardBounds.x - originX) * zoom,
                    "Anchor positioned on left edge of card");
    failed += check(anchorRect.y == (cardBounds.y - originY) * zoom, "Anchor y matches card top");

    Rectangle pillRect =
        CardLayoutEngine::getExcerptAnchorPillRect(cardBounds, originX, originY, zoom);
    failed += check(pillRect.w == anchorRect.w && pillRect.h == anchorRect.h,
                    "Pill rect compatibility wrapper matches anchor rect");

    Rectangle stackBounds{100.0, 200.0, 300.0, 200.0};
    Rectangle hdrRect = CardLayoutEngine::getStackHeaderRect(stackBounds, originX, originY, zoom);
    failed += check(hdrRect.w == 300.0 * zoom, "Stack header matches stack width");
    failed += check(hdrRect.h == 32.0 * zoom, "Stack header has standard 32pt scaled height");

    Rectangle chevRect = CardLayoutEngine::getStackChevronRect(stackBounds, originX, originY, zoom);
    failed += check(chevRect.x == (stackBounds.x - originX) * zoom + 6.0 * zoom,
                    "Chevron x has 6pt margin");
    failed += check(chevRect.w <= hdrRect.h, "Chevron fits inside header");

    return failed;
}

} // namespace

int main() {
    int failed = 0;
    failed += testTextSnippetDimensions();
    failed += testImageExcerptDimensions();
    failed += testAnchorPillAndStackHeaderBounds();

    if (failed == 0) {
        std::cout << "All CardLayoutEngine tests passed!\n";
    }
    return failed;
}
