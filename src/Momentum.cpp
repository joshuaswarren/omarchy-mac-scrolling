#include "Momentum.hpp"
#include "globals.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/managers/SeatManager.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>

// EMA time constant for velocity estimation while fingers are down. ~30ms
// spans a few touchpad frames: enough smoothing to eat sensor jitter, short
// enough that the launch velocity is what the fingers were doing at the end.
static constexpr double TRACK_TAU_MS = 30.0;

// If the last movement was longer ago than this at lift time, the fingers
// were resting: lifting them must not fling (macOS behaves the same).
static constexpr uint32_t STALE_LIFT_MS = 120;

// Safety net: if the scroll-end event never arrives (quirky backend), drop
// tracking state instead of flinging on stale data later.
static constexpr int TRACK_TIMEOUT_MS = 300;

static double nowMs() {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void dbg(const std::string& msg) {
    if (!cfgInt("debug", 0))
        return;

    Log::logger->log(Hyprutils::CLI::LOG_DEBUG, "[momentum] {}", msg);

    if (cfgInt("debug", 0) >= 2)
        HyprlandAPI::addNotification(PHANDLE, "[momentum] " + msg, CHyprColor{0.4, 0.7, 1.0, 1.0}, 1500);
}

static std::string lowered(std::string s) {
    for (auto& c : s)
        c = std::tolower(static_cast<unsigned char>(c));
    return s;
}

static bool looksLikeBrowser(const std::string& loweredClass) {
    // Substrings cover the families; "zen" is exact-match only so it can't
    // swallow unrelated classes like zenity.
    static const char* NEEDLES[] = {"chrom", "firefox", "brave", "vivaldi", "opera", "librewolf", "edge", "helium"};
    for (const auto* n : NEEDLES) {
        if (loweredClass.find(n) != std::string::npos)
            return true;
    }
    return loweredClass == "zen";
}

static bool inClassList(const std::string& loweredClass, std::string list) {
    for (auto& c : list) {
        if (c == ',')
            c = ' ';
        else
            c = std::tolower(static_cast<unsigned char>(c));
    }

    std::istringstream iss(list);
    for (std::string token; iss >> token;) {
        if (token == loweredClass)
            return true;
    }
    return false;
}

CMomentum::CMomentum() {
    auto* loop     = g_pCompositor->m_wlEventLoop;
    m_trackTimeout = wl_event_loop_add_timer(loop, trackTimeoutCb, this);
    m_glideTimer   = wl_event_loop_add_timer(loop, glideTickCb, this);
}

CMomentum::~CMomentum() {
    if (m_trackTimeout)
        wl_event_source_remove(m_trackTimeout);
    if (m_glideTimer)
        wl_event_source_remove(m_glideTimer);
}

void CMomentum::setAppRule(const std::string& cls, bool enabled) {
    m_appRules[lowered(cls)] = enabled;
}

void CMomentum::resetAppRules() {
    m_appRules.clear();
}

void CMomentum::stop(const char* reason) {
    if (m_phase != Phase::IDLE)
        dbg(std::string{"stop: "} + reason);

    m_phase     = Phase::IDLE;
    m_emaV      = m_emaH = 0.0;
    m_bucketV   = m_bucketH = 0.0;
    m_hasBucket = false;
    m_bucketTimeMs = m_prevFrameMs = m_lastMoveMs = 0;
    m_glideV = m_glideH = 0.0;

    wl_event_source_timer_update(m_trackTimeout, 0);
    wl_event_source_timer_update(m_glideTimer, 0);
}

void CMomentum::onAxis(const IPointer::SAxisEvent& e) {
    // Touchpads only. Wheel and continuous sources are excluded on purpose:
    // momentum is a trackpad gesture (and our own synthetic events bypass the
    // event bus entirely, so there is no feedback loop to guard against). A
    // real scroll from another device still catches a running glide.
    if (e.source != WL_POINTER_AXIS_SOURCE_FINGER) {
        if (m_phase == Phase::GLIDE)
            stop("caught by other device");
        return;
    }

    if (!cfgInt("enabled", 1)) {
        if (m_phase != Phase::IDLE)
            stop("disabled");
        return;
    }

    if (e.delta == 0.0) {
        // libinput scroll-end: the lift. One arrives per axis; the first one
        // decides, the rest are no-ops.
        if (m_phase == Phase::TRACK)
            maybeLaunch(e.timeMs);
        return;
    }

    // Fingers moving again while gliding: the catch. Fall through into fresh
    // tracking so a scroll-during-glide seamlessly starts the next gesture.
    if (m_phase == Phase::GLIDE)
        stop("caught by scroll");

    if (m_phase == Phase::IDLE)
        beginTrack(e);

    // Deltas for both axes of one touchpad frame share a timestamp. Bucket
    // them, and integrate the bucket once the timestamp advances.
    if (m_hasBucket && e.timeMs != m_bucketTimeMs)
        flushFrame();

    if (!m_hasBucket) {
        m_hasBucket    = true;
        m_bucketTimeMs = e.timeMs;
        m_bucketV = m_bucketH = 0.0;
    }

    if (e.axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        m_bucketV += e.delta;
    else
        m_bucketH += e.delta;

    m_lastMoveMs = e.timeMs;
    wl_event_source_timer_update(m_trackTimeout, TRACK_TIMEOUT_MS);
}

void CMomentum::beginTrack(const IPointer::SAxisEvent& e) {
    m_phase     = Phase::TRACK;
    m_emaV      = m_emaH = 0.0;
    m_hasBucket = false;
    m_prevFrameMs = 0;
    m_lastMoveMs  = e.timeMs;
}

void CMomentum::flushFrame() {
    if (!m_hasBucket)
        return;

    if (m_prevFrameMs == 0) {
        // First frame of the gesture: no dt yet, so no velocity sample.
        m_prevFrameMs = m_bucketTimeMs;
        m_hasBucket   = false;
        return;
    }

    const double dt = std::clamp<double>(m_bucketTimeMs - m_prevFrameMs, 1.0, 100.0);
    const double instV = m_bucketV / dt;
    const double instH = m_bucketH / dt;

    const bool   freshGesture = (m_bucketTimeMs - m_prevFrameMs) > 100;
    const double alpha        = 1.0 - std::exp(-dt / TRACK_TAU_MS);

    // A direction flip is a deliberate hand movement: track it immediately
    // instead of averaging through zero.
    if (freshGesture || (instV != 0.0 && instV * m_emaV < 0.0))
        m_emaV = instV;
    else
        m_emaV += alpha * (instV - m_emaV);

    if (freshGesture || (instH != 0.0 && instH * m_emaH < 0.0))
        m_emaH = instH;
    else
        m_emaH += alpha * (instH - m_emaH);

    m_prevFrameMs = m_bucketTimeMs;
    m_hasBucket   = false;
}

void CMomentum::maybeLaunch(uint32_t liftTimeMs) {
    flushFrame();

    if (liftTimeMs - m_lastMoveMs > STALE_LIFT_MS) {
        stop("stale lift");
        return;
    }

    const double boost = cfgFloat("boost", 1.0);
    double       v     = m_emaV * boost;
    double       h     = m_emaH * boost;

    const double maxV = cfgFloat("max_velocity", 8000.0) / 1000.0; // px/s -> px/ms
    v                 = std::clamp(v, -maxV, maxV);
    h                 = std::clamp(h, -maxV, maxV);

    const double speed = std::max(std::abs(v), std::abs(h)) * 1000.0; // px/s
    if (speed < cfgFloat("fling_threshold", 80.0)) {
        stop("below fling threshold");
        return;
    }

    if (!allowedForFocusedWindow()) {
        stop("app rule");
        return;
    }

    m_glideV      = v;
    m_glideH      = h;
    m_glideFactor = effectiveScrollFactor();
    m_intervalMs  = tickIntervalMs();
    m_lastTickMs  = nowMs();
    m_phase       = Phase::GLIDE;

    dbg("glide: v=" + std::to_string(v * 1000.0) + " h=" + std::to_string(h * 1000.0) + " px/s, factor=" + std::to_string(m_glideFactor) +
        ", tick=" + std::to_string(m_intervalMs) + "ms");

    wl_event_source_timer_update(m_trackTimeout, 0);
    wl_event_source_timer_update(m_glideTimer, m_intervalMs);
}

void CMomentum::glideTick() {
    if (m_phase != Phase::GLIDE)
        return;

    if (!cfgInt("enabled", 1)) {
        stop("disabled");
        return;
    }

    const double now = nowMs();
    const double dt  = std::clamp(now - m_lastTickMs, 1.0, 50.0);
    m_lastTickMs     = now;

    // Time-based exponential decay: v(t) = v0 * decay^t_ms. The default 0.998
    // is Apple's UIScrollViewDecelerationRateNormal, which is also what a
    // trackpad fling on macOS feels like.
    const double retain = std::pow(std::clamp(cfgFloat("decay", 0.998), 0.5, 0.99999), dt);
    m_glideV *= retain;
    m_glideH *= retain;

    const double minSpeed = cfgFloat("min_velocity", 15.0); // px/s
    if (std::abs(m_glideV) * 1000.0 < minSpeed && std::abs(m_glideH) * 1000.0 < minSpeed) {
        stop("glide finished");
        return;
    }

    const auto timeMs = static_cast<uint32_t>(now);
    bool       sent   = false;

    const double emitV = m_glideV * dt * m_glideFactor;
    const double emitH = m_glideH * dt * m_glideFactor;

    if (std::abs(emitV) > 1e-4) {
        g_pSeatManager->sendPointerAxis(timeMs, WL_POINTER_AXIS_VERTICAL_SCROLL, emitV, 0, 0, WL_POINTER_AXIS_SOURCE_CONTINUOUS,
                                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
        sent = true;
    }

    if (std::abs(emitH) > 1e-4) {
        g_pSeatManager->sendPointerAxis(timeMs, WL_POINTER_AXIS_HORIZONTAL_SCROLL, emitH, 0, 0, WL_POINTER_AXIS_SOURCE_CONTINUOUS,
                                        WL_POINTER_AXIS_RELATIVE_DIRECTION_IDENTICAL);
        sent = true;
    }

    if (sent)
        g_pSeatManager->sendPointerFrame();

    wl_event_source_timer_update(m_glideTimer, m_intervalMs);
}

void CMomentum::onTouchpadContact() {
    // Any new touch on the pad catches the scroll, exactly like macOS: a
    // cursor-moving finger or a resting hold both stop the glide.
    if (m_phase == Phase::GLIDE)
        stop("caught by touch");
}

void CMomentum::onButtonPress() {
    if (m_phase == Phase::GLIDE)
        stop("button press");
}

void CMomentum::onPointerFocusChange() {
    // The glide targets whatever surface has pointer focus; if that changes,
    // continuing would dump leftover scroll into a window that never saw the
    // gesture. Stop instead.
    if (m_phase == Phase::GLIDE)
        stop("focus change");
}

bool CMomentum::allowedForFocusedWindow() const {
    const auto PWIN = g_pInputManager ? g_pInputManager->m_lastMouseFocus.lock() : nullptr;
    if (!PWIN)
        return true; // layer surfaces (bars, panels) glide fine

    const auto cls = lowered(PWIN->m_class);

    if (const auto it = m_appRules.find(cls); it != m_appRules.end())
        return it->second;

    if (inClassList(cls, cfgString("disabled_classes", "")))
        return false;

    // Browsers ship their own fling (Blink's fling controller etc.); gliding
    // on top of that double-scrolls.
    if (cfgInt("disable_in_browser", 1) && looksLikeBrowser(cls))
        return false;

    return true;
}

double CMomentum::effectiveScrollFactor() const {
    // The event bus hands us deltas *before* Hyprland applies scroll factors,
    // so the glide has to apply the same factor the tracked scroll got:
    // input:touchpad:scroll_factor, overridden per-window by the
    // scroll_touchpad window rule.
    static auto PTOUCHPADFACTOR = CConfigValue<Config::FLOAT>("input:touchpad:scroll_factor");

    double      factor = *PTOUCHPADFACTOR;

    const auto  PWIN = g_pInputManager ? g_pInputManager->m_lastMouseFocus.lock() : nullptr;
    if (PWIN && PWIN->isScrollTouchpadOverridden())
        factor = PWIN->getScrollTouchpad();

    return factor > 0.0 ? factor : 1.0;
}

int CMomentum::tickIntervalMs() const {
    // Emit at the refresh rate of the monitor being scrolled, so the glide is
    // as smooth as the panel allows (a fixed 16ms timer on a 120Hz panel
    // halves the visible frame rate).
    double hz = 120.0;

    if (const auto PWIN = g_pInputManager ? g_pInputManager->m_lastMouseFocus.lock() : nullptr) {
        if (const auto PMON = PWIN->m_monitor.lock(); PMON && PMON->m_refreshRate > 0.0f)
            hz = PMON->m_refreshRate;
    }

    return std::clamp(static_cast<int>(std::lround(1000.0 / hz)), 3, 16);
}

int CMomentum::trackTimeoutCb(void* data) {
    auto* self = static_cast<CMomentum*>(data);
    if (self->m_phase == Phase::TRACK)
        self->stop("track timeout");
    return 0;
}

int CMomentum::glideTickCb(void* data) {
    static_cast<CMomentum*>(data)->glideTick();
    return 0;
}
