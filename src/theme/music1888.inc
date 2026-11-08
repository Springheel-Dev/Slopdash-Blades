// --- Music (Media > Music) --------------------------------------------------------
// 1888's music area, on its
// own screens and in its own words (music#Strings.xus):
//   Hard Drive, Current Disc, Portable Device (1000_MusicMain) find the songs
//   there (platform/music.h) and open the library (1003_IndividualDevice,
//   MusicLibraryScene): Albums, Artists, Saved Playlists, Songs, Genres, each
//   listed in the side panel 1888 names for it (1003_MediaContainerList or
//   1003_SongList). Y Play All Music, X Music Player.
//   An album, genre or saved playlist (1005_MediaContainerOptions), an artist
//   (1006_IndividualArtistMain) or a song (1016_IndividualSongMain): Play,
//   Add to Current Playlist, Delete (a box first, 1888's words), and for a
//   playlist Rename; their songs listed beside.
//   Music Player (1028_NowPlaying): the current playlist, the song playing,
//   previous / play-pause / stop / next / shuffle, "Edit or Save Playlist"
//   (1030_EditPlaylist: move up, move down, remove; Save Playlist, Clear All).
// Computer streams nothing here. Included by blades1888.cpp (namespace launch).
struct MusicRoot { const wchar_t* button; const wchar_t* name; const char* root; int depth; };
const MusicRoot kMusicRoots[] = {
    { L"navOption1", L"Hard Drive", "hdd:", 4 },
    { L"navOption3", L"Current Disc", "dvd:", 4 },
    { L"navOption4", L"Portable Device", "usb:", 4 },
};
std::wstring g_musicSource;
const DWORD kMusicTimer = 0x4D555331;           // "MUS1": once a music screen's parts exist
const DWORD kMusicAskTimer = 0x4D555332;        // "MUS2": a box or keyboard answered
const DWORD kNowPlayingTimer = 0x4D555333;      // "MUS3": the player's state, kept current

// The library's views, by the buttons that show them (1888's order).
const wchar_t* const kViewButtons[] = { L"btnAlbums", L"btnArtists", L"btnPlaylists", L"btnSongs", L"btnGenres" };
const wchar_t* const kNoneFound[] = { L"No albums found", L"No artists found", L"No playlists found",
                                      L"No songs found", L"No genres found" };
musiclib::View g_musicView = musiclib::kAlbums;  // the library's panel now
// Which songs or groups each list shows, set as it's made.
std::map<HXUIOBJ, std::vector<size_t> > g_songLists;
std::map<HXUIOBJ, musiclib::View> g_groupLists;
std::vector<size_t> g_nextSongs;                // for the next song list made
// The group an options screen is for (1005, 1006) and the song (1016).
musiclib::View g_pickView = musiclib::kAlbums;
size_t g_pickGroup = 0, g_pickSong = 0;

bool IsWithin(HXUIOBJ obj, HXUIOBJ ancestor)
{
    for (HXUIOBJ p = obj; p; XuiElementGetParent(p, &p))
        if (p == ancestor)
            return true;
    return false;
}

std::wstring Counted(size_t n, const wchar_t* one, const wchar_t* many)
{
    wchar_t b[48];
    _snwprintf_s(b, 48, _TRUNCATE, L"%u %s", (unsigned)n, n == 1 ? one : many);
    return b;
}

const musiclib::Group* PickedGroup()
{
    const std::vector<musiclib::Group>& g = music::Groups(g_pickView);
    return g_pickGroup < g.size() ? &g[g_pickGroup] : NULL;
}

// A box or the keyboard, answered later (kMusicAskTimer on the screen that asked).
enum MusicAsk { kAskNone, kAskDeleteGroup, kAskDeleteSong, kAskDeletePlaylist, kAskSaveName, kAskReplace,
                kAskRename, kAskSomeLeftOut };
struct MusicAsking
{
    MusicAsk what;
    HXUIOBJ scene;
    XOVERLAPPED ov;
    MESSAGEBOX_RESULT result;
    WCHAR text[64];
    std::wstring name;                          // a playlist's name, between the asks
} g_ask = { kAskNone, NULL };

void AskYesNo(HXUIOBJ scene, MusicAsk what, const wchar_t* title, const wchar_t* text)
{
    static LPCWSTR buttons[] = { L"Yes", L"No" };
    int user = SignedInUser();
    ZeroMemory(&g_ask.ov, sizeof(g_ask.ov));
    DWORD r = XShowMessageBoxUI(user >= 0 ? user : 0, title, text, 2, buttons, 1, XMB_WARNINGICON, &g_ask.result,
                                &g_ask.ov);
    if (r != ERROR_IO_PENDING && r != ERROR_SUCCESS)
        return;
    BoxOpened(&g_ask.ov, r);
    g_ask.what = what;
    g_ask.scene = scene;
    XuiSetTimer(scene, kMusicAskTimer, 100);
}

void AskName(HXUIOBJ scene, MusicAsk what, const wchar_t* start, const wchar_t* title, const wchar_t* text)
{
    int user = SignedInUser();
    ZeroMemory(&g_ask.ov, sizeof(g_ask.ov));
    g_ask.text[0] = 0;
    DWORD r = ShowKeyboard(user < 0 ? 0 : user, VKBD_DEFAULT, start, title, text, g_ask.text, 48, &g_ask.ov);
    if (r != ERROR_IO_PENDING && r != ERROR_SUCCESS)
        return;
    g_ask.what = what;
    g_ask.scene = scene;
    XuiSetTimer(scene, kMusicAskTimer, 100);
}

void SaveQueue(HXUIOBJ scene, const std::wstring& name, bool replace)
{
    music::SaveResult r = music::SavePlaylist(name, replace);
    if (r == music::kExists)
    {
        wchar_t b[200];
        _snwprintf_s(b, 200, _TRUNCATE, L"The saved playlist '%s' already exists. Do you want to replace it?",
                     name.c_str());
        g_ask.name = name;
        AskYesNo(scene, kAskReplace, L"Playlist Already Exists", b);
    }
    else if (r == music::kNothingToSave)
        SayBox(L"Can't Save Playlist", L"Saved playlists can only contain songs from the hard drive.");
    else if (r == music::kSaveFailed)
        SayBox(L"Can't Save Playlist", L"Cannot Save");
    else if (r == music::kSomeLeftOut)
        SayBox(L"Save Playlist", L"Saved playlists can only contain songs from the hard drive. Some of your songs "
                                 L"were not saved.");
}

void GoBack(HXUIOBJ scene)
{
    HXUIOBJ back = XuiSceneGetBackScene(scene);
    if (back)
        XuiSceneNavigateBack(scene, back, XUSER_INDEX_FOCUS);
}

// A box or the keyboard answered: what it was for.
void MusicAnswered(HXUIOBJ scene)
{
    if (g_ask.what == kAskNone || !XHasOverlappedIoCompleted(&g_ask.ov))
        return;
    XuiKillTimer(scene, kMusicAskTimer);
    MusicAsk what = g_ask.what;
    g_ask.what = kAskNone;
    bool ok = XGetOverlappedExtendedError(&g_ask.ov) == ERROR_SUCCESS;
    bool yes = ok && g_ask.result.dwButtonPressed == 0;
    std::wstring typed = ok ? g_ask.text : L"";
    if (what == kAskDeleteGroup && yes)
    {
        if (const musiclib::Group* g = PickedGroup())
            music::DeleteSongs(g->songs);
        GoBack(scene);
    }
    else if (what == kAskDeleteSong && yes)
    {
        music::DeleteSongs(std::vector<size_t>(1, g_pickSong));
        GoBack(scene);
    }
    else if (what == kAskDeletePlaylist && yes)
    {
        if (const musiclib::Group* g = PickedGroup())
            music::DeletePlaylist(g->name);
        GoBack(scene);
    }
    else if (what == kAskSaveName && ok)
    {
        if (typed.empty())
            SayBox(L"Name Required", L"Enter a name for your playlist.");
        else
            SaveQueue(scene, typed, false);
    }
    else if (what == kAskReplace && yes)
        SaveQueue(scene, g_ask.name, true);
    else if (what == kAskRename && ok && !typed.empty())
    {
        if (const musiclib::Group* g = PickedGroup())
            if (music::RenamePlaylist(g->name, typed))
                SetText(scene, L"labelHeader", typed.c_str());
    }
}

// The options screens' lists (XuiCommonList, no items in the file): filled here.
void FillOptions(HXUIOBJ scene, const std::vector<std::wstring>& items)
{
    HXUIOBJ list = Child(scene, L"listOptions");
    if (!list)
        return;
    SetListCount(list, (int)items.size());
    for (size_t i = 0; i < items.size(); i++)
        XuiListSetText(list, (int)i, items[i].c_str());
}

std::vector<std::wstring> GroupOptions(musiclib::View view)
{
    std::vector<std::wstring> o;
    o.push_back(view == musiclib::kAlbums ? L"Play Album" : view == musiclib::kGenres ? L"Play Genre"
                : view == musiclib::kArtists ? L"Play Artist" : L"Play Saved Playlist");
    o.push_back(L"Add to Current Playlist");
    if (view == musiclib::kAlbums)
        o.push_back(L"Delete Album");
    else if (view == musiclib::kPlaylists)
    {
        o.push_back(L"Rename Saved Playlist");
        o.push_back(L"Delete Saved Playlist");
    }
    return o;
}

void OpenNowPlaying(HXUIOBJ from)
{
    OpenOver(from, L"music", L"1028_NowPlaying.xur");
}

HRESULT MusicMainProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X && !HudOpen())
    {
        OpenNowPlaying(obj);
        msg->bHandled = TRUE;
        return S_OK;
    }
    if (msg->dwMessage != XM_NOTIFY || ((XUINotify*)msg->pvData)->dwNotify != XN_PRESS || HudOpen())
        return S_OK;
    std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
    if (id == L"navOption2")
    {
        SayBox(L"Computer", L"Cannot Connect. Not Supported");
        msg->bHandled = TRUE;
    }
    else if (id == L"navMusicPlayer")
    {
        OpenNowPlaying(obj);
        msg->bHandled = TRUE;
    }
    for (size_t i = 0; i < ARRAYSIZE(kMusicRoots); i++)
        if (id == kMusicRoots[i].button)
        {
            music::Find(kMusicRoots[i].root, kMusicRoots[i].depth);
            g_musicSource = kMusicRoots[i].name;
            OpenOver(obj, L"music", L"1003_IndividualDevice.xur");
            msg->bHandled = TRUE;
        }
    return S_OK;
}

// A on a view's button (Albums...): into its list, as Right does. The buttons
// are nav buttons in a plain container scene, and XuiScene takes a nav
// button's press itself (it navigates to the button's link: these have none),
// so neither the press nor the key gets to the library's class; XUI's message
// filter sees the key first.
PFN_MESSAGE_FILTER g_musicPrevFilter = NULL;
bool g_musicFilterOn = false;
HXUIOBJ g_musicLibrary = NULL;

HRESULT APIENTRY MusicKeyFilter(HXUIOBJ obj, XUIMessage* msg)
{
    if (msg->dwMessage == XM_KEYDOWN && g_musicLibrary && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_A &&
        !HudOpen())
    {
        std::wstring id = ElementId(obj);
        for (int v = 0; v < 5; v++)
            if (id == kViewButtons[v] && IsWithin(obj, g_musicLibrary))
            {
                HXUIOBJ list = Find(Child(g_musicLibrary, L"metaPanelScene"), L"listItems");
                bool any = v == musiclib::kSongs ? music::Count() > 0 : !music::Groups((musiclib::View)v).empty();
                if (list && any)
                    XuiElementSetFocus(list);
                msg->bHandled = TRUE;
                return S_OK;
            }
    }
    return g_musicPrevFilter ? g_musicPrevFilter(obj, msg) : S_OK;
}

// The library (1003_IndividualDevice): the focused view's list comes up in the
// side panel (DashScene builds it from the screen's own list of panels).
HRESULT MusicLibraryProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
    {
        g_musicLibrary = obj;
        if (!g_musicFilterOn)
        {
            g_musicPrevFilter = XuiSetMessageFilter(MusicKeyFilter);
            g_musicFilterOn = true;
        }
        XuiSetTimer(obj, kMusicTimer, 30);
    }
    else if (msg->dwMessage == XM_DESTROY && obj == g_musicLibrary)
        g_musicLibrary = NULL;
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        SetText(obj, L"labelHeader", g_musicSource.c_str());
        Show(obj, L"btnY", music::Count() > 0);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_SET_FOCUS)
    {
        // (before DashScene builds the panel: its list reads the view as it's made)
        std::wstring id = ElementId(((XUINotify*)msg->pvData)->hObjSource);
        for (int v = 0; v < 5; v++)
            if (id == kViewButtons[v])
            {
                g_musicView = (musiclib::View)v;
                if (v == musiclib::kSongs)
                    g_nextSongs = music::Groups(musiclib::kSongs)[0].songs;
            }
    }
    else if (msg->dwMessage == XM_KEYDOWN && !HudOpen())
    {
        DWORD key = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        if (key == VK_PAD_Y && music::Count())
        {
            if (music::Play(0))
                OpenNowPlaying(obj);
            else
                SayBox(g_musicSource.c_str(), L"Cannot Play");
            msg->bHandled = TRUE;
        }
        else if (key == VK_PAD_X)
        {
            OpenNowPlaying(obj);
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// A list scene (1003_MediaContainerList / 1003_SongList): its count line and
// empty state, once its parts exist.
void FillListScene(HXUIOBJ scene, size_t n, const std::wstring& count, const wchar_t* none)
{
    SetText(scene, L"labelItemCount", n ? count.c_str() : L"");
    Show(scene, L"labelLoadingText", false);
    Show(scene, L"labelLoadingAnimation", false);
    SetText(scene, L"labelNoResultsText", none);
    Show(scene, L"labelNoResultsText", n == 0);
    if (HXUIOBJ list = Child(scene, L"listItems"))
        XuiElementSetShow(list, n > 0);
}

HRESULT ContainerListSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"listItems");
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        musiclib::View v = list && g_groupLists.count(list) ? g_groupLists[list] : g_musicView;
        size_t n = music::Groups(v).size();
        static const wchar_t* const kOne[] = { L"album", L"artist", L"playlist", L"song", L"genre" };
        static const wchar_t* const kMany[] = { L"albums", L"artists", L"playlists", L"songs", L"genres" };
        FillListScene(obj, n, Counted(n, kOne[v], kMany[v]), kNoneFound[v]);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && list && !HudOpen() &&
             IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
    {
        int at = XuiListGetCurSel(list, NULL);
        musiclib::View v = g_groupLists.count(list) ? g_groupLists[list] : g_musicView;
        if (at >= 0 && (size_t)at < music::Groups(v).size())
        {
            g_pickView = v;
            g_pickGroup = (size_t)at;
            g_nextSongs = music::Groups(v)[at].songs;
            OpenOver(obj, L"music", v == musiclib::kArtists ? L"1006_IndividualArtistMain.xur"
                                                              : L"1005_MediaContainerOptions.xur");
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}

HRESULT SongListSceneProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    HXUIOBJ list = Child(obj, L"listItems");
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        size_t n = list && g_songLists.count(list) ? g_songLists[list].size() : 0;
        FillListScene(obj, n, Counted(n, L"song", L"songs"), L"No songs found");
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && list && !HudOpen() &&
             IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
    {
        int at = XuiListGetCurSel(list, NULL);
        const std::vector<size_t>& songs = g_songLists[list];
        if (at >= 0 && (size_t)at < songs.size())
        {
            g_pickSong = songs[at];
            OpenOver(obj, L"music", L"1016_IndividualSongMain.xur");
        }
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// The lists themselves (row visual btn_2ndList: text 0 on the left, text 1 a
// narrow right-hand column, text 2 a full-width line over text 0's place,
// and a picture on the right over text 1's): groups (the name; an album's
// artist or a group's song count on the right) and songs (title; artist).
// Text 2 and the picture stay empty: filled, they drew over the others.
HRESULT ContainerListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        g_groupLists[obj] = g_musicView;
    else if (msg->dwMessage == XM_DESTROY)
        g_groupLists.erase(obj);
    musiclib::View v = g_groupLists.count(obj) ? g_groupLists[obj] : g_musicView;
    const std::vector<musiclib::Group>& groups = music::Groups(v);
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)groups.size();
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
            if (d->iItem >= 0 && (size_t)d->iItem < groups.size())
            {
                const musiclib::Group& g = groups[d->iItem];
                if (d->iData == 0)
                    held = g.name;
                else if (d->iData == 1)
                    held = v == musiclib::kAlbums && !g.detail.empty() ? g.detail
                                                                       : Counted(g.songs.size(), L"song", L"songs");
            }
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

HRESULT SongListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        g_songLists[obj] = g_nextSongs;
    else if (msg->dwMessage == XM_DESTROY)
        g_songLists.erase(obj);
    const std::vector<size_t>& songs = g_songLists[obj];
    if (msg->dwMessage == XM_GET_ITEMCOUNT)
    {
        XUIMessageGetItemCount* d = (XUIMessageGetItemCount*)msg->pvData;
        if (d->nType == XUI_ITEMCOUNT_ALL)
        {
            d->cItems = (int)songs.size();
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
            if (d->iItem >= 0 && (size_t)d->iItem < songs.size())
            {
                const musiclib::Song& s = music::SongAt(songs[d->iItem]);
                held = d->iData == 0 ? s.title : d->iData == 1 ? s.artist : std::wstring();
            }
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// The songs of the group or artist, beside its options: 1888's song list in
// the screen's container.
void AddSongList(HXUIOBJ scene, const wchar_t* container)
{
    HXUIOBJ c = Child(scene, container), list = NULL;
    if (c)
        AddScene(c, L"music", L"1003_SongList.xur", &list);
}

// An album, genre or saved playlist (1005) and an artist (1006).
HRESULT GroupOptionsProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    bool artist = g_pickView == musiclib::kArtists;
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        const musiclib::Group* g = PickedGroup();
        SetText(obj, L"labelHeader", g ? g->name.c_str() : L"");
        Show(obj, L"labelAsyncWorkText", false);
        Show(obj, L"labelAsyncWorkAnimation", false);
        Show(obj, L"btnY", false);
        FillOptions(obj, GroupOptions(g_pickView));     // (an artist's View Songs: the list beside)
        AddSongList(obj, artist ? L"sceneAlbumListContainer" : L"sceneSongListContainer");
        if (HXUIOBJ list = Child(obj, L"listOptions"))
            XuiElementSetFocus(list);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicAskTimer)
    {
        MusicAnswered(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X && !HudOpen())
    {
        OpenNowPlaying(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ list = Child(obj, L"listOptions");
        if (!list || !IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
            return S_OK;
        const musiclib::Group* g = PickedGroup();
        int at = XuiListGetCurSel(list, NULL);
        msg->bHandled = TRUE;
        if (!g)
            return S_OK;
        if (at == 0)
        {
            if (music::Play(g->songs, 0))
                OpenNowPlaying(obj);
            else
                SayBox(g->name.c_str(), L"Cannot Play");
        }
        else if (at == 1)
            music::Add(g->songs);
        else if (at == 2 && g_pickView == musiclib::kAlbums)
            AskYesNo(obj, kAskDeleteGroup, L"Delete Album",
                     L"Are you sure you want to delete this album? This will permanently delete it from the hard drive.");
        else if (at == 2 && g_pickView == musiclib::kPlaylists)
            AskName(obj, kAskRename, g->name.c_str(), L"Rename Saved Playlist", L"Enter a new name for your playlist.");
        else if (at == 3 && g_pickView == musiclib::kPlaylists)
            AskYesNo(obj, kAskDeletePlaylist, L"Delete Saved Playlist",
                     L"Are you sure you want to delete this playlist? This will permanently delete it from the hard "
                     L"drive.");
    }
    return S_OK;
}

// A song (1016).
HRESULT SongOptionsProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        const musiclib::Song& s = music::SongAt(g_pickSong);
        SetText(obj, L"labelHeader", s.title.c_str());
        Show(obj, L"labelAsyncWorkText", false);
        Show(obj, L"labelAsyncWorkAnimation", false);
        Show(obj, L"btnY", false);
        // Beside the options, 1888's song details (Meta_Songs.xur).
        HXUIOBJ meta = NULL;
        if (HXUIOBJ c = Child(obj, L"sceneSongMetadataContainer"))
            AddScene(c, L"music", L"Meta_Songs.xur", &meta);
        if (meta)
        {
            SetText(meta, L"labelSongName", s.title.c_str());
            SetText(meta, L"labelArtistName", s.artist.empty() ? L"Unknown Artist" : s.artist.c_str());
            SetText(meta, L"labelAlbumName", s.album.empty() ? L"Unknown Album" : s.album.c_str());
            SetText(meta, L"labelGenre", s.genre.empty() ? L"Unknown Genre" : s.genre.c_str());
        }
        std::vector<std::wstring> o;
        o.push_back(L"Play Song");
        o.push_back(L"Add to Current Playlist");
        o.push_back(L"Delete Song");
        FillOptions(obj, o);
        if (HXUIOBJ list = Child(obj, L"listOptions"))
            XuiElementSetFocus(list);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicAskTimer)
    {
        MusicAnswered(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_KEYDOWN && ((XUIMessageInput*)msg->pvData)->dwKeyCode == VK_PAD_X && !HudOpen())
    {
        OpenNowPlaying(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ list = Child(obj, L"listOptions");
        if (!list || !IsWithin(((XUINotify*)msg->pvData)->hObjSource, list))
            return S_OK;
        int at = XuiListGetCurSel(list, NULL);
        std::vector<size_t> one(1, g_pickSong);
        if (at == 0)
        {
            if (music::Play(one, 0))
                OpenNowPlaying(obj);
            else
                SayBox(music::SongAt(g_pickSong).title.c_str(), L"Cannot Play");
        }
        else if (at == 1)
            music::Add(one);
        else if (at == 2)
            AskYesNo(obj, kAskDeleteSong, L"Delete Song",
                     L"Are you sure you want to delete this song? This will permanently delete it from the hard drive.");
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// --- Music Player (1028_NowPlaying) ---------------------------------------------------
void FillNowPlaying(HXUIOBJ obj)
{
    std::wstring title, artist;
    bool playing = music::NowPlaying(title, artist);
    int current = music::QueueCurrent();
    size_t n = music::QueueCount();
    SetText(obj, L"labelSongTitle", playing ? title.c_str() : L"");
    SetText(obj, L"labelArtistName", playing ? artist.c_str() : L"");
    wchar_t count[48] = L"";
    if (current >= 0)
        _snwprintf_s(count, 48, _TRUNCATE, L"%d of %d", current + 1, (int)n);
    SetText(obj, L"labelSongCount", count);
    SetText(obj, L"labelAsyncWorkText", n ? L"" : L"To play music, select a music source.");
    Show(obj, L"labelAsyncWorkAnimation", false);
    if (HXUIOBJ c = Find(obj, L"btnRipOrEdit"))
    {
        XuiElementSetShow(c, n > 0);
        XuiControlSetText(c, L"Edit or Save Playlist");
    }
    if (HXUIOBJ c = Find(obj, L"navY"))
        XuiControlSetText(c, L"");
    if (HXUIOBJ c = Find(obj, L"navX"))
        XuiControlSetText(c, L"");
    if (HXUIOBJ c = Find(obj, L"btnPlayPause"))
        SetStringProperty(c, L"ImagePath", music::PlayerState() == music::kPlaying ? L"common://btn_32x_Pause.png"
                                                                                    : L"common://btn_32x_Play.png");
    // The play order by its picture (the music package's: shuffle, or in order).
    if (HXUIOBJ c = Find(obj, L"btnPlaybackMode"))
        SetStringProperty(c, L"ImagePath", music::Shuffle() ? L"section://0,music#bt_shuffle.png"
                                                            : L"section://0,music#bt_normal.png");
    // The list follows the playlist (its count) and marks the song playing.
    if (HXUIOBJ list = Child(obj, L"listSongs"))
    {
        if (XuiListGetItemCount(list) != (int)n)
        {
            int had = XuiListGetItemCount(list);
            if (had > 0)
                XuiListDeleteItems(list, 0, had);
            if (n)
                XuiListInsertItems(list, 0, (int)n);
        }
        XuiElementSetShow(list, n > 0);
        static int s_marked = -2;
        if (current >= 0 && current != s_marked && !XuiElementTreeHasFocus(list))
            XuiListSetCurSel(list, current);
        s_marked = current;
    }
}

HRESULT NowPlayingProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        FillNowPlaying(obj);
        XuiSetTimer(obj, kNowPlayingTimer, 1000);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kNowPlayingTimer)
    {
        FillNowPlaying(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ source = ((XUINotify*)msg->pvData)->hObjSource;
        std::wstring id = ElementId(source);
        HXUIOBJ list = Child(obj, L"listSongs");
        if (list && IsWithin(source, list))
        {
            int at = XuiListGetCurSel(list, NULL);
            if (at >= 0)
                music::PlayQueueFrom((size_t)at);
        }
        else if (id == L"btnPrevious")
            music::Previous();
        else if (id == L"btnPlayPause")
            music::PlayPause();
        else if (id == L"btnStop")
            music::Stop();
        else if (id == L"btnNext")
            music::Next();
        else if (id == L"btnPlaybackMode")
            music::SetShuffle(!music::Shuffle());
        else if (id == L"btnRipOrEdit")
            OpenOver(obj, L"music", L"1030_EditPlaylist.xur");
        else
            return S_OK;
        FillNowPlaying(obj);
        msg->bHandled = TRUE;
    }
    return S_OK;
}

// The current playlist's songs (Music Player's list and Edit Current
// Playlist's): title and artist; Edit's icon columns their buttons' pictures.
HRESULT QueueListProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    int n = (int)music::QueueCount();
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
            std::wstring id = ElementId(obj);
            held.clear();
            if (d->iItem >= 0 && d->iItem < n && (id == L"listSongs"))
            {
                const musiclib::Song& s = music::QueueSong(d->iItem);
                // Edit Current Playlist's row (btn_PlaylistEdit) has text 2
                // only; the Music Player's (btn_2ndList) texts 0 and 1, its
                // text 2 drawing over text 0.
                HXUIOBJ parent = NULL;
                XuiElementGetParent(obj, &parent);
                if (ElementId(parent) == L"sceneSynchronisedListContainer")
                    held = d->iData == 2 ? (s.artist.empty() ? s.title : s.title + L" - " + s.artist) : std::wstring();
                else
                    held = d->iData == 0 ? s.title : d->iData == 1 ? s.artist : std::wstring();
            }
            d->szText = held.c_str();
            msg->bHandled = TRUE;
        }
    }
    else if (msg->dwMessage == XM_GET_SOURCE_IMAGE)
    {
        XUIMessageGetSourceImage* d = (XUIMessageGetSourceImage*)msg->pvData;
        if (d->bItemData)
        {
            std::wstring id = ElementId(obj);
            d->szPath = NULL;
            if (d->iItem >= 0 && d->iItem < n && d->iData == 0)
            {
                // (the music package's own: bt_up, bt_dn, bt_dlt)
                if (id == L"listMoveUpBtns")
                    d->szPath = L"section://0,music#bt_up.png";
                else if (id == L"listMoveDownBtns")
                    d->szPath = L"section://0,music#bt_dn.png";
                else if (id == L"listDeleteBtns")
                    d->szPath = L"section://0,music#bt_dlt.png";
                // (the songs' own rows: no picture. The Music Player's drew
                // over the artist, Edit's over its row's remove button.)
            }
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}

// Edit Current Playlist (1030): each row's song and its move up / move down /
// remove buttons (three lists kept in step), Save Playlist and Clear All.
void FillQueueEditor(HXUIOBJ obj, int select)
{
    int n = (int)music::QueueCount();
    static const wchar_t* const kLists[] = { L"listSongs", L"listMoveUpBtns", L"listMoveDownBtns", L"listDeleteBtns" };
    HXUIOBJ box = Child(obj, L"sceneSynchronisedListContainer");
    for (int i = 0; i < 4; i++)
        if (HXUIOBJ list = Child(box, kLists[i]))
        {
            int had = XuiListGetItemCount(list);
            if (had > 0)
                XuiListDeleteItems(list, 0, had);
            if (n)
            {
                XuiListInsertItems(list, 0, n);
                XuiListSetCurSel(list, select < 0 ? 0 : select >= n ? n - 1 : select);
            }
        }
    SetText(obj, L"labelSongCount", Counted((size_t)n, L"song", L"songs").c_str());
}

HRESULT QueueEditorProc(HXUIOBJ obj, XUIMessage* msg, const XUIElementPropVal*, const bool*)
{
    if (msg->dwMessage == XM_INIT)
        XuiSetTimer(obj, kMusicTimer, 30);
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicTimer)
    {
        XuiKillTimer(obj, kMusicTimer);
        std::vector<std::wstring> o;
        o.push_back(L"Save Playlist");
        o.push_back(L"Clear All");
        FillOptions(obj, o);
        FillQueueEditor(obj, 0);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kMusicAskTimer)
    {
        MusicAnswered(obj);
        msg->bHandled = TRUE;
    }
    else if (msg->dwMessage == XM_NOTIFY && ((XUINotify*)msg->pvData)->dwNotify == XN_PRESS && !HudOpen())
    {
        HXUIOBJ source = ((XUINotify*)msg->pvData)->hObjSource;
        HXUIOBJ options = Child(obj, L"listOptions"), box = Child(obj, L"sceneSynchronisedListContainer");
        if (options && IsWithin(source, options))
        {
            int at = XuiListGetCurSel(options, NULL);
            if (at == 0)
                AskName(obj, kAskSaveName, L"Playlist", L"Save Playlist", L"Enter a name for this playlist.");
            else if (at == 1)
            {
                music::QueueClear();
                FillQueueEditor(obj, 0);
            }
            msg->bHandled = TRUE;
            return S_OK;
        }
        static const wchar_t* const kButtons[] = { L"listMoveUpBtns", L"listMoveDownBtns", L"listDeleteBtns" };
        for (int b = 0; b < 3; b++)
        {
            HXUIOBJ list = Child(box, kButtons[b]);
            if (!list || !IsWithin(source, list))
                continue;
            int at = XuiListGetCurSel(list, NULL);
            if (at < 0)
                break;
            if (b == 0)
                music::QueueMove((size_t)at, -1);
            else if (b == 1)
                music::QueueMove((size_t)at, +1);
            else
                music::QueueRemove((size_t)at);
            FillQueueEditor(obj, b == 0 ? at - 1 : b == 1 ? at + 1 : at);
            if (HXUIOBJ again = Child(box, kButtons[b]))
                if (XuiListGetItemCount(again) > 0)
                    XuiElementSetFocus(again);
            msg->bHandled = TRUE;
        }
    }
    return S_OK;
}
