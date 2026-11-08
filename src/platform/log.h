// game:\SlopDash.log (and the debug output). A line goes onto the USB stick
// from a thread of its own a moment later: written and flushed there, a line cost the caller
// 3.5 ms (2026-10-15), and the dashboard logs as it animates.
#pragma once

void Log(const char* fmt, ...);

// Everything logged so far written and flushed now, on the caller's thread:
// before SlopDash ends (a launch, a restart, power off) and in the crash
// filter, so the log still shows how far a run got.
void LogFlush();
