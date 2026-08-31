#pragma once

#include "FluidCoreAPI.h"
#include "services/StrokeStabilizer.h"
#include "storage/AnnotationStore.h"
#include "workspace/ExcerptCardNode.h"
#include "workspace/PhysicsSolver.h"

#include <optional>
#include <string>
#include <vector>

#include <gtk/gtk.h>

namespace FluidCoreApp {

struct ViewportTransform {
    double originX = 0.0;
    double originY = 0.0;
    double zoom = 1.0;

    FluidCore::Point screenToWorld(double sx, double sy) const {
        return {originX + sx / zoom, originY + sy / zoom};
    }
    FluidCore::Point worldToScreen(double wx, double wy) const {
        return {(wx - originX) * zoom, (wy - originY) * zoom};
    }
};

struct DragSnapState {
    bool dragPending = false;
    double dragStartScreenX = 0.0;
    double dragStartScreenY = 0.0;
    std::string dragCandidateNodeId;
    bool dragCandidateIsChild = false;
    std::string dragCandidateParentStackId;
    FluidCore::Point dragInitialWorldPos{0.0, 0.0};
    FluidCore::Point dragOffsetWorld{0.0, 0.0};

    bool isDraggingCard = false;
    FluidCore::SnapType activeSnapType = FluidCore::SnapType::None;
    std::string activeMergeTargetId;
    std::vector<FluidCore::SnapGuideLine> activeSnapGuideLines;
    FluidCore::Rectangle draggedGhostBounds{0.0, 0.0, 0.0, 0.0};
};

struct InkingState {
    std::string currentTool = "select";
    uint32_t currentColor = 0x000000;
    double currentWidth = 1.5;
    bool isDrawing = false;
    FluidCore::Stroke activeStroke;
    std::vector<StrokeStabilizer::BezierSegment> activeSegments;
    StrokeStabilizer::Point2D activeWetTip;
    bool hasWetSegment = false;
    StrokeStabilizer stabilizer;
};

struct ConnectorState {
    bool isConnecting = false;
    std::string connectorSourceNodeId;
    FluidCore::Point connectorStartWorld{0.0, 0.0};
    FluidCore::Point connectorCurrentWorld{0.0, 0.0};
    std::string connectorTargetHoverNodeId;
};

struct AnimationState {
    guint glideTimerId = 0;
    double glideStartX = 0.0;
    double glideStartY = 0.0;
    double glideTargetX = 0.0;
    double glideTargetY = 0.0;
    gint64 glideStartTimeUs = 0;

    std::string flashCardId;
    double flashAlpha = 0.0;
    guint flashTimerId = 0;
    gint64 flashStartTimeUs = 0;

    guint zoomSettlingTimerId = 0;
};

struct WorkspaceSearchState {
    bool active = false;
    std::string query;
    std::vector<FluidCore::WorkspaceMatch> matches;
    int activeMatchIndex = -1;
    std::vector<std::string> matchingNodeIds;
    std::vector<std::string> matchingTopLevelNodeIds;

    bool hasMatch(const std::string& nodeId) const {
        return std::find(matchingNodeIds.begin(), matchingNodeIds.end(), nodeId) !=
               matchingNodeIds.end();
    }

    bool hasTopLevelMatch(const std::string& topLevelId) const {
        return std::find(matchingTopLevelNodeIds.begin(), matchingTopLevelNodeIds.end(),
                         topLevelId) != matchingTopLevelNodeIds.end();
    }
};

struct WorkspaceState {
    ViewportTransform viewport;
    DragSnapState dragSnap;
    InkingState inking;
    ConnectorState connector;
    AnimationState animation;
    WorkspaceSearchState search;

    bool isPanning = false;
    bool isSpacePressed = false;
    bool isMinimapDragging = false;
    double lastMouseX = 0.0;
    double lastMouseY = 0.0;

    std::optional<std::string> selectedEdgeId;
    std::optional<std::string> selectedNodeId;

    bool isDropHovering = false;
    double dropHoverScreenX = 0.0;
    double dropHoverScreenY = 0.0;

    bool showMinimap = true;
    double minimapWidth = 200.0;
    double minimapHeight = 140.0;
    double minimapMargin = 16.0;

    std::string hoveredAnchorCardId;
};

} // namespace FluidCoreApp
