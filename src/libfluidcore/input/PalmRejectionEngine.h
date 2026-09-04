#pragma once

#include <cmath>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace FluidCore {

enum class InputDeviceClass { Unknown, Pen, Eraser, Touch, Mouse };

enum class StylusHardwareProfile { Generic, Wacom, HpMpp, Surface };

enum class InputDecision { Accept, RejectAsPalm, CancelPriorTouch };

struct PenEventResult {
    bool accepted = true;
    bool isEraser = false;
    bool isDuplicateBounce = false;
    std::vector<uint32_t> cancelledTouchIds;
};

struct PalmRejectionConfig {
    StylusHardwareProfile profile = StylusHardwareProfile::Generic;
    bool enabled = true;
    bool suppressTouchOnHover = true;
    uint64_t hoverCooldownMs = 300;
    uint64_t strokeCooldownMs = 400;
    uint64_t retroactiveCancelWindowMs = 100;
    double palmRejectionRadiusPx = 200.0;
    uint64_t penBounceDebounceMs = 25;
    double penBounceRadiusPx = 3.0;

    static PalmRejectionConfig wacomDefaults();
    static PalmRejectionConfig hpMppDefaults();
    static PalmRejectionConfig surfaceDefaults();
    static PalmRejectionConfig genericDefaults();
};

class PalmRejectionEngine {
  public:
    explicit PalmRejectionEngine(
        PalmRejectionConfig config = PalmRejectionConfig::genericDefaults());
    ~PalmRejectionEngine() = default;

    // Hardware profile detection via Vendor ID / Product ID primary with device name fallback
    static StylusHardwareProfile detectProfile(const std::string& vendorId,
                                               const std::string& productId,
                                               const std::string& deviceName);

    void setConfig(const PalmRejectionConfig& config);
    const PalmRejectionConfig& config() const { return m_config; }

    void setProfileOverride(StylusHardwareProfile profile);
    StylusHardwareProfile currentProfile() const { return m_config.profile; }

    bool isEnabled() const { return m_config.enabled; }
    void setEnabled(bool enabled) { m_config.enabled = enabled; }

    // Proximity tracking
    void onPenProximity(bool inRange, uint64_t timestampMs, const std::string& deviceId = "");

    // Pen events
    PenEventResult onPenDown(double x, double y, double pressure, uint64_t timestampMs,
                             InputDeviceClass device = InputDeviceClass::Pen,
                             const std::string& deviceId = "");

    PenEventResult onPenMotion(double x, double y, double pressure, uint64_t timestampMs,
                               const std::string& deviceId = "");

    PenEventResult onPenUp(double x, double y, uint64_t timestampMs,
                           const std::string& deviceId = "");

    // Touch events
    InputDecision onTouchDown(uint32_t touchId, double x, double y, uint64_t timestampMs,
                              const std::string& deviceId = "");

    InputDecision onTouchMotion(uint32_t touchId, double x, double y, uint64_t timestampMs,
                                const std::string& deviceId = "");

    InputDecision onTouchUp(uint32_t touchId, double x, double y, uint64_t timestampMs,
                            const std::string& deviceId = "");

    // Mouse events
    InputDecision onMouseDown(double x, double y, uint64_t timestampMs,
                              const std::string& deviceId = "");

    // State queries
    bool isPenInProximity() const { return m_isPenInProximity; }
    bool isPenInking() const { return m_isPenInking; }
    bool isEraserActive() const { return m_isEraser; }
    bool isTouchCancelled(uint32_t touchId) const;

    void reset();

  private:
    struct TouchRecord {
        uint32_t id = 0;
        double x = 0.0;
        double y = 0.0;
        uint64_t timestamp = 0;
        bool cancelled = false;
    };

    bool isWithinPalmRadius(double x, double y) const;
    bool isCooldownActive(uint64_t timestampMs) const;

    PalmRejectionConfig m_config;

    bool m_isPenInProximity = false;
    bool m_isPenInking = false;
    bool m_isEraser = false;

    double m_lastPenX = 0.0;
    double m_lastPenY = 0.0;
    double m_lastPenDownX = -999999.0;
    double m_lastPenDownY = -999999.0;
    uint64_t m_lastPenDownTime = 0;
    uint64_t m_lastPenTime = 0;
    uint64_t m_lastPenLiftTime = 0;
    uint64_t m_lastHoverExitTime = 0;

    std::unordered_map<uint32_t, TouchRecord> m_activeTouches;
};

} // namespace FluidCore
