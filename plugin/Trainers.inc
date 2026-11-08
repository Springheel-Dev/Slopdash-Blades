// --- Trainers in the game (never through Aurora) ---------------------------------
// SlopDash loads a game's trainer as it starts it (src/platform/trainers.cpp:
// "dll.aurora.trainer", export 2 with the cheats chosen). Every trainer the
// console has is an XYZEngine one (28 of 28, checked 2026-10-22), and they all
// give Aurora the same thing through export 6: their cheat list, rebuilt with
// each cheat's state at each call -
//
//   +0xE4  the number of cheats
//   +0xE8  16 bytes a cheat: +4 on (BOOL), +8 its name (char*), +0xC a function
//          that switches it (no arguments; it applies the change in the game
//          and returns the new state)
//
// (Halo 3's trainer: export 6 at 9102D820 copies the list from 91046730 and
// refreshes the states from its flags at 910466A8; the switches, one a cheat,
// are 9102C288 on.) Start + Back, pressed alone, opens the Guide on Trainer
// (Guide1888.inc TrainerScreen). Each trainer opens its own keyboard menu on
// the same two buttons: its combination (a halfword, read by one small
// function every build has: stw r3,0x14(r1); lis r11,hi; lhz r3,lo(r11); blr)
// is set to one no pad can press, once, as it's found.
namespace trainer {

extern "C" LONG XexGetProcedureAddress(HANDLE module, DWORD ordinal, PVOID* address);

const char kModule[] = "dll.aurora.trainer";
const WORD kCombo = XINPUT_GAMEPAD_START | XINPUT_GAMEPAD_BACK;

struct Cheat
{
    DWORD kind;
    BOOL on;
    const char* name;
    BOOL (*flip)();
};

HANDLE Module()
{
    HANDLE h = NULL;
    return NT_OK(XexGetModuleHandle((PCHAR)kModule, &h)) ? h : NULL;
}

// The trainer's list now (NULL: no trainer).
BYTE* List()
{
    HANDLE m = Module();
    PVOID fn = NULL;
    if (!m || !NT_OK(XexGetProcedureAddress(m, 6, &fn)) || !fn)
        return NULL;
    typedef BYTE* (*ListFn)();
    BYTE* list = ((ListFn)fn)();
    return list && MmIsAddressValid(list) ? list : NULL;
}

int Count(const BYTE* list)
{
    int n = list ? (int)*(const DWORD*)(list + 0xE4) : 0;
    return n < 0 ? 0 : n > 32 ? 32 : n;
}

Cheat* At(BYTE* list, int i)
{
    return (Cheat*)(list + 0xE8 + 16 * i);
}

// Switches cheat i; its state after (the trainer's own word on it).
bool Flip(int i)
{
    BYTE* list = List();
    if (!list || i < 0 || i >= Count(list))
        return false;
    Cheat* c = At(list, i);
    if (!c->flip || !MmIsAddressValid((PVOID)c->flip))
        return false;
    BOOL on = c->flip();
    Log("trainer: %s %s", c->name && MmIsAddressValid((PVOID)c->name) ? c->name : "?", on ? "on" : "off");
    return on != FALSE;
}

// The trainer's own Start + Back menu switched off (its combination set to a
// halfword no pad sends). Once a trainer: by its module handle.
HANDLE g_quietened = NULL;

void Quieten(HANDLE m)
{
    if (m == g_quietened)
        return;
    g_quietened = m;
    // The module's image: its loader entry.
    KLDR_ENTRY* e = (KLDR_ENTRY*)m;
    BYTE* image = (BYTE*)e->ImageBase;
    DWORD size = e->SizeOfNtImage;
    if (!image || !size || !MmIsAddressValid(image))
        return;
    for (DWORD at = 0; at + 16 <= size; at += 4)
    {
        const DWORD* w = (const DWORD*)(image + at);
        if (w[0] != 0x90610014 || (w[1] & 0xFFFF0000) != 0x3D600000 || (w[2] & 0xFFFF0000) != 0xA06B0000 ||
            w[3] != 0x4E800020)
            continue;
        DWORD addr = (w[1] << 16) + (DWORD)(LONG)(SHORT)(w[2] & 0xFFFF);
        if (addr < (DWORD)image || addr >= (DWORD)image + size || !MmIsAddressValid((PVOID)addr))
            continue;
        WORD was = *(volatile WORD*)addr;
        *(volatile WORD*)addr = 0xFFFF;
        Log("trainer: its own menu off (combination at %08X, was %04X)", (unsigned)addr, (unsigned)was);
        return;
    }
    Log("trainer: its own menu's combination not found");
}

// Start + Back alone on any pad, pressed now (not held from before).
bool g_held = false;

bool ComboPressed()
{
    static PVOID getState = NULL;
    if (!getState)
    {
        HANDLE xam = NULL;
        if (!NT_OK(XexGetModuleHandle("xam.xex", &xam)) || !NT_OK(XexGetProcedureAddress(xam, 0x191, &getState)))
            return false;
    }
    typedef DWORD (*GetStateFn)(DWORD user, DWORD flags, XINPUT_STATE* state);
    bool down = false;
    for (DWORD u = 0; u < 4 && !down; u++)
    {
        XINPUT_STATE s;
        ZeroMemory(&s, sizeof(s));
        if (((GetStateFn)getState)(u, 1, &s) == ERROR_SUCCESS && s.Gamepad.wButtons == kCombo)
            down = true;
    }
    bool pressed = down && !g_held;
    g_held = down;
    return pressed;
}

}  // namespace trainer
