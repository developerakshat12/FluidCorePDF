# Technical Specification: Palm Rejection Tuning & Stylus Matrix (FR-6.5)

## 1. Overview
The stylus matrix and palm rejection subsystem provides responsive, low-latency inking on modern digitizers while eliminating accidental palm inputs and touch interference across primary hardware platforms (Wacom EMR/AES, Microsoft Surface Pen, HP MPP, and Generic digitizers).

In addition, it provides full per-point pressure persistence in `.ltproj` SQLite storage (`pressures_blob`) and dynamic variable-width stroke rendering matching Xournal++ ergonomics.

---

## 2. Hardware Profiles & Detection

Hardware detection examines USB Vendor ID (VID) and Product ID (PID), falling back to device name inspection:

| Profile | Primary VID / PID | Name Substrings | Hover Distance | Retroactive Window | Debounce Window | Touch Guard Radius |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Wacom EMR/AES** | `056a`, `2d1f` | `"wacom"`, `"intuos"` | 15.0 mm | 30 ms | 15 ms | 45.0 px |
| **Microsoft Surface** | `045e` | `"surface"`, `"microsoft"` | 10.0 mm | 65 ms | 20 ms | 60.0 px |
| **HP MPP** | `03f0` | `"mpp"`, `"elan"`, `"synaptics"` | 8.0 mm | 80 ms | 25 ms | 70.0 px |
| **Generic** | Default | Any other | 10.0 mm | 50 ms | 20 ms | 50.0 px |

---

## 3. Pure C++20 Arbitration Architecture (`libfluidcore/input`)

`PalmRejectionEngine` manages digitizer input independently of any GUI framework (zero GTK/Cairo/X11 dependencies):

### 3.1 Data Structures
```cpp
enum class InputDeviceClass { Pen, Eraser, Touch, Mouse };
enum class InputDecision { Accept, RejectAsPalm, CancelPriorTouch };

struct PenEventResult {
    InputDecision decision;
    bool isDuplicateBounce;
    std::vector<uint32_t> cancelledTouchIds;
};
```

### 3.2 State Machine
1. **Proximity Tracking**: `onPenProximity(inRange, timestampMs)`. When pen is hovering within range, incoming touch events are rejected as palm touches.
2. **Pen Contact & Debounce**: If pen contacts down within `contactDebounceWindowMs` of the prior release, it is flagged as a duplicate bounce (`isDuplicateBounce = true`), preventing accidental stroke fragment generation.
3. **Retroactive Touch Cancellation**: On `onPenDown`, any active touch contacts that began within `retroactiveTouchCancelWindowMs` of the pen contact are cancelled (`cancelledTouchIds`). Viewports immediately abort active gestures (pans, drags) started by those touches.
4. **Touch & Mouse Arbitration**: Touch events occurring within `touchGuardRadiusPx` of the pen position or while the pen is in proximity/down are rejected (`RejectAsPalm`).

---

## 4. Vector Pressure Storage (`ProjectStore.cpp`)

Vector strokes persist per-point pressure values in the SQLite `ink_strokes` table:

```sql
ALTER TABLE ink_strokes ADD COLUMN pressures_blob BLOB DEFAULT NULL;
```

- **Encoding**: IEEE 754 double precision little-endian (8 bytes per point).
- **Cross-Validation**: During rehydration, the engine validates that `pressures_blob.size() == 8 * N`, where `N = points_blob.size() / 16`. If byte sizes do not align, the stroke falls back to `base_width` for all points to avoid data misalignment.
- **NULL Handling**: If `pressures_blob` is NULL or empty, the stroke rehydrates using constant `base_width`.

---

## 5. Variable Width Stroke Rendering

Both document ink (`InkOverlay.cpp`) and workspace canvas strokes (`WorkspaceRenderer.cpp`) use a unified linear pressure response:

$$w_i = \text{base\_width} \cdot (0.25 + 0.75 \cdot p_i)$$

where $p_i \in [0.0, 1.0]$ is the normalized stylus contact pressure.

---

## 6. Viewport Integration

1. `WorkspaceView` and `InkOverlay` classify GDK input devices into `InputDeviceClass`.
2. Connect to `proximity-in-event` and `proximity-out-event` to notify `onPenProximity`.
3. Process `onPenDown` results; when `cancelledTouchIds` are returned, viewports call `cancelActiveTouches()` to abort tentative drags and redraw immediately.
4. Top-level window (`main.cpp`) instantiates a shared `PalmRejectionEngine` and forwards it to both `DocumentPane` and `WorkspaceView`.
