#pragma once
#include <hyprland/src/devices/IPointer.hpp>
#include <wayland-server-core.h>
#include <cstdint>
#include <string>
#include <unordered_map>

// macOS-style momentum scrolling.
//
// Three phases:
//   IDLE  -> nothing happening.
//   TRACK -> fingers on the pad, scrolling. We estimate velocity (px/ms, raw
//            libinput units) with a time-constant EMA over per-frame deltas.
//   GLIDE -> fingers lifted with speed: we synthesize scroll events that decay
//            exponentially with *time* (v *= decay^dt_ms), so the feel is
//            independent of both touchpad report rate and timer cadence.
//
// The lift is not guessed with timers: libinput emits a scroll-end event
// (source FINGER, delta 0) which Hyprland forwards through the event bus
// before applying scroll_factor. That event is the launch trigger, exactly
// like the "gesture ended" phase macOS keys off.
class CMomentum {
  public:
    CMomentum();
    ~CMomentum();

    CMomentum(const CMomentum&)            = delete;
    CMomentum& operator=(const CMomentum&) = delete;

    void onAxis(const IPointer::SAxisEvent& e);
    void onTouchpadContact(); // any new touch during a glide is a "catch"
    void onButtonPress();
    void onPointerFocusChange();
    void stop(const char* reason);

    // Per-app rules, exposed as hl.plugin.momentum.enable/disable(class).
    void setAppRule(const std::string& cls, bool enabled);
    void resetAppRules();

  private:
    enum class Phase : uint8_t {
        IDLE,
        TRACK,
        GLIDE,
    };

    static int trackTimeoutCb(void* data);
    static int glideTickCb(void* data);

    void       beginTrack(const IPointer::SAxisEvent& e);
    void       flushFrame();
    void       maybeLaunch(uint32_t liftTimeMs);
    void       glideTick();
    bool       allowedForFocusedWindow() const;
    double     effectiveScrollFactor() const;
    int        tickIntervalMs() const;

    Phase      m_phase = Phase::IDLE;

    // ---- tracking state (velocities in px/ms, pre-scroll_factor units) ----
    double     m_emaV = 0.0, m_emaH = 0.0;
    double     m_bucketV = 0.0, m_bucketH = 0.0; // deltas of the in-flight frame
    bool       m_hasBucket    = false;
    uint32_t   m_bucketTimeMs = 0; // timestamp of bucketed deltas
    uint32_t   m_prevFrameMs  = 0; // timestamp of last flushed frame
    uint32_t   m_lastMoveMs   = 0; // last nonzero delta (stale-lift check)

    // ---- glide state ----
    double           m_glideV = 0.0, m_glideH = 0.0; // px/ms, decaying
    double           m_glideFactor = 1.0;            // scroll factor captured at launch
    double           m_lastTickMs  = 0.0;            // steady-clock ms
    int              m_intervalMs  = 8;

    wl_event_source* m_trackTimeout = nullptr;
    wl_event_source* m_glideTimer   = nullptr;

    std::unordered_map<std::string, bool> m_appRules;
};
