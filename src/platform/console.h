// The console itself: its serial number and ID (as 1888's System Info shows
// them), the kernel's version, temperatures, the IP address, free space, and
// power (restart, turn off).
#pragma once

#include <xtl.h>
#include <string>

namespace console {

// As 1888's System Info (dash 92121000): the serial number is key 0x14 (12
// characters), the console ID the kernel's own 12-character text. "" if unknown.
std::string SerialNumber();
std::string ConsoleId();

// The running kernel ("2.0.17559.0"), or "".
std::wstring KernelVersion();

// The system controller's temperature sensors, in degrees C.
struct Temps
{
    float cpu, gpu, memory, board;
};
bool Temperatures(Temps& t);

// The console's IP address ("10.0.0.5"), or "" without one.
std::wstring IpAddress();

// Whether the debug monitor (xbdm.xex: the PC's connection, port 730) is loaded.
bool DebugMonitorOn();

// A drive's free and total bytes ("hdd:\\"); false if it isn't there.
bool Space(const char* root, ULONGLONG& freeBytes, ULONGLONG& total);

// Under this much free, the hard drive is low on space.
const ULONGLONG kLowSpace = 2ULL << 30;
bool LowOnSpace(const char* root);

// A toast with the given text: a warning (xam's sad face) or not (happy face).
void Notify(const std::wstring& text, bool warning);

// The hard drive's space looked at: a toast when it has become low. Called
// now and then (main.cpp); quiet until it has recovered and gone low again.
void CheckSpace();

// Power, as 1888's xam does it (HalReturnToFirmware: 1 restarts, 818DFC84;
// 5 turns off, after its own Turn Off box, 818EA440). Neither returns.
void Restart();
void TurnOff();

// SlopDash started again (its own xex, as a theme switch does).
void RestartDashboard();

}  // namespace console
