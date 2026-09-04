#include "input/PalmRejectionEngine.h"

#include <algorithm>
#include <cctype>

namespace FluidCore {

namespace {

std::string toLower(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return str;
}

} // namespace

PalmRejectionConfig PalmRejectionConfig::wacomDefaults() {
    PalmRejectionConfig cfg;
    cfg.profile = StylusHardwareProfile::Wacom;
    cfg.enabled = true;
    cfg.suppressTouchOnHover = true;
    cfg.hoverCooldownMs = 200;
    cfg.strokeCooldownMs = 300;
    cfg.retroactiveCancelWindowMs = 60;
    cfg.palmRejectionRadiusPx = 180.0;
    cfg.penBounceDebounceMs = 20;
    cfg.penBounceRadiusPx = 2.0;
    return cfg;
}

PalmRejectionConfig PalmRejectionConfig::hpMppDefaults() {
    PalmRejectionConfig cfg;
    cfg.profile = StylusHardwareProfile::HpMpp;
    cfg.enabled = true;
    cfg.suppressTouchOnHover = true;
    cfg.hoverCooldownMs = 350;
    cfg.strokeCooldownMs = 400;
    cfg.retroactiveCancelWindowMs = 100;
    cfg.palmRejectionRadiusPx = 200.0;
    cfg.penBounceDebounceMs = 35;
    cfg.penBounceRadiusPx = 3.5;
    return cfg;
}

PalmRejectionConfig PalmRejectionConfig::surfaceDefaults() {
    PalmRejectionConfig cfg;
    cfg.profile = StylusHardwareProfile::Surface;
    cfg.enabled = true;
    cfg.suppressTouchOnHover = true;
    cfg.hoverCooldownMs = 400;
    cfg.strokeCooldownMs = 450;
    cfg.retroactiveCancelWindowMs = 120;
    cfg.palmRejectionRadiusPx = 220.0;
    cfg.penBounceDebounceMs = 30;
    cfg.penBounceRadiusPx = 3.0;
    return cfg;
}

PalmRejectionConfig PalmRejectionConfig::genericDefaults() {
    PalmRejectionConfig cfg;
    cfg.profile = StylusHardwareProfile::Generic;
    cfg.enabled = true;
    cfg.suppressTouchOnHover = true;
    cfg.hoverCooldownMs = 300;
    cfg.strokeCooldownMs = 400;
    cfg.retroactiveCancelWindowMs = 80;
    cfg.palmRejectionRadiusPx = 200.0;
    cfg.penBounceDebounceMs = 25;
    cfg.penBounceRadiusPx = 3.0;
    return cfg;
}

PalmRejectionEngine::PalmRejectionEngine(PalmRejectionConfig config) : m_config(config) {}

StylusHardwareProfile PalmRejectionEngine::detectProfile(const std::string& vendorId,
                                                         const std::string& /*productId*/,
                                                         const std::string& deviceName) {
    const std::string vid = toLower(vendorId);
    const std::string name = toLower(deviceName);

    // 1. Primary: USB Vendor ID lookup
    if (vid == "056a" || vid == "2d1f") {
        return StylusHardwareProfile::Wacom;
    }
    if (vid == "045e") {
        return StylusHardwareProfile::Surface;
    }
    if (vid == "03f0") {
        return StylusHardwareProfile::HpMpp;
    }

    // 2. Secondary fallback: Device name substrings
    if (name.find("wacom") != std::string::npos || name.find("cintiq") != std::string::npos ||
        name.find("intuos") != std::string::npos) {
        return StylusHardwareProfile::Wacom;
    }

    if (name.find("surface") != std::string::npos || name.find("ipts") != std::string::npos ||
        name.find("n-trig") != std::string::npos) {
        return StylusHardwareProfile::Surface;
    }

    const bool hasPenKeyword =
        (name.find("pen") != std::string::npos || name.find("stylus") != std::string::npos);
    if (name.find("mpp") != std::string::npos ||
        (name.find("hp") != std::string::npos && hasPenKeyword) ||
        (name.find("elan") != std::string::npos && hasPenKeyword)) {
        return StylusHardwareProfile::HpMpp;
    }

    return StylusHardwareProfile::Generic;
}

void PalmRejectionEngine::setConfig(const PalmRejectionConfig& config) {
    m_config = config;
}

void PalmRejectionEngine::setProfileOverride(StylusHardwareProfile profile) {
    switch (profile) {
    case StylusHardwareProfile::Wacom:
        m_config = PalmRejectionConfig::wacomDefaults();
        break;
    case StylusHardwareProfile::HpMpp:
        m_config = PalmRejectionConfig::hpMppDefaults();
        break;
    case StylusHardwareProfile::Surface:
        m_config = PalmRejectionConfig::surfaceDefaults();
        break;
    case StylusHardwareProfile::Generic:
    default:
        m_config = PalmRejectionConfig::genericDefaults();
        break;
    }
}

void PalmRejectionEngine::onPenProximity(bool inRange, uint64_t timestampMs,
                                         const std::string& /*deviceId*/) {
    m_isPenInProximity = inRange;
    if (!inRange) {
        m_lastHoverExitTime = timestampMs;
    } else {
        m_lastPenTime = timestampMs;
    }
}

PenEventResult PalmRejectionEngine::onPenDown(double x, double y, double /*pressure*/,
                                              uint64_t timestampMs, InputDeviceClass device,
                                              const std::string& /*deviceId*/) {
    if (!m_config.enabled) {
        return PenEventResult{.accepted = true,
                              .isEraser = (device == InputDeviceClass::Eraser),
                              .isDuplicateBounce = false,
                              .cancelledTouchIds = {}};
    }

    // Pen contact bounce deduplication
    if (m_lastPenDownTime > 0 && (timestampMs >= m_lastPenDownTime) &&
        (timestampMs - m_lastPenDownTime <= m_config.penBounceDebounceMs) &&
        std::hypot(x - m_lastPenDownX, y - m_lastPenDownY) <= m_config.penBounceRadiusPx) {
        return PenEventResult{.accepted = false,
                              .isEraser = m_isEraser,
                              .isDuplicateBounce = true,
                              .cancelledTouchIds = {}};
    }

    m_lastPenDownX = x;
    m_lastPenDownY = y;
    m_lastPenDownTime = timestampMs;

    m_isPenInking = true;
    m_isPenInProximity = true;
    m_isEraser = (device == InputDeviceClass::Eraser);
    m_lastPenX = x;
    m_lastPenY = y;
    m_lastPenTime = timestampMs;

    std::vector<uint32_t> cancelledTouchIds;

    // Retroactive palm cancellation: check touches that landed within pre-contact window
    for (auto& [id, record] : m_activeTouches) {
        if (!record.cancelled && timestampMs >= record.timestamp) {
            const uint64_t dt = timestampMs - record.timestamp;
            if (dt <= m_config.retroactiveCancelWindowMs ||
                isWithinPalmRadius(record.x, record.y)) {
                record.cancelled = true;
                cancelledTouchIds.push_back(id);
            }
        }
    }

    return PenEventResult{.accepted = true,
                          .isEraser = m_isEraser,
                          .isDuplicateBounce = false,
                          .cancelledTouchIds = std::move(cancelledTouchIds)};
}

PenEventResult PalmRejectionEngine::onPenMotion(double x, double y, double /*pressure*/,
                                                uint64_t timestampMs,
                                                const std::string& /*deviceId*/) {
    m_lastPenX = x;
    m_lastPenY = y;
    m_lastPenTime = timestampMs;
    return PenEventResult{.accepted = true,
                          .isEraser = m_isEraser,
                          .isDuplicateBounce = false,
                          .cancelledTouchIds = {}};
}

PenEventResult PalmRejectionEngine::onPenUp(double x, double y, uint64_t timestampMs,
                                            const std::string& /*deviceId*/) {
    m_isPenInking = false;
    m_lastPenLiftTime = timestampMs;
    m_lastPenX = x;
    m_lastPenY = y;
    m_lastPenTime = timestampMs;
    return PenEventResult{.accepted = true,
                          .isEraser = m_isEraser,
                          .isDuplicateBounce = false,
                          .cancelledTouchIds = {}};
}

bool PalmRejectionEngine::isWithinPalmRadius(double x, double y) const {
    if (m_config.palmRejectionRadiusPx <= 0.0) {
        return false;
    }
    return std::hypot(x - m_lastPenX, y - m_lastPenY) <= m_config.palmRejectionRadiusPx;
}

bool PalmRejectionEngine::isCooldownActive(uint64_t timestampMs) const {
    if (m_isPenInking) {
        return true;
    }
    if (m_isPenInProximity && m_config.suppressTouchOnHover) {
        return true;
    }
    if (m_lastPenLiftTime > 0 && timestampMs >= m_lastPenLiftTime &&
        (timestampMs - m_lastPenLiftTime <= m_config.strokeCooldownMs)) {
        return true;
    }
    if (m_lastHoverExitTime > 0 && timestampMs >= m_lastHoverExitTime &&
        (timestampMs - m_lastHoverExitTime <= m_config.hoverCooldownMs)) {
        return true;
    }
    return false;
}

InputDecision PalmRejectionEngine::onTouchDown(uint32_t touchId, double x, double y,
                                               uint64_t timestampMs,
                                               const std::string& /*deviceId*/) {
    if (!m_config.enabled) {
        return InputDecision::Accept;
    }

    if (isCooldownActive(timestampMs)) {
        m_activeTouches[touchId] = TouchRecord{touchId, x, y, timestampMs, true};
        return InputDecision::RejectAsPalm;
    }

    if ((m_isPenInProximity || m_isPenInking) && isWithinPalmRadius(x, y)) {
        m_activeTouches[touchId] = TouchRecord{touchId, x, y, timestampMs, true};
        return InputDecision::RejectAsPalm;
    }

    m_activeTouches[touchId] = TouchRecord{touchId, x, y, timestampMs, false};
    return InputDecision::Accept;
}

InputDecision PalmRejectionEngine::onTouchMotion(uint32_t touchId, double x, double y,
                                                 uint64_t timestampMs,
                                                 const std::string& /*deviceId*/) {
    if (!m_config.enabled) {
        return InputDecision::Accept;
    }

    auto it = m_activeTouches.find(touchId);
    if (it != m_activeTouches.end() && it->second.cancelled) {
        return InputDecision::RejectAsPalm;
    }

    // Dynamic spatial radius re-evaluation on motion:
    // A resting palm must not outrun rejection when the pen strokes across the screen.
    if (isCooldownActive(timestampMs) ||
        ((m_isPenInProximity || m_isPenInking) && isWithinPalmRadius(x, y))) {
        if (it != m_activeTouches.end()) {
            it->second.cancelled = true;
        } else {
            m_activeTouches[touchId] = TouchRecord{touchId, x, y, timestampMs, true};
        }
        return InputDecision::RejectAsPalm;
    }

    if (it != m_activeTouches.end()) {
        it->second.x = x;
        it->second.y = y;
    }

    return InputDecision::Accept;
}

InputDecision PalmRejectionEngine::onTouchUp(uint32_t touchId, double /*x*/, double /*y*/,
                                             uint64_t timestampMs,
                                             const std::string& /*deviceId*/) {
    if (!m_config.enabled) {
        return InputDecision::Accept;
    }

    bool wasCancelled = false;
    auto it = m_activeTouches.find(touchId);
    if (it != m_activeTouches.end()) {
        wasCancelled = it->second.cancelled;
        m_activeTouches.erase(it);
    }

    if (wasCancelled || isCooldownActive(timestampMs)) {
        return InputDecision::RejectAsPalm;
    }

    return InputDecision::Accept;
}

InputDecision PalmRejectionEngine::onMouseDown(double /*x*/, double /*y*/, uint64_t /*timestampMs*/,
                                               const std::string& /*deviceId*/) {
    if (!m_config.enabled) {
        return InputDecision::Accept;
    }
    // Mouse is accepted unless pen is inking and mouse is synthetic touch
    return InputDecision::Accept;
}

bool PalmRejectionEngine::isTouchCancelled(uint32_t touchId) const {
    auto it = m_activeTouches.find(touchId);
    if (it != m_activeTouches.end()) {
        return it->second.cancelled;
    }
    return false;
}

void PalmRejectionEngine::reset() {
    m_isPenInProximity = false;
    m_isPenInking = false;
    m_isEraser = false;
    m_lastPenDownX = -999999.0;
    m_lastPenDownY = -999999.0;
    m_lastPenDownTime = 0;
    m_lastPenTime = 0;
    m_lastPenLiftTime = 0;
    m_lastHoverExitTime = 0;
    m_activeTouches.clear();
}

} // namespace FluidCore
