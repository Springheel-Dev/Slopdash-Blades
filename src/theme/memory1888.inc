// --- Memory (System > Memory) -----------------------------------------------------
// 1888's Memory area, on its own screens
// and in its own words (memory#Strings.xus):
//   a drive (DeviceSelector) - A: its categories (Categories: Games, Gamer
//   Profiles, Demos, Videos and Trailers, Themes, Gamer Pictures, each with
//   its count); Y: Device Options (DeviceDetails: the drive's name,
//   capacity, free space; SlopDash's File Manager there - A did that before).
//   A category: its items (Items; for Games first the games, "Select Game",
//   then the game's items). An item (ItemDetails: name, type, owner, size,
//   location, thumbnail): Copy, Move, Delete - to and from the other drive,
//   with 1888's boxes (replace, delete) and progress (OperationProgress).
// The content itself: platform/storage.h (the BadAvatar exploit's profile and
// its content are never listed). Formatting isn't offered: the drives hold
// SlopDash and the softmod.
// Included by blades1888.cpp (namespace launch).
const DWORD kMemoryTimer = 0x4D454D31;          // "MEM1": once a memory screen's parts exist
const DWORD kMemoryAskTimer = 0x4D454D32;       // "MEM2": a box answered
const DWORD kMemoryOpTimer = 0x4D454D33;        // "MEM3": copy / move / delete going on

HXUIOBJ g_categoriesScene = NULL;               // the drive's categories (back to it after a move or delete)
storage::Category g_memCategory = storage::kGames;
// What the next Items screen lists: the games (true) or items.
bool g_nextItemsAreGames = false;
std::vector<size_t> g_nextItems;
std::map<HXUIOBJ, bool> g_itemsAreGames;        // by Items scene
std::map<HXUIOBJ, std::vector<size_t> > g_itemsOf;
std::wstring g_nextItemsTitle;                  // the next Items screen's heading
std::map<HXUIOBJ, std::wstring> g_itemsTitle;
size_t g_memItem = 0;                           // the item ItemDetails is for
storage::Op g_memOp = storage::kCopy;

// A Yes / No box (1888's), answered later.
enum MemAsk { kMemAskNone, kMemAskReplace, kMemAskDelete };
struct MemAsking
{
    MemAsk what;
    XOVERLAPPED ov;
    MESSAGEBOX_RESULT result;
} g_memAsk = { kMemAskNone };

void MemoryAskYesNo(HXUIOBJ scene, MemAsk what, const wchar_t* title, const wchar_t* text)
{
    static LPCWSTR buttons[] = { L"Yes", L"No" };
    int user = SignedInUser();
    ZeroMemory(&g_memAsk.ov, sizeof(g_memAsk.ov));
    DWORD r = XShowMessageBoxUI(user >= 0 ? user : 0, title, text, 2, buttons, 1, XMB_WARNINGICON, &g_memAsk.result,
                                &g_memAsk.ov);
    if (r != ERROR_IO_PENDING && r != ERROR_SUCCESS)
        return;
    BoxOpened(&g_memAsk.ov, r);
    g_memAsk.what = what;
    XuiSetTimer(scene, kMemoryAskTimer, 100);
}

void StartMemoryOp(HXUIOBJ scene)
{
    std::string to = g_memOp == storage::kDelete ? std::string() : storage::OtherDrive();
    if (!storage::Start(g_memOp, g_memItem, to))
    {
        const wchar_t* why = storage::LastError();
        SayBox(*why == L'T' && wcsstr(why, L"full") ? L"No Space Available" : L"Can't Perform Action",
               *why ? why : L"There was a problem performing that action.");
        return;
    }
    OpenOver(scene, L"memory", L"OperationProgress.xur");
}

HRESULT MemoryCategoriesListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = storage::kCategoryCount;
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        static std::wstring held;
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData)
        {
            held.clear();
            if (d->iItem >= 0 && d->iItem < storage::kCategoryCount)
            {
                storage::Category c = (storage::Category)d->iItem;
                size_t n = storage::InCategory(c).size();
                wchar_t b[64];
                _snwprintf_s(b, 64, _TRUNCATE, n == 1 ? L"%s (%d item)" : L"%s (%d items)",
                             storage::CategoryName(c), (int)n);
                held = d->iData == 0 ? b : L"";
            }
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        // 1888's own icons for them (the memory package's; games and gamer
        // pictures from the shared art).
        static const wchar_t* const kIcons[] = { L"common://ico_64x_games.png",
                                                 L"section://0,memory#ico_64x_profiles.png",
                                                 L"section://0,memory#ico_64x_Demos.png",
                                                 L"section://0,memory#ico_64x_Trailers.png",
                                                 L"section://0,memory#ico_64x_skins.png",
                                                 L"sharedres://ico_64x_Friend.png" };
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = d->iData == 0 && d->iItem >= 0 && d->iItem < storage::kCategoryCount ? kIcons[d->iItem] : NULL;
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

void ShowOneOf(HXUIOBJ scene, HXUIOBJ list, int n)
{
    int at = list ? XuiListGetCurSel(list, NULL) : -1;
    wchar_t b[32] = L"";
    if (n > 0)
        _snwprintf_s(b, 32, _TRUNCATE, L"%d of %d", (at < 0 ? 0 : at) + 1, n);
    SetText(scene, L"labTotal", b);
}

HRESULT MemoryCategoriesProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"listCategories");
    if (msg->dwMessage == XM_INIT)
    {
        g_categoriesScene = obj;
        XuiSetTimer(obj, kMemoryTimer, 30);
    }
    else if (msg->dwMessage == XM_DESTROY && obj == g_categoriesScene)
        g_categoriesScene = NULL;
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryTimer)
    {
        XuiKillTimer(obj, kMemoryTimer);
        SetText(obj, L"labHeader", storage::DriveName(storage::Drive()).c_str());
        if (list)
        {
            SetListCount(list, storage::kCategoryCount);
            XuiElementSetFocus(list);
        }
        ShowOneOf(obj, list, storage::kCategoryCount);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && list)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        if (n->dwNotify == XN_SELCHANGED)
            ShowOneOf(obj, list, storage::kCategoryCount);
        else if (n->dwNotify == XN_PRESS && IsWithin(n->hObjSource, list) && !HudOpen())
        {
            int at = XuiListGetCurSel(list, NULL);
            if (at >= 0 && at < storage::kCategoryCount)
            {
                g_memCategory = (storage::Category)at;
                g_nextItemsAreGames = g_memCategory == storage::kGames;
                g_nextItems = storage::InCategory(g_memCategory);
                g_nextItemsTitle = g_nextItemsAreGames ? L"Select Game" : storage::CategoryName(g_memCategory);
                OpenOver(obj, L"memory", L"Items.xur");
            }
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

HRESULT MemoryItemsListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ scene = NULL;
    XuiElementGetParent(obj, &scene);
    bool games = g_itemsAreGames.count(scene) ? g_itemsAreGames[scene] : g_nextItemsAreGames;
    const std::vector<size_t>& items = g_itemsOf.count(scene) ? g_itemsOf[scene] : g_nextItems;
    int n = games ? (int)storage::Games().size() : (int)items.size();
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = n;
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_TEXT)
    {
        static std::wstring held;
        XUIMessageGetSourceText* d = (XUIMessageGetSourceText*)msg->pvData;
        if (d->bItemData)
        {
            held.clear();
            if (d->iItem >= 0 && d->iItem < n)
            {
                if (games)
                {
                    const storage::Game& g = storage::Games()[d->iItem];
                    wchar_t b[32];
                    _snwprintf_s(b, 32, _TRUNCATE, g.items.size() == 1 ? L"%d item" : L"%d items",
                                 (int)g.items.size());
                    held = d->iData == 0 ? g.name : d->iData == 1 ? std::wstring(b) : std::wstring();
                }
                else
                {
                    const storage::Item& it = storage::At(items[d->iItem]);
                    held = d->iData == 0 ? it.name : d->iData == 1 ? storage::SizeText(it.size) : std::wstring();
                }
            }
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        static std::wstring held;
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            held.clear();
            if (d->iData == 0 && d->iItem >= 0 && d->iItem < n)
                held = storage::Locator(games ? storage::Games()[d->iItem].items[0] : items[d->iItem]);
            d->szPath = held.empty() ? NULL : held.c_str();
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

HRESULT MemoryItemsProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"list_items");
    if (msg->dwMessage == XM_INIT)
    {
        g_itemsAreGames[obj] = g_nextItemsAreGames;
        g_itemsOf[obj] = g_nextItems;
        g_itemsTitle[obj] = g_nextItemsTitle;
        XuiSetTimer(obj, kMemoryTimer, 30);
    }
    else if (msg->dwMessage == XM_DESTROY)
    {
        g_itemsAreGames.erase(obj);
        g_itemsOf.erase(obj);
        g_itemsTitle.erase(obj);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryTimer)
    {
        XuiKillTimer(obj, kMemoryTimer);
        bool games = g_itemsAreGames[obj];
        int n = games ? (int)storage::Games().size() : (int)g_itemsOf[obj].size();
        SetText(obj, L"txt_Header", g_itemsTitle[obj].c_str());
        Show(obj, L"txt_SelectProfile", false);
        Show(obj, L"labDots", false);
        Show(obj, L"txt_EmptyList", n == 0);
        if (list)
        {
            XuiElementSetShow(list, n > 0);
            if (n)
            {
                SetListCount(list, n);
                XuiElementSetFocus(list);
            }
        }
        ShowOneOf(obj, list, n);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && list)
    {
        XUINotify* nt = (XUINotify*)msg->pvData;
        bool games = g_itemsAreGames[obj];
        int n = games ? (int)storage::Games().size() : (int)g_itemsOf[obj].size();
        if (nt->dwNotify == XN_SELCHANGED)
            ShowOneOf(obj, list, n);
        else if (nt->dwNotify == XN_PRESS && IsWithin(nt->hObjSource, list) && !HudOpen())
        {
            int at = XuiListGetCurSel(list, NULL);
            if (at >= 0 && at < n)
            {
                if (games)
                {
                    g_nextItemsAreGames = false;
                    g_nextItems = storage::Games()[at].items;
                    g_nextItemsTitle = storage::Games()[at].name;
                    OpenOver(obj, L"memory", L"Items.xur");
                }
                else
                {
                    g_memItem = g_itemsOf[obj][at];
                    OpenOver(obj, L"memory", L"ItemDetails.xur");
                }
            }
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

HRESULT MemoryItemDetailsProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMemoryTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryTimer)
    {
        XuiKillTimer(obj, kMemoryTimer);
        const storage::Item& it = storage::At(g_memItem);
        SetText(obj, L"txt_MetaHead", it.name.c_str());
        SetText(obj, L"txt_MetaSubHead", storage::TypeName(it.type));
        std::wstring owner = storage::OwnerName(g_memItem);
        SetText(obj, L"txt_Owner", owner.c_str());
        Show(obj, L"txt_Owner", !owner.empty());
        Show(obj, L"txt_OwnerHead", !owner.empty());
        SetText(obj, L"txt_Size", storage::SizeText(it.size).c_str());
        SetText(obj, L"txt_Device", storage::DriveName(storage::Drive()).c_str());
        // The thumbnail: the file's spot is a presenter (1888's code fed it):
        // an image in its place.
        if (HXUIOBJ thumb = Child(obj, L"image_thumbnail"))
        {
            std::wstring at = storage::Locator(g_memItem);
            HXUIOBJ img = NULL;
            if (!at.empty() && SUCCEEDED(XuiCreateObject(XUI_CLASS_IMAGE, &img)) && img)
            {
                float w = 0, h = 0;
                XuiElementGetBounds(thumb, &w, &h);
                D3DXVECTOR3 p(0, 0, 0);
                XuiElementGetPosition(thumb, &p);
                XuiElementSetBounds(img, w, h);
                XuiElementSetPosition(img, &p);
                SetUint(img, L"SizeMode", 8);           // (fitted, its own shape)
                XuiElementAddChild(obj, img);
                XuiImageElementSetImagePath(img, at.c_str());
            }
            XuiElementSetShow(thumb, FALSE);
        }
        std::vector<std::wstring> o;
        o.push_back(L"Copy");
        o.push_back(L"Move");
        o.push_back(L"Delete");
        FillOptions(obj, o);
        if (HXUIOBJ list = Child(obj, L"listOptions"))
            XuiElementSetFocus(list);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryAskTimer)
    {
        if (g_memAsk.what != kMemAskNone && XHasOverlappedIoCompleted(&g_memAsk.ov))
        {
            XuiKillTimer(obj, kMemoryAskTimer);
            bool yes = XGetOverlappedExtendedError(&g_memAsk.ov) == ERROR_SUCCESS && g_memAsk.result.dwButtonPressed == 0;
            g_memAsk.what = kMemAskNone;
            if (yes)
                StartMemoryOp(obj);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ list = Child(obj, L"listOptions");
        if (!list || !IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
            return S_OK;
        msg->bHandled = TRUE;
        int at = XuiListGetCurSel(list, NULL);
        const storage::Item& it = storage::At(g_memItem);
        bool profile = it.type == 0x10000;
        if (at == 0 || at == 1)
        {
            g_memOp = at == 0 ? storage::kCopy : storage::kMove;
            std::string to = storage::OtherDrive();
            if (to.empty())
            {
                SayBox(L"Can't Transfer Item", L"Connect another storage device.");
                return S_OK;
            }
            if (profile && g_memOp == storage::kMove)
            {
                // (1888 signed it out first; here it has to be already)
                for (DWORD u = 0; u < 4; u++)
                {
                    XUID x = 0;
                    if (XUserGetXUID(u, &x) == ERROR_SUCCESS && x == it.owner)
                    {
                        SayBox(L"Move Profile", L"Before moving a profile, the profile must be signed out.");
                        return S_OK;
                    }
                }
            }
            if (GetFileAttributesA(storage::PathOn(g_memItem, to).c_str()) != (DWORD)-1)
                MemoryAskYesNo(obj, kMemAskReplace, L"Replace Item",
                               L"The destination device already contains this item. Would you like to replace the "
                               L"existing item?");
            else
                StartMemoryOp(obj);
        }
        else if (at == 2)
        {
            g_memOp = storage::kDelete;
            MemoryAskYesNo(obj, kMemAskDelete, L"Delete Item",
                           profile ? L"Are you sure you want to delete this profile? Deleting a profile permanently "
                                     L"deletes everything associated with the profile, including saved games and "
                                     L"achievements."
                                   : L"Are you sure you want to permanently delete this item?");
        }
    }
    return S_OK;
}

// Copy / move / delete going on (OperationProgress): 1888's words and bar;
// done, back to the item (a copy) or to the drive's categories (a move or a
// delete: the item's gone from here), the drive read again.
HRESULT MemoryProgressProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        if (HXUIOBJ bar = Child(obj, L"xuiProgressBar"))
            XuiProgressBarSetRange(bar, 0, 100);
        XuiSetTimer(obj, kMemoryOpTimer, 100);
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryOpTimer)
    {
        static const wchar_t* const kHeads[] = { L"Copy", L"Move", L"Delete" };
        static const wchar_t* const kTexts[] = { L"Copy in progress.\r\nPlease do not turn off your console.",
                                                 L"Move in progress.\r\nPlease do not turn off your console.",
                                                 L"Delete in progress.\r\nPlease do not turn off your console." };
        SetText(obj, L"txt_Header", kHeads[g_memOp]);
        SetText(obj, L"txt_MetaHead", kTexts[g_memOp]);
        if (HXUIOBJ bar = Child(obj, L"xuiProgressBar"))
            XuiProgressBarSetValue(bar, storage::Percent());
        if (!storage::Busy())
        {
            XuiKillTimer(obj, kMemoryOpTimer);
            bool ok = storage::LastSucceeded();
            if (!ok)
                SayBox(L"Can't Perform Action", *storage::LastError() ? storage::LastError()
                                                                      : L"There was a problem performing that action.");
            if (ok && g_memOp != storage::kCopy && g_categoriesScene)
            {
                storage::Scan(storage::Drive());
                XuiSceneNavigateBack(obj, g_categoriesScene, XUSER_INDEX_FOCUS);
                if (HXUIOBJ list = Child(g_categoriesScene, L"listCategories"))
                    SetListCount(list, storage::kCategoryCount);
            }
            else
                GoBack(obj);
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// Device Options (DeviceDetails): the drive's name, capacity and free space.
std::string g_optionsDrive;

HRESULT MemoryDeviceDetailsProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMemoryTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMemoryTimer)
    {
        XuiKillTimer(obj, kMemoryTimer);
        SetText(obj, L"labMetaHead", storage::DriveName(g_optionsDrive).c_str());
        ULARGE_INTEGER freeBytes, total, totalFree;
        if (GetDiskFreeSpaceExA((g_optionsDrive + "\\").c_str(), &freeBytes, &total, &totalFree))
        {
            SetText(obj, L"labSize", storage::SizeText(total.QuadPart).c_str());
            SetText(obj, L"labFreeSpace", (storage::SizeText(freeBytes.QuadPart) + L" free").c_str());
        }
        std::vector<std::wstring> o;
        o.push_back(L"File Manager");
        o.push_back(L"Format Device");
        FillOptions(obj, o);
        if (HXUIOBJ list = Child(obj, L"listOptions"))
            XuiElementSetFocus(list);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ list = Child(obj, L"listOptions");
        if (list && IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
        {
            if (XuiListGetCurSel(list, NULL) == 0)
            {
                HRESULT hr = OpenFileManager(obj, g_optionsDrive.c_str());
                Log("1888 memory: files at %s: %08X", g_optionsDrive.c_str(), (unsigned)hr);
            }
            else
                SayBox(L"Format Device", L"Cannot Format. SlopDash and the Softmod Run From Here");
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// The drive list's A / X / Y (DeviceSceneProc calls this first): true if taken.
bool MemoryDeviceKey(HXUIOBJ obj, XUIMessage* msg, const std::string& root)
{
    if (msg->dwMessage == XM_KEYDOWN && !HudOpen() && !root.empty())
    {
        DWORD key = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        if (key == VK_PAD_Y)
        {
            g_optionsDrive = root;
            OpenOver(obj, L"memory", L"DeviceDetails.xur");
            msg->bHandled = TRUE;
            return true;
        }

    }
    return false;
}

void OpenCategories(HXUIOBJ from, const std::string& root)
{
    storage::Scan(root);
    OpenOver(from, L"memory", L"Categories.xur");
}
