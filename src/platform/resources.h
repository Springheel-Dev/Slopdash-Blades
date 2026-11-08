// The user's own dashboard files, unpacked into memory, and the locators XUI
// uses to reach them. Nothing is copied to disk.
#pragma once

#include <string>
#include <vector>

#include "core/xur.h"

// Reads and unpacks dash.xex (its XUI packages) and the shared-images package
// next to it. Logs and returns false on failure.
bool LoadDashboard(const char* dashPath, const char* sharedResPath);

// The launch dashboard (2.0.1888) from a theme folder: <folder>\dash.xex and
// <folder>\xam.xex (the gamer card, notifications, the Guide's blade and the
// shared images, "sharedres://"). Every scene is upgraded in memory for the
// XDK's XUI runtime (core/xur.h); `custom` is the dashboard's own classes.
// dash6717: 6717's dash.xex, for its five blades (the Marketplace blade).
bool LoadDashboard1888(const std::string& folder, const std::vector<xur::CustomClass>& custom, const char* dash6717);

// The same from the theme's pack (<Themes>\blades1888.pak, core/pak.h): the
// modules' packages are already out of their .xex files, so nothing is
// decrypted; the scenes are upgraded as above. The pack is kept for the run.
bool LoadDashboard1888Pack(const char* path, const std::vector<xur::CustomClass>& custom);

// A file in the loaded theme pack (e.g. "xenonclatin.xtt"); false if it isn't there.
bool ThemePackFile(const char* name, const unsigned char** data, unsigned* size);

// L"memory://<address>,<size>#" for one of the dashboard's packages
// (e.g. L"gamesbla"); false if there's no such package.
bool PackageLocator(const wchar_t* package, std::wstring& out);

// A file in one of the dashboard's packages, to change in memory before XUI
// reads it (the Blades colours: platform/blade_colours.h). NULL if the package
// or file isn't there.
unsigned char* PackageFileForEdit(const wchar_t* package, const wchar_t* file, unsigned* size);

// Rewrites a locator the dashboard's scenes use into one XUI can open:
//   section://<module>,<package>#<file>   -> memory://<package>#<file>
//   common://<file>                       -> the dashcomm package
//   sharedres://<file>                    -> shrdres.xzp
//   memory://<package>#<file>             -> the package that really holds <file>
//                                            (scenes refer to files in other packages)
// Returns false when the locator is fine as it is.
bool ResolveLocator(const wchar_t* locator, std::wstring& out);

// The dashboards' XMA1 sounds as XMA2, so they play (resources.cpp: on, with a
// guard against a start that dies). SoundTestArm at start-up, SoundsSettled once
// it has run a while.
void SoundTestArm();
void SoundsSettled();

// The bytes of the file a resolved "memory://<package>#<file>" locator names,
// or false if it isn't one of the dashboard's packages / files.
bool LocatorBytes(const wchar_t* locator, const unsigned char** data, unsigned* size);
