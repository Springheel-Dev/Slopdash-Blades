// Stand-ins for a dashboard's own XUI classes.
//
// A .xur scene stores a custom class's properties by position, so XUI can only
// load it if a class of that name is registered with the same base class and
// the same property types in the same order. The tables come from
// tools/xurlearn.py. A stand-in keeps the property values it is given (e.g. a
// DashScene's button lists) and otherwise behaves exactly like its base class.
#pragma once

#include <xtl.h>
#include <xui.h>

enum { kStandInMaxProps = 10 };

struct StandInInfo
{
    const wchar_t* name;
    const wchar_t* base;
    int count;
    int types[kStandInMaxProps];        // XUI_ELEMENT_PROPERTY_TYPE values
};

// Registers every class in the table that XUI doesn't already know. Returns how many.
int RegisterStandIns(const StandInInfo* table, int count);

// A stand-in with behaviour: same stored properties, plus a handler that sees
// every message first (return S_OK without setting bHandled to pass it on).
// `vals[i]` is property i, `set[i]` whether the scene set it. Up to 24 classes.
typedef HRESULT (*StandInProc)(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal* vals, const bool* set);
bool RegisterStandInWithProc(const StandInInfo& info, StandInProc proc);
void UnregisterStandIns();

// A stand-in object's stored property, or NULL (wrong object, or never set).
const XUIElementPropVal* StandInProperty(HXUIOBJ obj, const wchar_t* className, int index);

// Whether the object is of one of the stand-in classes. XUI's own copy of a
// stand-in's property can be garbage (a bad pointer read as a string crashed
// SlopDash in XUIElementPropVal_SetString, 0.0.70): such properties are read
// with StandInProperty, never XuiObjectGetProperty.
bool IsStandIn(HXUIOBJ obj);
