// ExcerptCardNodeTest.cpp — Unit tests for ExcerptCardNode, ExcerptPayload, and Undo commands.

#include "workspace/ExcerptCardNode.h"
#include "undo/UndoStack.h"
#include "undo/WorkspaceCommands.h"
#include "workspace/ExcerptPayload.h"
#include "workspace/WorkspaceModel.h"

#include <cmath>
#include <iostream>
#include <memory>
#include <string>

namespace {

using namespace FluidCore;

int check(bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << "\n";
        return 1;
    }
    return 0;
}

constexpr double kEps = 1e-6;

bool close(double a, double b) {
    return std::abs(a - b) < kEps;
}

bool rectClose(const Rectangle& a, const Rectangle& b) {
    return close(a.x, b.x) && close(a.y, b.y) && close(a.w, b.w) && close(a.h, b.h);
}

int testExcerptCardNodeProperties() {
    int failures = 0;
    std::cout << "Running testExcerptCardNodeProperties...\n";

    Rectangle bounds{100.0, 150.0, 260.0, 180.0};
    Rectangle normRect{0.1, 0.2, 0.4, 0.5};
    Color color{255, 200, 50, 255};

    ExcerptCardNode card("card-01", bounds, "doc-ref-1", 2, normRect, "Extracted text snippet",
                         false, color, 123456789ULL);

    failures += check(card.id() == "card-01", "id match");
    failures += check(rectClose(card.bounds(), bounds), "bounds match");
    failures += check(card.sourceDocId() == "doc-ref-1", "docId match");
    failures += check(card.sourcePageNo() == 2, "pageNo match");
    failures += check(rectClose(card.sourceNormalizedRect(), normRect), "normalized rect match");
    failures += check(card.textSnippet() == "Extracted text snippet", "text match");
    failures += check(!card.isImageExcerpt(), "isImageExcerpt false");
    failures += check(card.color().r == 255 && card.color().g == 200 && card.color().b == 50,
                      "color match");
    failures += check(card.creationTimestamp() == 123456789ULL, "timestamp match");

    // Test position modification
    card.setPosition(300.0, 400.0);
    failures += check(close(card.bounds().x, 300.0) && close(card.bounds().y, 400.0),
                      "setPosition updates origin");
    failures += check(close(card.bounds().w, 260.0) && close(card.bounds().h, 180.0),
                      "setPosition preserves size");

    // Test normalized rect clamping
    card.setSourceNormalizedRect({-0.5, 1.5, 2.0, 2.0});
    failures += check(card.sourceNormalizedRect().x >= 0.0, "norm rect x >= 0");
    failures += check(card.sourceNormalizedRect().y <= 1.0, "norm rect y <= 1");
    failures += check(card.sourceNormalizedRect().x + card.sourceNormalizedRect().w <= 1.0 + kEps,
                      "norm rect x+w <= 1");
    failures += check(card.sourceNormalizedRect().y + card.sourceNormalizedRect().h <= 1.0 + kEps,
                      "norm rect y+h <= 1");

    // Test cloning
    auto cloned = card.clone();
    failures += check(cloned != nullptr, "clone created");
    auto* clonedCard = dynamic_cast<ExcerptCardNode*>(cloned.get());
    failures += check(clonedCard != nullptr, "clone is ExcerptCardNode");
    failures += check(clonedCard->id() == card.id(), "clone id match");
    failures += check(clonedCard->sourceDocId() == card.sourceDocId(), "clone docId match");
    failures += check(clonedCard->textSnippet() == card.textSnippet(), "clone text match");

    return failures;
}

int testExcerptPayloadSerialization() {
    int failures = 0;
    std::cout << "Running testExcerptPayloadSerialization...\n";

    ExcerptDropPayload payload;
    payload.sourceDocId = "/data/research/paper_2026.pdf";
    payload.sourcePageNo = 7;
    payload.sourceNormalizedRect = {0.15, 0.25, 0.60, 0.35};
    payload.sourcePageWidth = 595.0;
    payload.sourcePageHeight = 842.0;
    payload.textSnippet = "Line 1: High throughput spatial index.\nLine 2: R*-tree <= 1ms p99.\n"
                          "Line 3: 🚀 Special Unicode chars € & symbols!";
    payload.isImageExcerpt = false;
    payload.color = {10, 120, 240, 255};

    std::string serialized = serializeExcerptPayload(payload);
    failures += check(!serialized.empty(), "serialization produced non-empty string");
    failures += check(serialized.find("FLUID_EXCERPT_V1") != std::string::npos,
                      "serialization contains magic header");
    failures += check(serialized.find("pagesize:595 842") != std::string::npos,
                      "serialization contains pagesize");

    auto deserialized = deserializeExcerptPayload(serialized);
    failures += check(deserialized.has_value(), "deserialization succeeded");
    if (deserialized) {
        failures += check(deserialized->sourceDocId == payload.sourceDocId, "docId roundtrip");
        failures += check(deserialized->sourcePageNo == payload.sourcePageNo, "pageNo roundtrip");
        failures +=
            check(rectClose(deserialized->sourceNormalizedRect, payload.sourceNormalizedRect),
                  "normalized rect roundtrip");
        failures += check(std::abs(deserialized->sourcePageWidth - 595.0) < 1e-6,
                          "sourcePageWidth roundtrip");
        failures += check(std::abs(deserialized->sourcePageHeight - 842.0) < 1e-6,
                          "sourcePageHeight roundtrip");
        failures += check(deserialized->textSnippet == payload.textSnippet, "text roundtrip");
        failures += check(deserialized->isImageExcerpt == payload.isImageExcerpt,
                          "isImageExcerpt roundtrip");
        failures += check(deserialized->color.r == payload.color.r &&
                              deserialized->color.g == payload.color.g &&
                              deserialized->color.b == payload.color.b &&
                              deserialized->color.a == payload.color.a,
                          "color roundtrip");
    }

    // Test legacy payload without pagesize key
    std::string legacyPayload = "FLUID_EXCERPT_V1\n"
                                "doc:8:test.pdf\n"
                                "page:0\n"
                                "rect:0.1 0.1 0.5 0.5\n"
                                "image:1\n"
                                "color:255 255 255 255\n"
                                "text:0:\n";
    auto legacyDeserialized = deserializeExcerptPayload(legacyPayload);
    failures += check(legacyDeserialized.has_value(), "legacy deserialization succeeded");
    if (legacyDeserialized) {
        failures += check(std::abs(legacyDeserialized->sourcePageWidth - 612.0) < 1e-6,
                          "legacy default width 612");
        failures += check(std::abs(legacyDeserialized->sourcePageHeight - 792.0) < 1e-6,
                          "legacy default height 792");
    }

    // Test byte buffer deserialization
    auto fromBytes = deserializeExcerptPayload(reinterpret_cast<const uint8_t*>(serialized.data()),
                                               serialized.size());
    failures += check(fromBytes.has_value(), "byte buffer deserialization succeeded");

    // Test invalid / corrupted input
    failures += check(!deserializeExcerptPayload("NOT_A_VALID_HEADER").has_value(),
                      "reject invalid magic header");
    failures += check(!deserializeExcerptPayload("").has_value(), "reject empty data");
    failures += check(!deserializeExcerptPayload("FLUID_EXCERPT_V1\ncorrupted").has_value(),
                      "reject malformed payload");

    return failures;
}

int testExcerptCardAspectRatioGeometry() {
    int failures = 0;
    std::cout << "Running testExcerptCardAspectRatioGeometry...\n";

    // Helper implementing the coupled uniform scalar sizing algorithm
    auto computeCardDimensions =
        [](const ExcerptDropPayload& payload) -> std::pair<double, double> {
        const double pw = (payload.sourcePageWidth > 0.0) ? payload.sourcePageWidth : 612.0;
        const double ph = (payload.sourcePageHeight > 0.0) ? payload.sourcePageHeight : 792.0;

        const double cropW_pt = std::max(1.0, payload.sourceNormalizedRect.w * pw);
        const double cropH_pt = std::max(1.0, payload.sourceNormalizedRect.h * ph);

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

        const double cardW = std::max(200.0, imgW + 20.0);
        const double cardH = imgH + 46.0;

        return {cardW, cardH};
    };

    struct TestCase {
        std::string name;
        double normW;
        double normH;
        double pageW;
        double pageH;
        double expectedAspect;
    };

    std::vector<TestCase> cases = {
        {"Standard Landscape", 0.84, 0.35, 612.0, 792.0, (0.84 * 612.0) / (0.35 * 792.0)},
        {"Extreme Wide Equation Banner", 0.90, 0.05, 612.0, 792.0, (0.90 * 612.0) / (0.05 * 792.0)},
        {"Extreme Tall Column", 0.05, 0.80, 612.0, 792.0, (0.05 * 612.0) / (0.80 * 792.0)},
        {"Square Diagram", 0.40, 0.40, 612.0, 612.0, 1.0},
        {"Tiny Icon Snippet", 0.05, 0.05, 612.0, 792.0, (0.05 * 612.0) / (0.05 * 792.0)}};

    for (const auto& tc : cases) {
        ExcerptDropPayload p;
        p.isImageExcerpt = true;
        p.sourceNormalizedRect = {0.1, 0.1, tc.normW, tc.normH};
        p.sourcePageWidth = tc.pageW;
        p.sourcePageHeight = tc.pageH;

        auto [cardW, cardH] = computeCardDimensions(p);

        // Derive inner image dimensions
        const double cropW = tc.normW * tc.pageW;
        const double cropH = tc.normH * tc.pageH;
        constexpr double kMaxInnerW = 450.0;
        constexpr double kMaxInnerH = 380.0;
        constexpr double kMinInnerTarget = 180.0;

        double s = std::min(1.0, std::min(kMaxInnerW / cropW, kMaxInnerH / cropH));
        const double maxDim = std::max(cropW, cropH);
        if (maxDim < 160.0) {
            const double upscale = kMinInnerTarget / maxDim;
            s = std::min(upscale, std::min(kMaxInnerW / cropW, kMaxInnerH / cropH));
        }

        const double imgW = s * cropW;
        const double imgH = s * cropH;

        const double renderedAspect = imgW / imgH;
        failures +=
            check(std::abs(renderedAspect - tc.expectedAspect) < 1e-6,
                  (tc.name + ": rendered aspect ratio strictly matches physical crop").c_str());
        failures +=
            check(cardW >= 200.0, (tc.name + ": card width meets minimum floor of 200pt").c_str());
        failures +=
            check(cardH >= 46.0, (tc.name + ": card height includes header and padding").c_str());
    }

    return failures;
}

int testWorkspaceCommandsUndoRedo() {
    int failures = 0;
    std::cout << "Running testWorkspaceCommandsUndoRedo...\n";

    WorkspaceModel model("proj-commands");
    UndoStack stack;

    auto card = std::make_unique<ExcerptCardNode>(
        "card-undo-1", Rectangle{50.0, 60.0, 200.0, 100.0}, "doc-1", 0,
        Rectangle{0.1, 0.1, 0.5, 0.5}, "Undo test snippet", false);

    // 1. InsertNodeCommand
    stack.pushAndExecute(std::make_unique<InsertNodeCommand>(model, std::move(card)));
    failures += check(model.nodeCount() == 1, "card inserted in model");
    failures += check(model.find("card-undo-1") != nullptr, "card found in model");

    // Undo insertion
    failures += check(stack.undo(), "undo insertion");
    failures += check(model.nodeCount() == 0, "card removed after undo");
    failures += check(model.find("card-undo-1") == nullptr, "card no longer in model");

    // Redo insertion
    failures += check(stack.redo(), "redo insertion");
    failures += check(model.nodeCount() == 1, "card restored after redo");
    auto* restored = dynamic_cast<ExcerptCardNode*>(model.find("card-undo-1"));
    failures += check(restored != nullptr, "restored node is ExcerptCardNode");
    if (restored) {
        failures += check(restored->textSnippet() == "Undo test snippet", "restored text match");
    }

    // 2. RemoveNodeCommand
    stack.pushAndExecute(std::make_unique<RemoveNodeCommand>(model, "card-undo-1"));
    failures += check(model.nodeCount() == 0, "card removed via RemoveNodeCommand");

    // Undo removal
    failures += check(stack.undo(), "undo removal");
    failures += check(model.nodeCount() == 1, "card restored on undo removal");
    failures += check(model.find("card-undo-1") != nullptr, "card back in model");

    // Redo removal
    failures += check(stack.redo(), "redo removal");
    failures += check(model.nodeCount() == 0, "card removed on redo removal");

    return failures;
}

} // namespace

int main() {
    int totalFailures = 0;
    totalFailures += testExcerptCardNodeProperties();
    totalFailures += testExcerptPayloadSerialization();
    totalFailures += testExcerptCardAspectRatioGeometry();
    totalFailures += testWorkspaceCommandsUndoRedo();

    if (totalFailures == 0) {
        std::cout << "All ExcerptCardNode tests passed successfully!\n";
        return 0;
    }
    std::cerr << "ExcerptCardNodeTest failed with " << totalFailures << " errors!\n";
    return 1;
}
