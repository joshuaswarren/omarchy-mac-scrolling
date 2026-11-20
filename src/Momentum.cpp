#include "Momentum.hpp"
#include "globals.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/protocols/InputCapture.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include "hypr_window_compat.hpp"
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

static void sendGlideAxis(const SP<IPointer>& device, wl_pointer_axis axis, double delta, uint32_t timeMs) {
    // FINGER selects Hyprland's native touchpad and window scaling.
    g_pInputManager->onMouseWheel({.timeMs = timeMs, .source = WL_POINTER_AXIS_SOURCE_FINGER, .axis = axis, .delta = delta}, device);
}

static uint32_t axisTimeMs(double now) {
    return static_cast<uint32_t>(static_cast<uint64_t>(now));
}

static constexpr wl_pointer_axis AXES[] = {WL_POINTER_AXIS_VERTICAL_SCROLL, WL_POINTER_AXIS_HORIZONTAL_SCROLL};

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

void CMomentum::stop(const char* reason, bool endAxes) {
    if (m_phase != Phase::IDLE)
        dbg(std::string{"stop: "} + reason);

    const auto device = m_device.lock();
    const auto axes = m_glideAxes;
    m_device.reset();
    m_glideAxes = 0;
    m_phase     = Phase::IDLE;
    m_emaV      = m_emaH = 0.0;
    m_bucketV   = m_bucketH = 0.0;
    m_hasBucket = false;
    m_bucketTimeMs = m_prevFrameMs = m_lastMoveMs = 0;
    m_glideV = m_glideH = 0.0;

    wl_event_source_timer_update(m_trackTimeout, 0);
    wl_event_source_timer_update(m_glideTimer, 0);

    if (endAxes && axes && device && device->m_connected && !PROTO::inputCapture->isCaptured()) {
        const auto timeMs = axisTimeMs(nowMs());
        for (unsigned i = 0; i < 2; ++i)
            if (axes & (1U << i))
                sendGlideAxis(device, AXES[i], 0.0, timeMs);
        g_pInputManager->onPointerFrame();
    }
}

void CMomentum::onAxis(const IPointer::SAxisEvent& e, const SP<IPointer>& device) {
    if (e.source != WL_POINTER_AXIS_SOURCE_FINGER || !device || !device->m_connected)
        return;

    if (!cfgInt("enabled", 1)) {
        if (m_phase != Phase::IDLE)
            stop("disabled");
        return;
    }

    if (e.delta == 0.0) {
        // libinput scroll-end: the lift. One arrives per axis; the first one
        // decides, the rest are no-ops.
        if (m_phase == Phase::TRACK && m_device.lock() == device)
            maybeLaunch(e.timeMs);
        return;
    }

    // The new physical sequence owns its own axis stop.
    if (m_phase == Phase::GLIDE || (m_phase == Phase::TRACK && m_device.lock() != device))
        stop("caught by scroll", false);

    if (m_phase == Phase::IDLE) {
        m_device = device;
        beginTrack(e);
    }

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
    m_intervalMs  = tickIntervalMs();
    m_lastTickMs  = nowMs();
    m_phase       = Phase::GLIDE;

    dbg("glide: v=" + std::to_string(v * 1000.0) + " h=" + std::to_string(h * 1000.0) + " px/s, tick=" + std::to_string(m_intervalMs) + "ms");

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

    const auto device = m_device.lock();
    if (!device || !device->m_connected || PROTO::inputCapture->isCaptured()) {
        stop("device gone or input captured", false);
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

    const double deltas[] = {m_glideV * dt, m_glideH * dt};
    bool sent = false;
    for (unsigned i = 0; i < 2 && m_phase == Phase::GLIDE; ++i) {
        if (std::abs(deltas[i]) <= 1e-4)
            continue;
        m_glideAxes |= 1U << i;
        sendGlideAxis(device, AXES[i], deltas[i], axisTimeMs(now));
        sent = true;
    }
    if (sent)
        g_pInputManager->onPointerFrame();
    if (m_phase == Phase::GLIDE)
        wl_event_source_timer_update(m_glideTimer, m_intervalMs);
}

void CMomentum::onTouchpadContact(const char* reason) {
    if (m_phase == Phase::GLIDE)
        stop(reason);
}

void CMomentum::onButtonPress() {
    if (m_phase == Phase::GLIDE)
        stop("button press");
}

void CMomentum::onPointerFocusChange() {
    // The glide targets whatever surface has pointer focus; if that changes,
    // continuing would dump leftover scroll into a window that never saw the
    // gesture. Stop instead.
    if (m_phase != Phase::IDLE)
        stop("focus change", false);
}

bool CMomentum::allowedForFocusedWindow() const {
    const auto PWIN = g_pInputManager ? g_pInputManager->m_lastMouseFocus.lock() : nullptr;
    if (!PWIN)
        return true; // layer surfaces (bars, panels) glide fine

    const auto cls = lowered(momentumWindowClass(PWIN));

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
