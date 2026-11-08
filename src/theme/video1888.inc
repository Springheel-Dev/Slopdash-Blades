// --- Video (Media > Videos) -------------------------------------------------------
// 1888's Video area: the
// drives' Windows Media videos (platform/videos.h) in 1888's list
// (videos#VideosMain.xur, VideosMainScene: title, size, and beside it the
// video's title, publisher and description), played full screen in 1888's
// black video screen (Video.xur, VideoScene) by the console's XMedia2 player
// (WMV9; platform/movie.h): A or Start pauses, B stops. The Media
// blade's Media (Pictures and Video as one entry, the user's 2026-10-15
// choice) has a Videos row for it, under Screenshots.
// Included by blades1888.cpp (namespace launch).
const DWORD kVideosTimer = 0x56494431;          // "VID1": once a video screen's parts exist
size_t g_videoAt = 0;                           // the video the list has, and plays

// The Media screen's Videos row: one more like its others, under Screenshots.
void AddVideosRow(HXUIOBJ scene)
{
    HXUIOBJ above = Child(scene, L"btnOption6"), before = Child(scene, L"btnOption5");
    if (!above || Child(scene, L"btnVideos"))
        return;
    HXUIOBJ row = NULL;
    if (FAILED(XuiCreateObject(L"XuiNavButton", &row)) || !row)
        return;
    SetStringProperty(row, L"Id", L"btnVideos");
    SetStringProperty(row, L"Visual", L"btn_1line_icon");
    float w = 0, h = 0;
    XuiElementGetBounds(above, &w, &h);
    XuiElementSetBounds(row, w, h);
    D3DXVECTOR3 a(0, 0, 0), b(0, 0, 0);
    XuiElementGetPosition(above, &a);
    if (before)
        XuiElementGetPosition(before, &b);
    D3DXVECTOR3 at(a.x, a.y + (before ? a.y - b.y : h), 0);
    XuiElementSetPosition(row, &at);
    XuiElementAddChild(scene, row);
    XuiControlSetText(row, L"Videos");
    SetStringProperty(row, L"ImagePath", L"section://0,mediabla#ico_64x_TrailersFolder_white.png");
    SetStringProperty(above, L"NavDown", L"btnVideos");
    SetStringProperty(row, L"NavUp", L"btnOption6");
}

bool OpenVideos(HXUIOBJ from)
{
    videos::Find();
    g_videoAt = 0;
    return OpenOver(from, L"videos", L"VideosMain.xur");
}

void ShowVideoDetails(HXUIOBJ scene)
{
    size_t n = videos::Count();
    wchar_t count[32] = L"";
    if (n)
        _snwprintf_s(count, 32, _TRUNCATE, L"%d of %d", (int)g_videoAt + 1, (int)n);
    SetText(scene, L"txtNumOfNum", count);
    HXUIOBJ meta = Child(scene, L"scnMetaData");
    if (meta)
        XuiElementSetShow(meta, n > 0);
    HXUIOBJ td = meta ? Child(meta, L"scnTD0") : NULL;
    if (!td || !n)
        return;
    const videos::Video& v = videos::At(g_videoAt);
    SetText(td, L"labVideoTitle", v.title.c_str());
    SetText(td, L"labPublisher", v.publisher.empty() ? L"Publisher Unknown" : v.publisher.c_str());
    SetText(td, L"labDescription", v.description.empty() ? L"No description available." : v.description.c_str());
}

HRESULT VideosMainProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"listVideos");
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kVideosTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kVideosTimer)
    {
        XuiKillTimer(obj, kVideosTimer);
        size_t n = videos::Count();
        SetText(obj, L"labMessage", n ? L"" : L"No videos found.");
        Show(obj, L"labMessage", n == 0);
        Show(obj, L"ctlLoading", false);
        if (list)
        {
            XuiElementSetShow(list, n > 0);
            if (n)
            {
                XuiListSetCurSel(list, 0);
                XuiElementSetFocus(list);
            }
        }
        ShowVideoDetails(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && list)
    {
        XUINotify* n = (XUINotify*)msg->pvData;
        if (n->dwNotify == XN_SELCHANGED && IsWithin(n->hObjSource, list))
        {
            int at = XuiListGetCurSel(list, NULL);
            if (at >= 0)
            {
                g_videoAt = (size_t)at;
                ShowVideoDetails(obj);
            }
        }
        else if (n->dwNotify == XN_PRESS && IsWithin(n->hObjSource, list) && !HudOpen())
        {
            int at = XuiListGetCurSel(list, NULL);
            if (at >= 0 && (size_t)at < videos::Count())
            {
                g_videoAt = (size_t)at;
                OpenOver(obj, L"videos", L"Video.xur");
            }
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// The list's rows (MetaListItem_Cost: text 0 the title, text 1 a right-hand
// column - the size here - and two small pictures, left empty).
HRESULT VideoListProc(HXUIOBJ, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    int n = (int)videos::Count();
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
                held = d->iData == 0 ? videos::At(d->iItem).title : d->iData == 1 ? videos::SizeText(d->iItem)
                                                                                 : std::wstring();
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            d->szPath = NULL;
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// The video, full screen: the console's player (platform/movie.h) draws it
// over the screen's black.
HRESULT VideoSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kVideosTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kVideosTimer)
    {
        if (!movie::Active())
        {
            // (first: the screen's parts exist now)
            const videos::Video& v = videos::At(g_videoAt);
            if (!movie::Start(v.path))
            {
                XuiKillTimer(obj, kVideosTimer);
                SayBox(v.title.c_str(), L"Cannot Play");
                GoBack(obj);
            }
            else
            {
                XuiElementSetFocus(obj);
                XuiSetTimer(obj, kVideosTimer, 250);    // (then: has it ended?)
            }
        }
        else if (movie::Ended())
        {
            XuiKillTimer(obj, kVideosTimer);
            movie::Stop();
            GoBack(obj);
        }
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_DESTROY)
        movie::Stop();
    else if (msg->dwMessage == XM_KEYDOWN && !HudOpen())
    {
        DWORD key = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        if (key == VK_PAD_A || key == VK_PAD_START)
        {
            movie::Pause(!movie::Paused());
            msg->bHandled = TRUE;
        }
        else if (key == VK_PAD_B || key == VK_PAD_BACK)
        {
            XuiKillTimer(obj, kVideosTimer);
            movie::Stop();
            GoBack(obj);
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}
