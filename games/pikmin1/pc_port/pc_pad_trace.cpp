/**
 * @file pc_pad_trace.cpp
 * @brief Diagnostic log of the pad axis path. See pc_pad_trace.h.
 */

#include "pc_pad_trace.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

enum TraceMode { TRACE_OFF = 0, TRACE_ANOMALIES = 1, TRACE_VERBOSE = 2 };

/// Directory for pad_trace.log, set by the window layer. Empty means "current
/// working directory", which is the best guess when nothing set it.
char sTraceDir[512] = { 0 };

/// Raw magnitude that counts as "at the very end of travel".
constexpr int kRailMagnitude = 30000;
/// A pad value this large with a centred raw axis has no plausible cause.
constexpr int kFullMagnitude = 100;
/// A one-poll change this large cannot come from a thumb.
constexpr int kJumpMagnitude = 32;
/// Polls at the rail before it counts as stuck rather than a deliberate push.
constexpr int kStuckPolls = 60;
/// Anomaly lines are capped so a storm stays readable.
constexpr unsigned kLinesPerSecond = 12;

int traceMode() {
    static int mode = -1;
    if (mode < 0) {
        mode = TRACE_ANOMALIES;
        if (const char* env = std::getenv("PIKMIN_PAD_TRACE")) {
            if (*env) {
                const int value = std::atoi(env);
                mode = value <= 0 ? TRACE_OFF : value >= 2 ? TRACE_VERBOSE : TRACE_ANOMALIES;
            }
        }
    }
    return mode;
}

std::FILE* traceFile() {
    static std::FILE* file = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        char path[600];
        std::snprintf(path, sizeof(path), "%spad_trace.log", sTraceDir);
        file = std::fopen(path, "w");
        if (file) {
            std::fprintf(file, "# Pikmin pad trace: raw SDL axes, the pad value derived from them, and the device\n");
            std::fprintf(file, "# columns: time poll id raw=(Lx,Ly,Rx,Ry) pad=(X,Y,Cx,Cy) dz=N tags\n");
            std::printf("[pad] trace -> %s\n", path);
            std::fflush(stdout);
        }
    }
    return file;
}

double traceSeconds() {
    using clock = std::chrono::steady_clock;
    static const clock::time_point start = clock::now();
    return std::chrono::duration<double>(clock::now() - start).count();
}

bool traceAllowed() {
    using clock = std::chrono::steady_clock;
    static clock::time_point windowStart{};
    static unsigned inWindow = 0;
    const clock::time_point now = clock::now();
    if (windowStart.time_since_epoch().count() == 0 || now - windowStart > std::chrono::seconds(1)) {
        windowStart = now;
        inWindow    = 0;
    }
    if (inWindow >= kLinesPerSecond) {
        return false;
    }
    ++inWindow;
    return true;
}

void emit(const char* line, bool toConsole) {
    if (std::FILE* file = traceFile()) {
        std::fprintf(file, "%8.3f %s\n", traceSeconds(), line);
        std::fflush(file);
    }
    if (toConsole) {
        std::printf("[pad] %s\n", line);
        std::fflush(stdout);
    }
}

} // namespace

void pc_pad_trace_set_dir(const char* dir)
{
    if (!dir) {
        sTraceDir[0] = '\0';
        return;
    }
    std::snprintf(sTraceDir, sizeof(sTraceDir), "%s", dir);
}

void pc_pad_trace_poll(int instanceId, const char* device, const int raw[4], const int pad[4], int deadZone)
{
    const int mode = traceMode();
    if (mode == TRACE_OFF) {
        return;
    }

    static int previousRaw[4] = { 0, 0, 0, 0 };
    static int previousPad[4] = { 0, 0, 0, 0 };
    static int stuckPolls[4]  = { 0, 0, 0, 0 };
    static bool havePrevious  = false;
    static unsigned long long polls = 0;
    ++polls;

    const char* const axisName[4] = { "Lx", "Ly", "Rx", "Ry" };

    // What is wrong, as a short list. An empty list is a quiet poll.
    char tags[96] = { 0 };
    bool logged   = false;
    auto tag = [&tags, &logged](const char* text, const char* axis) {
        const size_t used = std::strlen(tags);
        if (used < sizeof(tags) - 12) {
            std::snprintf(tags + used, sizeof(tags) - used, "%s%s%s", logged ? " " : "", text, axis);
        }
        logged = true;
    };

    bool anyChanged = false;
    for (int i = 0; i < 4; ++i) {
        // A value the raw axis cannot explain: the axis is centred (or was
        // never touched) yet the pad carries a full deflection. That is the
        // keyboard/digital/touch path, a stale pad, or a bug in the merge.
        if (std::abs(pad[i]) >= kFullMagnitude && std::abs(raw[i]) <= deadZone) {
            tag("override:", axisName[i]);
        }
        // A corner reached from rest in a single poll: no thumb does that often,
        // but a driver that decides the axis is at -1.0 does.
        if (havePrevious && std::abs(previousRaw[i]) <= deadZone && std::abs(raw[i]) >= kRailMagnitude) {
            tag("snap:", axisName[i]);
        }
        // Held at the rail for a long stretch. A human reaches the rail and
        // comes back; a bogus reading sits there.
        stuckPolls[i] = std::abs(raw[i]) >= kRailMagnitude ? stuckPolls[i] + 1 : 0;
        if (stuckPolls[i] == kStuckPolls) {
            tag("stuck:", axisName[i]);
        }
        // A discontinuity that a real stick cannot make in one poll.
        if (havePrevious && std::abs(pad[i] - previousPad[i]) >= kJumpMagnitude) {
            tag("jump:", axisName[i]);
        }
        if (havePrevious && raw[i] != previousRaw[i]) {
            anyChanged = true;
        }
        if (havePrevious && pad[i] != previousPad[i]) {
            anyChanged = true;
        }
    }

    const bool report = logged || (mode == TRACE_VERBOSE && (anyChanged || !havePrevious));
    if (report && (mode == TRACE_VERBOSE || traceAllowed())) {
        char line[320];
        std::snprintf(line, sizeof(line),
                      "poll=%llu id=%d ctl=\"%s\" raw=(%d,%d,%d,%d) pad=(%d,%d,%d,%d) dz=%d tags=%s",
                      polls, instanceId, device ? device : "?", raw[0], raw[1], raw[2], raw[3], pad[0], pad[1],
                      pad[2], pad[3], deadZone, logged ? tags : "change");
        // Verbose sampling is a file, not a console: a line per sample would
        // bury the rest of the log.
        emit(line, mode != TRACE_VERBOSE);
    }

    for (int i = 0; i < 4; ++i) {
        previousRaw[i] = raw[i];
        previousPad[i] = pad[i];
    }
    havePrevious = true;
}

void pc_pad_trace_note(const char* what, const char* detail)
{
    if (traceMode() == TRACE_OFF) {
        return;
    }
    char line[256];
    std::snprintf(line, sizeof(line), "EVENT %s: %s", what ? what : "?", detail ? detail : "");
    emit(line, true);
}
