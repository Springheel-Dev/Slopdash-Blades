// SlopDash's own implementations of Blades classes (the rest are stand-ins).
#pragma once

#include <xtl.h>
#include <xui.h>

// Registers the implemented classes. Call before RegisterStandIns, which then
// skips them.
HRESULT RegisterBladesClasses();
void UnregisterBladesClasses();

// The first signed-in user (0-3), or -1.
int SignedInUser();

// A storage device changed (XN_SYS_STORAGEDEVICESCHANGED): the disc tray looks
// at the drive again for a few seconds.
void RequestDiscCheck();

// A pressed with the focus on a "Sign In" card: opens the profile list (true if it was one).
bool PressSignInCard(HXUIOBJ focus);

// Blades' own options (System > Themes > Y). The Homebrew blade: the
// Marketplace blade, renamed (for now). Applied at start-up and on a change;
// `any` is any element of the dashboard.
bool BladesHomebrewOn();
void ApplyBladesOptions(HXUIOBJ any);
bool SetBladesHomebrew(bool on, HXUIOBJ any);

// The image locator the gamer card uses for the signed-in user's gamer picture;
// the texture loader answers it (XUserReadGamerPicture).
static const wchar_t* const kGamerPictureLocator = L"slopdash://gamerpicture";

// --- shared with the Blades (launch) theme (theme/blades1888.cpp) -------------------
// The Games Library (arcade#2502_TitleSelectorScene: its list, views, game menu
// and Library Settings) and SlopDash's list screens (Themes, on the Language
// screen's layout) work the same on 2.0.1888's scenes, which have the same
// classes. Registers those classes (and their bases' DashScene must be in place).
void RegisterSharedBladesClasses();
// System > Themes, opened from `from`.
HRESULT OpenThemesScreen(HXUIOBJ from);
// The File Manager (looking only): a drive's root ("hdd:", "usb:"), or with
// NULL the drives to choose from.
HRESULT OpenFileManager(HXUIOBJ from, const char* root);
// The homebrew store's screens (theme/store_screens.inc): a shelf (the
// Marketplace hub's tiles, store::Shelf order), Active Downloads, Add Repo.
HRESULT OpenStoreShelf(HXUIOBJ from, int shelf);
HRESULT OpenStoreDownloads(HXUIOBJ from);
HRESULT OpenStoreRepos(HXUIOBJ from);
// RetroAchievements' sign-in and account (theme/ra_screen.inc).
HRESULT OpenRetroAchievements(HXUIOBJ from);
// Network Settings > Join a Service: XLink Kai, in progress (theme/services_screen.inc).
HRESULT OpenJoinService(HXUIOBJ from);
// Where a screen opened from the blades goes is the theme's: the opener
// navigates to `scene` and returns S_OK (or a failure), or E_NOTIMPL when
// `from` isn't on its blades.
typedef HRESULT (*ScreenOpener)(HXUIOBJ from, HXUIOBJ scene);
void SetScreenOpener(ScreenOpener opener);

// A game's screen the Xbox theme asked Blades for (SlopDash.ini
// blades.opengame = <title ID>:<achievements|saves|updates|dlc|trainers>,
// read once and removed): its Game Menu, then that screen. False while the
// game library isn't ready (asked again later). game_menu.inc.
bool OpenGameFromBladesSetting(HXUIOBJ from);
// The on-screen keyboard, XShowKeyboardUI's arguments and result: the theme's
// own when it has one (the launch theme: 1888's, in its Guide), completing
// `overlapped` as the system's does; else the system's.
typedef DWORD (*KeyboardShower)(DWORD user, DWORD flags, LPCWSTR defaultText, LPCWSTR title, LPCWSTR description,
                                LPWSTR result, DWORD maxChars, PXOVERLAPPED overlapped);
void SetKeyboardShower(KeyboardShower shower);
DWORD ShowKeyboard(DWORD user, DWORD flags, LPCWSTR defaultText, LPCWSTR title, LPCWSTR description, LPWSTR result,
                   DWORD maxChars, PXOVERLAPPED overlapped);
