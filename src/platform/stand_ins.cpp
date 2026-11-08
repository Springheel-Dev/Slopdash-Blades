#include "platform/stand_ins.h"

#include "platform/log.h"

#include <stddef.h>
#include <string.h>
#include <map>
#include <new>
#include <vector>

namespace {

enum { kMaxClasses = 512 };

const DWORD kMagic = 0x52445349;     // "RDSI": a live stand-in instance

struct Instance
{
    // Each property's declared storage (its dwOffset). XUI may write a value
    // straight into the object at that offset instead of calling the setter;
    // with every offset 0 (before 0.0.20) that overwrote these fields.
    BYTE raw[kStandInMaxProps][32];
    HXUIOBJ obj;
    StandInProc proc;
    bool set[kStandInMaxProps];
    XUIElementPropVal vals[kStandInMaxProps];
    DWORD magic;                        // kMagic while alive; 0 once destroyed
};

// For some objects (those of 6717's dashmain: DashScene System, the gamer card,
// ScriptScene...) XUI hands the setters, the getters, the object proc and
// XuiDynamicCast the object's handle instead of the instance CreateInstance
// gave it. Used as a pointer, the setters wrote into low memory (0.0.109:
// Console Settings and the Games blade, signed in, crashed on what they
// broke). Each handle's instances, in the order made (a stand-in's base
// first), to find the instance from the handle. (Told apart by this map, not
// by size: a handle carries a reuse count in its upper bits, and after a few
// dozen screens had opened and closed they passed 0x00100000, the line drawn
// before 0.0.120; every property of a new screen was then dropped, Console
// Settings' descriptions with them.)
std::map<HXUIOBJ, std::vector<Instance*> > g_byHandle;

bool Live(const Instance* inst, const char* where, DWORD index = 0)
{
    bool live = false;
    if (inst && ((DWORD)(ULONG_PTR)inst & 3) == 0)
    {
        __try
        {
            live = inst->magic == kMagic;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            live = false;
        }
    }
    if (live)
        return true;
    static int logged = 0;
    if (logged++ < 20)
    {
        // Handles not made here: which object, then.
        const wchar_t* name = L"?";
        LPCWSTR id = NULL;
        XUIClass info;
        memset(&info, 0, sizeof(info));
        info.cbSize = sizeof(info);
        if (XuiHandleIsValid((HXUIOBJ)inst))
        {
            HXUICLASS cls = XuiGetObjectClass((HXUIOBJ)inst);
            if (cls && SUCCEEDED(XuiGetClass(cls, &info)) && info.szClassName)
                name = info.szClassName;
            XuiElementGetId((HXUIOBJ)inst, &id);
        }
        Log("stand-in: %s (index %u) given %08X (%S %S), not a live instance", where, (unsigned)index,
            (unsigned)(ULONG_PTR)inst, name, id ? id : L"");
    }
    return false;
}

struct Registered
{
    const StandInInfo* info;
    XUIElementPropDef defs[kStandInMaxProps];
    wchar_t names[kStandInMaxProps][4];     // "P0".."P9"
};

Registered g_classes[kMaxClasses];
int g_count = 0;

// The instance pvObj stands for: itself, or the first one made for a handle
// (the level that holds the properties when a stand-in is built on another).
Instance* Resolve(PVOID pvObj, bool withProc = false)
{
    std::map<HXUIOBJ, std::vector<Instance*> >::iterator it = g_byHandle.find((HXUIOBJ)pvObj);
    if (it != g_byHandle.end() && !it->second.empty())
    {
        if (withProc)
            for (size_t i = it->second.size(); i-- > 0;)
                if (it->second[i]->proc)
                    return it->second[i];
        return it->second[0];
    }
    return (Instance*)pvObj;
}

template <int P>
HRESULT APIENTRY SetProp(DWORD dwIndex, PVOID pvObj, const XUIElementPropVal* pVal)
{
    Instance* inst = Resolve(pvObj);
    if (!Live(inst, "SetProp (P<<16 | index)", (DWORD)(P << 16) | dwIndex))
        return S_OK;                    // dropped: failing it fails the whole scene
    inst->set[P] = true;
    return inst->vals[P].Copy(pVal);
}

template <int P>
HRESULT APIENTRY GetProp(DWORD dwIndex, PVOID pvObj, XUIElementPropVal* pVal)
{
    Instance* inst = Resolve(pvObj);
    if (!Live(inst, "GetProp", dwIndex))
        return E_FAIL;
    return pVal->Copy(&inst->vals[P]);
}

typedef HRESULT (APIENTRY* SetFn)(DWORD, PVOID, const XUIElementPropVal*);
typedef HRESULT (APIENTRY* GetFn)(DWORD, PVOID, XUIElementPropVal*);
const SetFn kSet[kStandInMaxProps] = { SetProp<0>, SetProp<1>, SetProp<2>, SetProp<3>, SetProp<4>,
                                       SetProp<5>, SetProp<6>, SetProp<7>, SetProp<8>, SetProp<9> };
const GetFn kGet[kStandInMaxProps] = { GetProp<0>, GetProp<1>, GetProp<2>, GetProp<3>, GetProp<4>,
                                       GetProp<5>, GetProp<6>, GetProp<7>, GetProp<8>, GetProp<9> };

HRESULT APIENTRY Create(HXUIOBJ hObj, void** ppvObj)
{
    Instance* inst = new (std::nothrow) Instance();
    if (!inst)
        return E_OUTOFMEMORY;
    memset(inst->raw, 0, sizeof(inst->raw));
    inst->obj = hObj;
    inst->proc = NULL;
    inst->magic = kMagic;
    g_byHandle[hObj].push_back(inst);
    for (int i = 0; i < kStandInMaxProps; i++)
        inst->set[i] = false;
    *ppvObj = inst;
    return S_OK;
}

HRESULT APIENTRY Destroy(void* pvObj)
{
    Instance* inst = Resolve(pvObj);
    if (!Live(inst, "Destroy"))
        return S_OK;
    std::map<HXUIOBJ, std::vector<Instance*> >::iterator it = g_byHandle.find(inst->obj);
    if (it != g_byHandle.end())
    {
        for (size_t i = 0; i < it->second.size(); i++)
            if (it->second[i] == inst)
            {
                it->second.erase(it->second.begin() + i);
                break;
            }
        if (it->second.empty())
            g_byHandle.erase(it);
    }
    inst->magic = 0;
    delete inst;
    return S_OK;
}

// Nothing handled here: every message goes on to the base class.
HRESULT APIENTRY Proc(HXUIOBJ, XUIMessage*, void*)
{
    return S_OK;
}

// Classes with behaviour: CreateInstance can't tell which class it's creating,
// so each gets its own instantiation that records its handler.
// (64 filled up as 1888's music area was added, 2026-10-21: every class after
// the 64th failed to register, the blades' own among them, and the Games and
// Media blades lost their setup.)
enum { kMaxProcClasses = 160 };  // one per class with a handler (1888's Console Settings: 22 of them)
StandInProc g_procs[kMaxProcClasses];
int g_procCount = 0;

template <int N>
HRESULT APIENTRY CreateWithProc(HXUIOBJ hObj, void** ppvObj)
{
    HRESULT hr = Create(hObj, ppvObj);
    if (SUCCEEDED(hr))
        ((Instance*)*ppvObj)->proc = g_procs[N];
    return hr;
}

HRESULT APIENTRY ProcWithHandler(HXUIOBJ hObj, XUIMessage* msg, void* pvThis)
{
    Instance* inst = Resolve(pvThis, true);
    if (!Live(inst, "ObjectProc"))
        return S_OK;
    return inst->proc ? inst->proc(hObj, msg, inst->vals, inst->set) : S_OK;
}

typedef HRESULT (APIENTRY* CreateFn)(HXUIOBJ, void**);
const CreateFn kCreateWithProc[kMaxProcClasses] = {
    CreateWithProc<0>, CreateWithProc<1>, CreateWithProc<2>, CreateWithProc<3>, CreateWithProc<4>,
    CreateWithProc<5>, CreateWithProc<6>, CreateWithProc<7>, CreateWithProc<8>, CreateWithProc<9>,
    CreateWithProc<10>, CreateWithProc<11>, CreateWithProc<12>, CreateWithProc<13>, CreateWithProc<14>,
    CreateWithProc<15>, CreateWithProc<16>, CreateWithProc<17>, CreateWithProc<18>, CreateWithProc<19>,
    CreateWithProc<20>, CreateWithProc<21>, CreateWithProc<22>, CreateWithProc<23>, CreateWithProc<24>,
    CreateWithProc<25>, CreateWithProc<26>, CreateWithProc<27>, CreateWithProc<28>, CreateWithProc<29>,
    CreateWithProc<30>, CreateWithProc<31>, CreateWithProc<32>, CreateWithProc<33>, CreateWithProc<34>,
    CreateWithProc<35>, CreateWithProc<36>, CreateWithProc<37>, CreateWithProc<38>, CreateWithProc<39>,
    CreateWithProc<40>, CreateWithProc<41>, CreateWithProc<42>, CreateWithProc<43>, CreateWithProc<44>,
    CreateWithProc<45>, CreateWithProc<46>, CreateWithProc<47>, CreateWithProc<48>, CreateWithProc<49>,
    CreateWithProc<50>, CreateWithProc<51>, CreateWithProc<52>, CreateWithProc<53>, CreateWithProc<54>,
    CreateWithProc<55>, CreateWithProc<56>, CreateWithProc<57>, CreateWithProc<58>, CreateWithProc<59>,
    CreateWithProc<60>, CreateWithProc<61>, CreateWithProc<62>, CreateWithProc<63>, CreateWithProc<64>,
    CreateWithProc<65>, CreateWithProc<66>, CreateWithProc<67>, CreateWithProc<68>, CreateWithProc<69>,
    CreateWithProc<70>, CreateWithProc<71>, CreateWithProc<72>, CreateWithProc<73>, CreateWithProc<74>,
    CreateWithProc<75>, CreateWithProc<76>, CreateWithProc<77>, CreateWithProc<78>, CreateWithProc<79>,
    CreateWithProc<80>, CreateWithProc<81>, CreateWithProc<82>, CreateWithProc<83>, CreateWithProc<84>,
    CreateWithProc<85>, CreateWithProc<86>, CreateWithProc<87>, CreateWithProc<88>, CreateWithProc<89>,
    CreateWithProc<90>, CreateWithProc<91>, CreateWithProc<92>, CreateWithProc<93>, CreateWithProc<94>,
    CreateWithProc<95>, CreateWithProc<96>, CreateWithProc<97>, CreateWithProc<98>, CreateWithProc<99>,
    CreateWithProc<100>, CreateWithProc<101>, CreateWithProc<102>, CreateWithProc<103>, CreateWithProc<104>,
    CreateWithProc<105>, CreateWithProc<106>, CreateWithProc<107>, CreateWithProc<108>, CreateWithProc<109>,
    CreateWithProc<110>, CreateWithProc<111>, CreateWithProc<112>, CreateWithProc<113>, CreateWithProc<114>,
    CreateWithProc<115>, CreateWithProc<116>, CreateWithProc<117>, CreateWithProc<118>, CreateWithProc<119>,
    CreateWithProc<120>, CreateWithProc<121>, CreateWithProc<122>, CreateWithProc<123>, CreateWithProc<124>,
    CreateWithProc<125>, CreateWithProc<126>, CreateWithProc<127>, CreateWithProc<128>, CreateWithProc<129>,
    CreateWithProc<130>, CreateWithProc<131>, CreateWithProc<132>, CreateWithProc<133>, CreateWithProc<134>,
    CreateWithProc<135>, CreateWithProc<136>, CreateWithProc<137>, CreateWithProc<138>, CreateWithProc<139>,
    CreateWithProc<140>, CreateWithProc<141>, CreateWithProc<142>, CreateWithProc<143>, CreateWithProc<144>,
    CreateWithProc<145>, CreateWithProc<146>, CreateWithProc<147>, CreateWithProc<148>, CreateWithProc<149>,
    CreateWithProc<150>, CreateWithProc<151>, CreateWithProc<152>, CreateWithProc<153>, CreateWithProc<154>,
    CreateWithProc<155>, CreateWithProc<156>, CreateWithProc<157>, CreateWithProc<158>, CreateWithProc<159>  };

bool RegisterOne(const StandInInfo& info, CreateFn create, PFN_OBJECT_PROC proc)
{
    if (g_count >= kMaxClasses || XuiFindClass(info.name))
        return false;                               // full, or the runtime has its own
    Registered& r = g_classes[g_count];
    r.info = &info;
    memset(r.defs, 0, sizeof(r.defs));
    for (int p = 0; p < info.count && p < kStandInMaxProps; p++)
    {
        r.names[p][0] = L'P';
        r.names[p][1] = (wchar_t)(L'0' + p);
        r.names[p][2] = 0;
        XUIElementPropDef& d = r.defs[p];
        d.dwId = (DWORD)p;
        d.dwOffset = (DWORD)(offsetof(Instance, raw) + sizeof(((Instance*)0)->raw[0]) * p);
        d.szPropName = r.names[p];
        d.Type = (XUI_ELEMENT_PROPERTY_TYPE)info.types[p];
        d.pfnGetProp = kGet[p];
        d.pfnSetProp = kSet[p];
    }
    XUIClass cls;
    memset(&cls, 0, sizeof(cls));
    cls.szClassName = info.name;
    cls.szBaseClassName = info.base;
    cls.Methods.CreateInstance = create;
    cls.Methods.DestroyInstance = Destroy;
    cls.Methods.ObjectProc = proc;
    cls.pPropDefs = info.count ? r.defs : NULL;
    cls.dwPropDefCount = (DWORD)info.count;
    HXUICLASS h = NULL;
    if (FAILED(XuiRegisterClass(&cls, &h)))
        return false;
    g_count++;
    return true;
}

}  // namespace

int RegisterStandIns(const StandInInfo* table, int count)
{
    int added = 0;
    for (int i = 0; i < count; i++)
        if (RegisterOne(table[i], Create, Proc))
            added++;
    return added;
}

bool RegisterStandInWithProc(const StandInInfo& info, StandInProc proc)
{
    if (g_procCount >= kMaxProcClasses)
    {
        Log("stand-ins: no handler slot left for %S (%d used): raise kMaxProcClasses", info.name, g_procCount);
        return false;
    }
    g_procs[g_procCount] = proc;
    if (!RegisterOne(info, kCreateWithProc[g_procCount], ProcWithHandler))
        return false;
    g_procCount++;
    return true;
}

void UnregisterStandIns()
{
    for (int i = 0; i < g_count; i++)
        XuiUnregisterClass(g_classes[i].info->name);
    g_count = 0;
    g_procCount = 0;
}

const XUIElementPropVal* StandInProperty(HXUIOBJ obj, const wchar_t* className, int index)
{
    HXUICLASS cls = XuiFindClass(className);
    if (!cls || index < 0 || index >= kStandInMaxProps)
        return NULL;
    for (HXUIOBJ o = obj; o; )
    {
        if (XuiGetObjectClass(o) == cls)
        {
            Instance* inst = Resolve(XuiDynamicCast(o, cls));
            if (inst && !Live(inst, "StandInProperty"))
            {
                Log("  (%S property %d, object %08X)", className, index, (unsigned)(ULONG_PTR)o);
                return NULL;
            }
            return inst && inst->set[index] ? &inst->vals[index] : NULL;
        }
        HXUIOBJ base = NULL;
        if (FAILED(XuiGetBaseObject(o, &base)))
            break;
        o = base;
    }
    return NULL;
}

bool IsStandIn(HXUIOBJ obj)
{
    HXUICLASS cls = obj ? XuiGetObjectClass(obj) : NULL;
    if (!cls)
        return false;
    XUIClass info;
    memset(&info, 0, sizeof(info));
    info.cbSize = sizeof(info);
    if (FAILED(XuiGetClass(cls, &info)) || !info.szClassName)
        return false;
    for (int i = 0; i < g_count; i++)
        if (!wcscmp(g_classes[i].info->name, info.szClassName))
            return true;
    return false;
}
