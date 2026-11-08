// Games Library views (Games Library > Y > Library View), each library its
// own (library_settings.h). "list" (List & Description) is the Blades screen as
// it is: the list, and the info panel beside it (the description, or the cover
// art). The others are built on that same screen, in its
// look: its green panel, the skin's label styles, the info pane's frame
// (Metapane) around the highlighted game, and the game covers SlopDash
// caches. The list stays in place, invisible, holding the focus and the
// selection; these views follow it, and turn left/right (and up/down in the
// grids) into moves along it.
//
// Included by blades.cpp, inside its anonymous namespace, after ShowSelectedGame.

struct Box
{
    float x, y, w, h, a;            // place, size, opacity
};

struct ViewItem
{
    HXUIOBJ obj;
    Box now, target;
    int game;                       // the game whose cover it shows (-1: none yet)
    bool placed;                    // put at `now` at least once (0.0.62: grid covers that
                                    // never moved were never placed, so they didn't show)
};

struct LibraryViewState
{
    std::string id;                 // the view on show ("" before the first)
    std::string stamp;              // ... and its options (LibraryViewStamp): redrawn when it changes
    HXUIOBJ scene;
    std::vector<ViewItem> covers;   // one per cover slot
    std::vector<HXUIOBJ> names;     // Index: one label per name slot
    ViewItem frame;                 // Metapane behind the highlighted cover
    std::vector<HXUIOBJ> made;      // everything the view created (removed on a switch)
    HXUIOBJ title, sub, body, count;   // count: the views' own centred "N of M"
    HXUIOBJ mask;                   // the blade's face: everything above is drawn in it, clipped
    int sel, top;                   // highlighted game; the first row (Index: column) shown
    bool animating;
    DWORD stepAt;                   // the last animation step's time
};
LibraryViewState g_lv;

const DWORD kViewAnimTimer = 0x56414E49;    // "VANI"
const float kCenterX = 560.0f;              // the screen's middle (1120 wide)

// The blade's shape is a mask (nothing spills over the blade's sides). The views draw
// inside the blade's face, clipped to it: measured on the console from 1888's
// Games blade with the library open (x 228..1729, y 128..895 of the 1920 x 1080
// picture). Its sides are nearly straight where the covers are (within 8 px).
const float kFaceX = 134.0f, kFaceY = 92.0f, kFaceW = 874.0f, kFaceH = 545.0f;
const int kFlowSide = 4;                    // covers each side in Cover Flow
float g_maskY = kFaceY;                     // the mask's top now (the grids' is lower)

// The grids: rows of covers sized to fill the views' area (x 145..975,
// y 152..578: the blade's face less the name at its foot), a cover's shape
// (0.73 wide per tall) kept. (From y 152: the highlight frame on the top
// row came up into the header band at 132.)
struct GridShape
{
    int cols, rows;
    float w, h, gx, gy, y0;
};
const float kGridTop = 152.0f, kGridHeight = 426.0f, kGridWidth = 830.0f;
const float kGridTitleY = 590.0f, kGridTitlePoints = 10.0f;     // the name at the blade's foot

GridShape MakeGrid(int rows, int cols)
{
    GridShape g;
    g.rows = rows;
    g.cols = cols;
    g.gy = rows >= 4 ? 10.0f : rows == 3 ? 12.0f : 22.0f;
    g.gx = cols >= 9 ? 12.0f : 18.0f;
    float h = (kGridHeight - (rows - 1) * g.gy) / rows;
    float byWidth = (kGridWidth - (cols - 1) * g.gx) / cols / 0.73f;
    g.h = h < byWidth ? h : byWidth;
    g.w = g.h * 0.73f;
    g.y0 = kGridTop + (kGridHeight - (rows * g.h + (rows - 1) * g.gy)) / 2;
    return g;
}

bool IsGrid(const std::string& v)
{
    return v == "grid" || v == "tiles";
}

// Tiles: rows rolling with the selection, 3 x 7 until the user picks
// another size.
GridShape Shape(const std::string& v)
{
    int rows = 2, cols = 5;
    if (v == "tiles")
        LibraryTilesSize(rows, cols);
    else
        LibraryGridSize(rows, cols);
    return MakeGrid(rows, cols);
}

bool IsListView(const std::string& v)
{
    return v.empty() || v == "list";
}

int WrapIndex(int i, int n)
{
    return n > 0 ? ((i % n) + n) % n : 0;
}

std::wstring CoverOrIcon(int game)
{
    std::wstring c = CoverLocator((size_t)game);
    return c.empty() ? IconLocator((size_t)game) : c;
}

void Apply(HXUIOBJ obj, const Box& b)
{
    D3DXVECTOR3 pos(b.x, b.y, 0);
    XuiElementSetPosition(obj, &pos);
    XuiElementSetBounds(obj, b.w, b.h);
    XuiElementSetOpacity(obj, b.a);
    XuiElementSetShow(obj, b.a > 0.01f);
}

// A view's own element at b (in the screen's design), inside the mask.
void Put(HXUIOBJ obj, const Box& b)
{
    Box in = b;
    if (g_lv.mask)
    {
        in.x -= kFaceX;
        in.y -= g_maskY;
    }
    Apply(obj, in);
}

// What the views draw goes in the mask (the screen itself without one).
HXUIOBJ g_viewParentOverride = NULL;     // (Launch's rows: their clip, while they're made)

HXUIOBJ ViewParent()
{
    if (g_viewParentOverride)
        return g_viewParentOverride;
    return g_lv.mask ? g_lv.mask : g_lv.scene;
}

// Shows a game's cover in a slot, only if it isn't already there: loading a
// cover (a JPEG) on every move made the views lag.
void SetCover(ViewItem& c, int game)
{
    if (c.game == game || !c.obj)
        return;
    c.game = game;
    XuiImageElementSetImagePath(c.obj, CoverOrIcon(game).c_str());
}

// --- Where each view puts things (in the screen's 1120 x 770 design) ----------------
// Cover Flow: the highlighted cover big in the middle, neighbours smaller and
// stacked away to each side, bottoms lined up. All solid: see-through covers
// over each other looked messy.
Box FlowBox(int o)
{
    static const float kScale[] = { 1.0f, 0.70f, 0.58f, 0.48f, 0.40f };
    int d = o < 0 ? -o : o;
    if (d > kFlowSide)
        d = kFlowSide + 1;
    float s = kScale[d > kFlowSide ? kFlowSide : d];
    Box b;
    b.w = 210 * s;
    b.h = 288 * s;
    float cx = d == 0 ? kCenterX : kCenterX + (o < 0 ? -1 : 1) * (200.0f + (d - 1) * 96.0f);
    b.x = cx - b.w / 2;
    b.y = 170 + (288 - b.h);
    b.a = d > kFlowSide ? 0.0f : 1.0f;
    return b;
}

// Grid and Tiles: cover k of the shown rows (row may be -1 or rows: Tiles' rows
// rolling in and out); the highlighted cover a little bigger and fully opaque.
Box GridBox(const GridShape& g, int col, int row, bool highlighted)
{
    const float x0 = kCenterX - (g.cols * g.w + (g.cols - 1) * g.gx) / 2;
    Box b;
    float s = highlighted ? 1.08f : 1.0f;
    b.w = g.w * s;
    b.h = g.h * s;
    b.x = x0 + col * (g.w + g.gx) - (b.w - g.w) / 2;
    b.y = g.y0 + row * (g.h + g.gy) - (b.h - g.h) / 2;
    b.a = row < 0 || row >= g.rows ? 0.0f : highlighted ? 1.0f : 0.75f;
    return b;
}

const Box kMarqueeCover = { 146, 160, 262, 358, 1.0f };    // (its frame inside the blade's face)

// Index: the names in columns, as small as the skin's text goes; small icons
// beside them if the user wants them.
const int kIndexRows = 21, kIndexCols = 3;
const float kIndexTop = 148.0f, kIndexRowH = 19.0f, kIndexColW = 284.0f, kIndexPoints = 8.0f, kIndexIcon = 16.0f;

Box IndexBox(int col, int row, bool icons)
{
    const float x0 = kCenterX - kIndexCols * kIndexColW / 2;
    Box b = { x0 + col * kIndexColW + (icons ? kIndexIcon + 6 : 0), kIndexTop + row * kIndexRowH,
              kIndexColW - 10 - (icons ? kIndexIcon + 6 : 0), kIndexRowH, 1.0f };
    return b;
}

Box Around(const Box& b, float margin)
{
    Box f = { b.x - margin, b.y - margin, b.w + 2 * margin, b.h + 2 * margin, b.a > 0 ? 1.0f : 0.0f };
    return f;
}

bool Visible(int o, int n)
{
    // Fewer games than slots: each game shown once.
    int d = o < 0 ? -o : o;
    return 2 * d < n || (n % 2 == 0 && o == n / 2);
}

// The text lines' places under the covers.
float TitleY(const std::string& v)
{
    if (IsGrid(v))
    {
        GridShape g = Shape(v);
        return g.y0 + g.rows * g.h + (g.rows - 1) * g.gy + 14;
    }
    return 470.0f;
}

// --- Building and updating --------------------------------------------------------
HXUIOBJ MakeElementIn(HXUIOBJ scene, const wchar_t* cls, const wchar_t* visual, std::vector<HXUIOBJ>& made)
{
    HXUIOBJ obj = NULL;
    if (FAILED(XuiCreateObject(cls, &obj)) || !obj)
        return NULL;
    if (visual)
        SetStringProperty(obj, L"Visual", visual);
    XuiElementAddChild(scene, obj);
    made.push_back(obj);
    return obj;
}

HXUIOBJ MakeElement(HXUIOBJ, const wchar_t* cls, const wchar_t* visual)
{
    return MakeElementIn(ViewParent(), cls, visual, g_lv.made);
}

HXUIOBJ MakeLabelIn(HXUIOBJ scene, const wchar_t* visual, float x, float y, float w, float h, float points,
                    std::vector<HXUIOBJ>& made)
{
    HXUIOBJ l = MakeElementIn(scene, L"XuiLabel", visual, made);
    if (!l)
        return NULL;
    D3DXVECTOR3 pos(x, y, 0);
    XuiElementSetPosition(l, &pos);
    XuiElementSetBounds(l, w, h);
    if (points > 0)
        SetFloatProperty(l, L"PointSize", points);
    return l;
}

HXUIOBJ MakeLabel(HXUIOBJ, const wchar_t* visual, float x, float y, float w, float h, float points)
{
    if (g_lv.mask)
    {
        x -= kFaceX;
        y -= g_maskY;
    }
    return MakeLabelIn(ViewParent(), visual, x, y, w, h, points, g_lv.made);
}

void DestroyAll(std::vector<HXUIOBJ>& made)
{
    for (size_t i = 0; i < made.size(); i++)
    {
        XuiElementUnlink(made[i]);
        XuiDestroyObject(made[i]);
    }
    made.clear();
}

void ClearLaunch();
bool LaunchAnimate(float k);

void ClearView()
{
    ClearLaunch();
    DestroyAll(g_lv.made);
    if (g_lv.mask)
    {
        XuiElementUnlink(g_lv.mask);
        XuiDestroyObject(g_lv.mask);
    }
    g_lv.mask = NULL;
    g_lv.covers.clear();
    g_lv.names.clear();
    g_lv.frame.obj = g_lv.title = g_lv.sub = g_lv.body = g_lv.count = NULL;
    g_lv.animating = false;
}

// The blade's face, its children clipped to it. The grids' mask is the covers'
// band (y 140..590, room for the highlight): Tiles' rows rolling in and out
// went over the header and the name at the foot.
void MakeMask(HXUIOBJ scene, float y, float h)
{
    HXUIOBJ m = NULL;
    if (FAILED(XuiCreateObject(L"XuiGroup", &m)) || !m)
        return;
    SetStringProperty(m, L"Id", L"rdViewMask");
    D3DXVECTOR3 pos(kFaceX, y, 0);
    XuiElementSetPosition(m, &pos);
    XuiElementSetBounds(m, kFaceW, h);
    g_maskY = y;
    SetBoolProperty(m, L"ClipChildren", true);
    XuiElementAddChild(scene, m);
    g_lv.mask = m;
}

void StartAnimation()
{
    if (!g_lv.animating && g_lv.scene)
    {
        g_lv.animating = true;
        g_lv.stepAt = GetTickCount();
        XuiSetTimer(g_lv.scene, kViewAnimTimer, 10);
    }
}

// One animation step: everything eases towards its target, by the time since
// the last step (a fixed share a step before 0.0.121: a late frame slowed the
// slide, then it jumped, and Cover Flow juddered). False when done.
bool StepAnimation()
{
    DWORD now = GetTickCount();
    float dt = (float)(now - g_lv.stepAt);
    g_lv.stepAt = now;
    if (dt > 100.0f)
        dt = 100.0f;
    // The share of the way left covered in dt: 26% each 60th of a second.
    float k = 1.0f - powf(0.74f, dt / 16.67f);
    bool moving = false;
    std::vector<ViewItem*> items;
    for (size_t i = 0; i < g_lv.covers.size(); i++)
        items.push_back(&g_lv.covers[i]);
    if (g_lv.frame.obj)
        items.push_back(&g_lv.frame);
    for (size_t i = 0; i < items.size(); i++)
    {
        Box& n = items[i]->now;
        const Box& t = items[i]->target;
        float* a[] = { &n.x, &n.y, &n.w, &n.h, &n.a };
        const float b[] = { t.x, t.y, t.w, t.h, t.a };
        bool changed = false;
        for (int c = 0; c < 5; c++)
        {
            float d = b[c] - *a[c];
            float limit = c == 4 ? 0.01f : 0.4f;
            if (d > limit || d < -limit)
            {
                *a[c] += d * k;
                changed = true;
            }
            else if (*a[c] != b[c])
            {
                *a[c] = b[c];
                changed = true;
            }
        }
        if (changed || !items[i]->placed)
        {
            Put(items[i]->obj, n);      // only what moved (and everything once)
            items[i]->placed = true;
            if (changed)
                moving = true;
        }
    }
    if (LaunchAnimate(k))
        moving = true;
    return moving;
}

// The text under (or beside) the covers.
void ShowViewText(int game)
{
    const std::vector<library::Game>& games = Games();
    if (game < 0 || game >= (int)games.size())
    {
        if (g_lv.title)
            XuiControlSetText(g_lv.title, games.empty() ? L"No games found" : L"");
        return;
    }
    const library::Game& g = games[game];
    if (g_lv.count)
    {
        wchar_t b[32];
        _snwprintf_s(b, 32, _TRUNCATE, L"%d of %d", game + 1, (int)games.size());
        XuiControlSetText(g_lv.count, b);
    }
    if (!g_lv.title && !g_lv.sub && !g_lv.body)
        return;
    catalog::Details d;
    bool details = GameDetails((size_t)game, d);
    std::wstring line = details ? (d.developer.empty() ? d.publisher : d.developer) : KindText(g.kind);
    if (details && !d.genre.empty())
        line += (line.empty() ? L"" : L"  |  ") + d.genre;
    if (details && !d.released.empty())
        line += (line.empty() ? L"" : L"  |  ") + d.released;
    if (g_lv.title)
        XuiControlSetText(g_lv.title, g.name.c_str());
    if (g_lv.sub)
        XuiControlSetText(g_lv.sub, line.c_str());
    if (g_lv.body)
    {
        std::wstring about = details ? d.description : (IsBoilerplate(g.description) ? L"" : g.description);
        XuiControlSetText(g_lv.body, about.c_str());
    }
}

void RaiseText()
{
    HXUIOBJ text[] = { g_lv.title, g_lv.sub, g_lv.count };
    for (int t = 0; t < 3; t++)
        if (text[t])
        {
            XuiElementUnlink(text[t]);
            XuiElementAddChild(ViewParent(), text[t]);
        }
}

// Index: the names on show from column `top` on, the highlight's frame.
void ShowIndex(int sel, int n)
{
    int perPage = kIndexRows * kIndexCols;
    int col = sel / kIndexRows;
    if (col < g_lv.top)
        g_lv.top = col;
    if (col >= g_lv.top + kIndexCols)
        g_lv.top = col - kIndexCols + 1;
    bool icons = IndexIcons();
    const std::vector<library::Game>& games = Games();
    for (int k = 0; k < perPage && k < (int)g_lv.names.size(); k++)
    {
        int game = g_lv.top * kIndexRows + k;
        bool shown = game < n;
        XuiControlSetText(g_lv.names[k], shown ? games[game].name.c_str() : L"");
        if (icons && k < (int)g_lv.covers.size())
        {
            ViewItem& c = g_lv.covers[k];
            if (shown)
                SetCover(c, game);
            Box at = IndexBox(k / kIndexRows, k % kIndexRows, true);
            Box ic = { at.x - kIndexIcon - 6, at.y + (kIndexRowH - kIndexIcon) / 2, kIndexIcon, kIndexIcon,
                       shown ? 1.0f : 0.0f };
            c.target = c.now = ic;
            c.placed = false;
        }
    }
    int k = sel - g_lv.top * kIndexRows;
    Box at = IndexBox(k / kIndexRows, k % kIndexRows, icons);
    if (icons)
    {
        at.x -= kIndexIcon + 6;
        at.w += kIndexIcon + 6;
    }
    g_lv.frame.target = Around(at, 3);
    if (!g_lv.frame.now.w)
        g_lv.frame.now = g_lv.frame.target;
}

// --- Launch: the 2005 "Select a Game" screen -----------------
// The library as the Xbox 360 retail kiosk showed games in 2005: a list down the
// left (the game's tile, name, maker, and what the game is on the right), and the
// highlighted game's screenshot, story, age rating and Available Content on the
// right. That screen isn't in any dashboard file, so it's made of 1888's own
// parts (checked 2026-11-02): the list on skin Metapane (the darker panel, its
// edge the divider), each row the All Games list's row visual (skin
// btn_twoline-icon_RightText, 855 x 70, drawn at the list's width; its Focus
// state, shine and all, played on the visual itself on the highlighted row; its
// XuiImage1, sharedres GScore.png, hidden: it stood over the row's text), the
// scroll arrows its list's (scr_ScrollEndUp/Down), the logo dashcomm's
// xboxLogo.png, the rating dashcomm's ESRB_*.png from the game's own xex header
// (scanner GameEsrb), Available Content's picture gamesbla's ico_32x_demos (a
// disc). The screenshot is the catalog's (fetcher.cpp shot.jpg), else the cover.
// Sizes as the reference picture's (1200 x 675: list 197..645, screenshot
// 668..1000 x 132..318 in a white edge, story under it in dark bold, ESRB at
// 668,491, Available Content's box 802..1000 x 494..551), at 1.036 design units
// a pixel from the list's top left (152, 150). The details: the highlighted row
// is a tab, out of the list's panel
// to the left, the panel's edges going round it (the panel in two, above and
// below it); each row's "XBOX 360" is the wordmark, white, on a plaque (a light
// edge above, a shadow below); the header's logo is the orb and the wordmark side
// by side. 1888 has no such logo or tag: both are dashcomm xboxLogo.png's halves
// (main.cpp slopdash://crop/: the orb 90,5 125 x 123, the wordmark 6,131 291 x 60).
// Row text dark, as the reference's.
const int kLaunchRows = 6;
const float kLaunchX = 152.0f, kLaunchTop = 150.0f, kLaunchRowH = 64.0f, kLaunchW = 466.0f;
// A row's parts (the reference, 2026-11-02: art 55 square 3 in, text 68 in, the
// title and the company the same size, baselines 22.7 apart).
const float kLaunchArt = 54.0f, kLaunchTextX = 66.0f, kLaunchTextPt = 13.5f, kLaunchLineGap = 24.0f;
const float kLaunchTitleY = 7.0f;                  // (the title label's top in its row)
const float kLaunchRowVisualW = 855.0f, kLaunchRowVisualH = 70.0f;     // (the row visual's own size)
const float kLaunchRightX = 640.0f, kLaunchRightW = 344.0f;
const float kLaunchShotH = 193.0f;
const unsigned kLaunchStoryColour = 0xE60F0F0F;    // (1888's own dark text: skin Label_Body's)
const float kLaunchTabOut = 8.0f;
// The rows in a clip of their own:
// six whole rows and part of a seventh, its foot cut off; and a slot above and
// one more below for sliding (slot i is row i - 1).
const int kLaunchSlots = kLaunchRows + 3;
const float kLaunchClipFoot = 150.0f + kLaunchRows * 64.0f + 29.0f;  // (kLaunchTop + 6 rows + 0.45 of one)
const float kLaunchClipX = 152.0f - 8.0f - 2.0f;                       // (room for the tab's step out)                  // (how far the highlighted row stands out to the left)
const float kLaunchTagW = 83.0f, kLaunchTagH = 29.0f;
const wchar_t* const kLaunchOrb = L"slopdash://crop/90,5,125,123,0/";
const wchar_t* const kLaunchWordmark = L"slopdash://crop/6,131,291,60,0/";
const wchar_t* const kLaunchWordmarkWhite = L"slopdash://crop/6,131,291,60,1/";

struct LaunchRow
{
    HXUIOBJ bg, visual, cover, name, maker, kind, mark;
    std::vector<HXUIOBJ> tag;       // the plaque under the kind: fill, light top, shadow
    int game;                       // shown in it now (-1: none)
    int focus;                      // its visual's state: 1 Focus, 0 Normal, -1 not set yet
    int tab;                        // (unused)
    float out, outTo;               // how far it stands out to the left as the tab: now, and going to
    bool shown;
};
struct LaunchState
{
    std::vector<LaunchRow> rows;
    HXUIOBJ shot, desc, rating, contentLabel, disc, logo, orb, up, down;
    HXUIOBJ paneAbove, paneBelow;   // (unused: the panel was Metapane)
    std::vector<HXUIOBJ> edges;     // the body's corners, edges and shadow, above and below the tab (LaunchPanes)
    std::vector<std::wstring> edgePaths;   // what each shows now (set again, each was made again: 200 ms a press)
    std::vector<HXUIOBJ> contentBox;   // Available Content's box: fill and edges
    HXUIOBJ clip;                   // the rows' clip (kLaunchClipX, kLaunchTop .. kLaunchClipFoot)
    int top;
    int shotGame;
    // Moving smoothly:
    // the rows slide by `slide` (design units, easing to 0) after the list
    // scrolls, the tab and the body's edges glide to tabTo, and the right-hand
    // side - a picture to load, files to read - waits till the highlight rests.
    float slide, tab, tabTo;
    int rowsShown, n;
    bool placed;                    // (once placed, moves animate; the first doesn't)
    int sideWanted;                 // the game whose side is to show when the highlight rests (-1 none)
    DWORD sideAt;
    int ratingShown;                // the ESRB value shown (-2: none yet)
    std::wstring header;            // the screen's own header, given back when the view changes
};
LaunchState g_launch;

std::wstring DashFile(const wchar_t* package, const wchar_t* file)
{
    std::wstring base;
    return PackageLocator(package, base) ? base + file : L"";
}

HXUIOBJ LaunchImage(HXUIOBJ scene, const Box& b, const std::wstring& path)
{
    HXUIOBJ img = MakeElement(scene, L"XuiImage", NULL);
    if (!img)
        return NULL;
    SetUintProperty(img, L"SizeMode", 0x10);       // fit, keeping its shape
    if (!path.empty())
        XuiImageElementSetImagePath(img, path.c_str());
    Put(img, b);
    return img;
}

bool SetColourProperty(HXUIOBJ obj, const wchar_t* name, unsigned argb)
{
    DWORD id = 0;
    if (!obj || FAILED(XuiObjectGetPropertyId(obj, name, &id)))
        return false;
    XUIElementPropVal v;
    XUIElementPropVal_SetColorFromUint(&v, argb);
    return SUCCEEDED(XuiObjectSetProperty(obj, id, 0, &v));
}

// Text presenters under obj, at any depth: dark (and bold).
int DarkTextIn(HXUIOBJ obj, unsigned argb, bool bold, unsigned style)
{
    int n = 0;
    HXUIOBJ c = NULL;
    for (XuiElementGetFirstChild(obj, &c); c; XuiElementGetNext(c, &c))
    {
        if (SetColourProperty(c, L"TextColor", argb))
        {
            DWORD id = 0;
            XUIElementPropVal v;
            if ((bold || style) && SUCCEEDED(XuiObjectGetPropertyId(c, L"TextStyle", &id)) &&
                SUCCEEDED(XuiObjectGetProperty(c, id, 0, &v)))
                SetUintProperty(c, L"TextStyle", v.uVal | style | (bold ? XUI_FONT_STYLE_BOLD : 0));
            SetColourProperty(c, L"DropShadowColor", 0x2EFFFFFF);  // (a pale lift, as 1888's dark text has)
            n++;
        }
        n += DarkTextIn(c, argb, bold, style);
    }
    return n;
}

// A label's text dark (and bold): on its visual's text presenters, where the
// skin's colour is (set on the label itself it stayed white, 2026-11-02; a
// row's in Label_Body_1line stayed white even so: they're Label_BodySmall, one
// line, `style` adding XUI_FONT_STYLE_ flags).
int LaunchDarkText(HXUIOBJ label, unsigned argb, bool bold, unsigned style = 0)
{
    HXUIOBJ visual = NULL;
    if (label && SUCCEEDED(XuiControlGetVisual(label, &visual)) && visual)
        return DarkTextIn(visual, argb, bold, style);
    return -1;
}

// A story cut to what its box holds (about seven lines): at a word, then "...".
std::wstring LaunchStory(const std::wstring& text)
{
    size_t caps = 0;
    for (size_t i = 0; i < text.size(); i++)
        caps += text[i] >= L'A' && text[i] <= L'Z';
    const size_t kMost = caps * 2 > text.size() ? 170 : 230;    // (capitals are wider: fewer of them)
    if (text.size() <= kMost)
        return text;
    size_t cut = text.rfind(L' ', kMost);
    return text.substr(0, cut == std::wstring::npos || cut < kMost / 2 ? kMost : cut) + L"...";
}

// A flat colour (main.cpp's slopdash://solid/ texture, stretched). (A XuiFigure
// made in code crashed the console in XuiFigureSetFill, 2026-11-02.)
HXUIOBJ LaunchSolid(HXUIOBJ scene, const Box& b, DWORD argb, bool fade = false)
{
    HXUIOBJ img = MakeElement(scene, L"XuiImage", NULL);
    if (!img)
        return NULL;
    SetUintProperty(img, L"SizeMode", 0x4);        // XUI_IMAGE_PRESENTER_MODE_STRETCH
    wchar_t path[40];
    _snwprintf_s(path, 40, _TRUNCATE, fade ? L"slopdash://hfade/%08X" : L"slopdash://solid/%08X", (unsigned)argb);
    XuiImageElementSetImagePath(img, path);
    Put(img, b);
    return img;
}

// A box: its fill, and an edge `edge` wide round it (inside b), into parts.
void LaunchBox(HXUIOBJ scene, const Box& b, DWORD fill, float edge, DWORD edgeColour, std::vector<HXUIOBJ>& parts)
{
    Box in = { b.x + edge, b.y + edge, b.w - 2 * edge, b.h - 2 * edge, 1.0f };
    parts.push_back(LaunchSolid(scene, in, fill));
    if (edge <= 0)
        return;
    Box sides[4] = { { b.x, b.y, b.w, edge, 1.0f }, { b.x, b.y + b.h - edge, b.w, edge, 1.0f },
                     { b.x, b.y + edge, edge, b.h - 2 * edge, 1.0f },
                     { b.x + b.w - edge, b.y + edge, edge, b.h - 2 * edge, 1.0f } };
    for (int i = 0; i < 4; i++)
        parts.push_back(LaunchSolid(scene, sides[i], edgeColour));
}

// Building Launch, step by step, each line on the stick at once: the library
// froze the console with the 2026-11-02 build (XBDM gone too), and a frozen
// run's queued lines never reach SlopDash.prev.log.
void LaunchStep(const char* what)
{
    Log("launch: %s", what);
    LogFlush();
}

// What the game is, as the row's right-hand text says it.
const wchar_t* LaunchKind(const library::Game& g)
{
    if (g.place == kPlaceApps)
        return L"APPLICATION";
    if (g.place == kPlaceEmulators)
        return L"EMULATOR";
    switch (g.kind)
    {
    case library::kXbox360: return L"XBOX 360";
    case library::kArcade: return L"XBOX LIVE ARCADE";
    case library::kXboxOriginal: return L"XBOX";
    default: return L"HOMEBREW";
    }
}

// dashcomm's badge for an ESRB rating (library::XexEsrb's values), "" if none.
const wchar_t* LaunchEsrbFile(int esrb)
{
    switch (esrb)
    {
    case 0: return L"ESRB_EC.png";
    case 2: return L"ESRB_E.png";
    case 4: return L"ESRB_E10.png";
    case 6: return L"ESRB_T.png";
    case 8: return L"ESRB_M.png";
    default: return L"";            // (AO: 1888 has no badge for it)
    }
}

// Downloadable content for the title (shotcache.cpp: looked at once a title,
// by the prefetch worker as a rule).
bool LaunchHasContent(unsigned titleId)
{
    return TitleHasContent(titleId);
}

// Row k where it goes: out to the left as the tab, or in the list.
// An element of Launch's at b, in the screen's design (inside the mask), its
// opacity a: Put without its show (a row's parts show and hide by their own rules).
void LaunchMove(HXUIOBJ obj, const Box& b, float a)
{
    if (!obj)
        return;
    D3DXVECTOR3 pos(b.x - (g_lv.mask ? kFaceX : 0.0f), b.y - (g_lv.mask ? g_maskY : 0.0f), 0);
    XuiElementSetPosition(obj, &pos);
    XuiElementSetBounds(obj, b.w, b.h);
    XuiElementSetOpacity(obj, a);
}

// A row's part at b (in the screen's design), inside the rows' clip.
void LaunchRowMove(HXUIOBJ obj, const Box& b, float a)
{
    if (!obj)
        return;
    D3DXVECTOR3 pos(b.x - kLaunchClipX, b.y - kLaunchTop, 0);
    XuiElementSetPosition(obj, &pos);
    XuiElementSetBounds(obj, b.w, b.h);
    XuiElementSetOpacity(obj, a);
}

// Row k where it is now: slid by g_launch.slide and out to the left by r.out
// (the clip cuts it off past the list's top and foot).
void LaunchPlaceRow(LaunchRow& r, int k)
{
    float y = kLaunchTop + k * kLaunchRowH + g_launch.slide;
    const float a = 1.0f;
    float x = kLaunchX - r.out, w = kLaunchW + r.out;
    if (r.bg)
    {
        Box b = { x, y, kLaunchRowVisualW, kLaunchRowVisualH, 1.0f };
        LaunchRowMove(r.bg, b, a);
        D3DXVECTOR3 scale(w / kLaunchRowVisualW, kLaunchRowH / kLaunchRowVisualH, 1.0f);
        XuiElementSetScale(r.bg, &scale);
    }
    Box c = { x + 3, y + (kLaunchRowH - kLaunchArt) / 2, kLaunchArt, kLaunchArt, 1.0f };
    Box name = { x + kLaunchTextX, y + kLaunchTitleY, 290, 26, 1.0f };
    Box maker = { x + kLaunchTextX, y + kLaunchTitleY + kLaunchLineGap, 290, 26, 1.0f };
    LaunchRowMove(r.cover, c, a);
    LaunchRowMove(r.name, name, a);
    LaunchRowMove(r.maker, maker, a);
    // The kind's plaque, at the row's right (where the tab's right is too).
    float px = kLaunchX + kLaunchW - 8 - kLaunchTagW, py = y + (kLaunchRowH - kLaunchTagH) / 2;
    Box tag[3] = { { px, py, kLaunchTagW, kLaunchTagH, 1.0f },
                   { px + 2, py, kLaunchTagW - 4, 1.5f, 1.0f },
                   { px + 2, py + kLaunchTagH - 2, kLaunchTagW - 4, 2.0f, 1.0f } };
    for (size_t i = 0; i < r.tag.size() && i < 3; i++)
        LaunchRowMove(r.tag[i], tag[i], a);
    Box mark = { px + 10, py + 8, kLaunchTagW - 20, kLaunchTagH - 16, 1.0f };
    LaunchRowMove(r.mark, mark, a * 0.85f);           // (the reference's tag: the wordmark a little faded)
    Box kind = { px + 2, py + 1, kLaunchTagW - 6, kLaunchTagH - 2, 1.0f };
    LaunchRowMove(r.kind, kind, a);
}

// The list's panel in two, round the tab at row k (of `shown` rows).
// main.cpp's slopdash://shape/ texture: a fading line ('v'), a fading shadow
// ('s') or a ring's corner ('c'), flipped left-right / top-bottom.
std::wstring LaunchShape(wchar_t kind, DWORD argb, bool hflip, bool vflip)
{
    wchar_t path[48];
    _snwprintf_s(path, 48, _TRUNCATE, L"slopdash://shape/%c,%08X,%d,%d", kind, (unsigned)argb, hflip ? 1 : 0,
                 vflip ? 1 : 0);
    return path;
}

// The list's body round the tab at row k (of `shown` rows), as the reference
// has it: beside the tab the
// body's corners curve into its side edges - a dark line, and on the right a
// light one outside it - and those fade away along the list, a shadow left of
// the body fading with them. Below the tab they fade downwards, above it
// upwards (mirrored), strongest by the tab. No line along the body's top or foot.
enum { kEdgeCornerL, kEdgeCornerR, kEdgeDarkL, kEdgeDarkR, kEdgeLightR, kEdgeShadowL, kEdgeJoin, kEdgeParts };
const DWORD kEdgeDark = 0x8C0F0F0F, kEdgeLight = 0x4DFFFFFF, kEdgeShadow = 0x590F0F0F;
const float kEdgeRadius = 6.0f, kEdgeDarkW = 2.5f, kEdgeShadowW = 10.0f;

// Where the list ends: its last row's foot, or the clip's.
float LaunchListFoot()
{
    float foot = kLaunchTop + g_launch.rowsShown * kLaunchRowH;
    return foot < kLaunchClipFoot ? foot : kLaunchClipFoot;
}

void LaunchPanes(float tabY, float foot)
{
    if (g_launch.edges.size() < 2 * kEdgeParts)
        return;
    g_launch.edgePaths.resize(g_launch.edges.size());
    // (tabY: the tab's top, gliding between rows; the body above it and below)
    const float xL = kLaunchX, xR = kLaunchX + kLaunchW, r = kEdgeRadius;
    for (int i = 0; i < 2; i++)
    {
        HXUIOBJ* part = &g_launch.edges[i * kEdgeParts];
        bool up = i == 0;                           // (above the tab: mirrored, the tab at its foot)
        float yTab = up ? tabY : tabY + kLaunchRowH;
        float yFar = up ? kLaunchTop : foot;
        bool any = (up ? yTab - yFar : yFar - yTab) > kLaunchRowH / 2;
        float cornerY = up ? yTab - r : yTab;
        float lineY = up ? yFar : yTab + r, lineH = (up ? yTab - r : yFar) - lineY;
        if (lineH < 1)
            lineH = 1;
        Box boxes[kEdgeParts] = { { xL, cornerY, r, r, 1.0f },
                                  { xR - r, cornerY, r, r, 1.0f },
                                  { xL, lineY, kEdgeDarkW, lineH, 1.0f },
                                  { xR - kEdgeDarkW, lineY, kEdgeDarkW, lineH, 1.0f },
                                  { xR, lineY, 1.0f, lineH, 1.0f },
                                  { xL - kEdgeShadowW, lineY, kEdgeShadowW, lineH, 1.0f },
                                  { xL + r, up ? yTab - kEdgeDarkW : yTab, kLaunchW - 2 * r, kEdgeDarkW, 1.0f } };
        std::wstring paths[kEdgeParts] = { LaunchShape(L'c', kEdgeDark, true, up),
                                           LaunchShape(L'c', kEdgeDark, false, up),
                                           LaunchShape(L'v', kEdgeDark, false, up),
                                           LaunchShape(L'v', kEdgeDark, false, up),
                                           LaunchShape(L'v', kEdgeLight, false, up),
                                           LaunchShape(L's', kEdgeShadow, false, up),
                                           L"" };
        for (int p = 0; p < kEdgeParts; p++)
        {
            if (!part[p])
                continue;
            if (any)
            {
                Put(part[p], boxes[p]);
                std::wstring& was = g_launch.edgePaths[i * kEdgeParts + p];
                if (!paths[p].empty() && paths[p] != was)   // (the join: LaunchSolid's flat colour, set when made)
                {
                    XuiImageElementSetImagePath(part[p], paths[p].c_str());
                    was = paths[p];
                }
            }
            XuiElementSetShow(part[p], any);
        }
    }
}

void BuildLaunch(HXUIOBJ scene)
{
    MakeMask(scene, kFaceY, kFaceH);
    g_launch.rows.clear();
    g_launch.top = 0;
    g_launch.shotGame = -1;
    g_launch.ratingShown = -2;
    g_launch.slide = g_launch.tab = g_launch.tabTo = 0;
    g_launch.rowsShown = kLaunchRows;
    g_launch.n = 0;
    g_launch.placed = false;
    g_launch.sideWanted = -1;
    g_launch.sideAt = 0;
    LaunchStep("build");
    // (No panel round the list: lines between the rows only.)
    g_launch.paneAbove = g_launch.paneBelow = NULL;
    g_launch.edges.clear();
    g_launch.edgePaths.clear();
    for (int i = 0; i < 2 * kEdgeParts; i++)
    {
        Box none = { kLaunchX, kLaunchTop, 1.0f, 1.0f, 0.0f };
        g_launch.edges.push_back(LaunchSolid(scene, none, i % kEdgeParts == kEdgeJoin ? kEdgeDark : 0));
                                        // (shape and place: LaunchPanes)
    }
    std::wstring logoFile = DashFile(L"dashcomm", L"xboxLogo.png");
    LaunchStep("panel");
    g_launch.clip = NULL;
    if (SUCCEEDED(XuiCreateObject(L"XuiGroup", &g_launch.clip)) && g_launch.clip)
    {
        SetBoolProperty(g_launch.clip, L"ClipChildren", true);
        XuiElementAddChild(ViewParent(), g_launch.clip);
        Box b = { kLaunchClipX, kLaunchTop, kLaunchX + kLaunchW + 3 - kLaunchClipX, kLaunchClipFoot - kLaunchTop,
                  1.0f };
        Put(g_launch.clip, b);
        g_viewParentOverride = g_launch.clip;
    }
    for (int slot = 0; slot < kLaunchSlots; slot++)
    {
        int k = slot - 1;
        float y = kLaunchTop + k * kLaunchRowH;
        LaunchRow r;
        r.game = -1;
        r.focus = -1;
        r.visual = NULL;
        r.tab = -1;
        r.mark = NULL;
        r.bg = MakeElement(scene, L"XuiControl", L"btn_twoline-icon_RightText");
        if (r.bg && SUCCEEDED(XuiControlGetVisual(r.bg, &r.visual)) && r.visual)
            if (HXUIOBJ gscore = Child(r.visual, L"XuiImage1"))
                XuiElementSetShow(gscore, FALSE);
        Box c = { kLaunchX + 3, y + (kLaunchRowH - kLaunchArt) / 2, kLaunchArt, kLaunchArt, 1.0f };
        r.cover = LaunchImage(scene, c, L"");
        r.name = MakeLabel(scene, L"Label_BodySmall", kLaunchX + kLaunchTextX, y + kLaunchTitleY, 290, 26,
                           kLaunchTextPt);
        r.maker = MakeLabel(scene, L"Label_BodySmall", kLaunchX + kLaunchTextX, y + kLaunchTitleY + kLaunchLineGap, 290,
                            26, kLaunchTextPt);
        const unsigned oneLine = XUI_FONT_STYLE_NO_WORDWRAP | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_ELLIPSIS;
        LaunchDarkText(r.name, kLaunchStoryColour, false, oneLine);
        LaunchDarkText(r.maker, kLaunchStoryColour, false, oneLine);
        // The kind's plaque, at the row's right (where the tab's right is too).
        float px = kLaunchX + kLaunchW - 8 - kLaunchTagW, py = y + (kLaunchRowH - kLaunchTagH) / 2;
        Box fill = { px, py, kLaunchTagW, kLaunchTagH, 1.0f };
        Box lit = { px + 2, py, kLaunchTagW - 4, 1.5f, 1.0f };
        Box shade = { px + 2, py + kLaunchTagH - 2, kLaunchTagW - 4, 2.0f, 1.0f };
        r.tag.push_back(LaunchSolid(scene, fill, 0x14FFFFFF, true));
        r.tag.push_back(LaunchSolid(scene, lit, 0x59FFFFFF, true));
        r.tag.push_back(LaunchSolid(scene, shade, 0x330F0F0F, true));
        Box mark = { px + 10, py + 8, kLaunchTagW - 20, kLaunchTagH - 16, 1.0f };
        r.mark = LaunchImage(scene, mark, logoFile.empty() ? L"" : kLaunchWordmarkWhite + logoFile);
        if (r.mark)
            XuiElementSetOpacity(r.mark, 0.85f);    // (the reference's tag: the wordmark a little faded)
        r.kind = MakeLabel(scene, L"Label_SubRight", px + 2, py + 1, kLaunchTagW - 6, kLaunchTagH - 2, 8);
        r.out = r.outTo = 0;
        r.shown = true;
        g_launch.rows.push_back(r);
    }
    g_viewParentOverride = NULL;
    if (g_launch.clip)
        g_lv.made.push_back(g_launch.clip);    // (after its rows: destroyed once they are)
    LaunchStep("rows");
    // More below (and above): a small dark triangle under the list's right end,
    // as the reference's (main.cpp shape 't'; skin scr_ScrollEndUp/Down drew nothing).
    Box up = { kLaunchX + kLaunchW - 24, kLaunchTop - 11, 14, 8, 1.0f };
    Box down = { kLaunchX + kLaunchW - 24, kLaunchClipFoot + 4, 14, 8, 1.0f };
    g_launch.up = LaunchImage(scene, up, LaunchShape(L't', 0xA60F0F0F, false, true));
    g_launch.down = LaunchImage(scene, down, LaunchShape(L't', 0xA60F0F0F, false, false));
    if (g_launch.up)
        SetUintProperty(g_launch.up, L"SizeMode", 0x4);
    if (g_launch.down)
        SetUintProperty(g_launch.down, L"SizeMode", 0x4);
    // The screenshot set into the panel (the reference, 2026-11-02: "embedded,
    // rather than a white outline, there's an inner-bevel"): the bevel - shade
    // along its top and left, light along its bottom and right - and dark
    // behind a picture that doesn't fill it. (No pale band round it: the user.)
    std::vector<HXUIOBJ> frame;
    const float px = kLaunchRightX, py = kLaunchTop + 2, pw = kLaunchRightW, ph = kLaunchShotH;
    Box back = { px, py, pw, ph, 1.0f };
    frame.push_back(LaunchSolid(scene, back, 0xFF0F0F0F));
    Box shot = { kLaunchRightX, kLaunchTop + 2, kLaunchRightW, kLaunchShotH, 1.0f };
    g_launch.shot = LaunchImage(scene, shot, L"");
    Box bevel[4] = { { px, py, pw, 2.5f, 1.0f },                      // shade: top, left
                     { px, py + 2.5f, 2.5f, ph - 2.5f, 1.0f },
                     { px + 1.5f, py + ph - 1.5f, pw - 1.5f, 1.5f, 1.0f },  // light: bottom, right
                     { px + pw - 1.5f, py + 2.5f, 1.5f, ph - 4.0f, 1.0f } };
    const DWORD bevelColour[4] = { 0x8C0F0F0F, 0x8C0F0F0F, 0x8CFFFFFF, 0x8CFFFFFF };
    for (int i = 0; i < 4; i++)
        frame.push_back(LaunchSolid(scene, bevel[i], bevelColour[i]));
    // The story: 1888's dark text (skin Label_Body's colour), bold.
    g_launch.desc = MakeLabel(scene, L"Label_BodySmall", kLaunchRightX, kLaunchTop + kLaunchShotH + 12, kLaunchRightW,
                              150, kLaunchTextPt - 0.5f);
    LaunchDarkText(g_launch.desc, kLaunchStoryColour, true);
    LaunchStep("shot");
    Box rating = { kLaunchRightX, 525, 43, 64, 1.0f };
    g_launch.rating = LaunchImage(scene, rating, L"");
    if (g_launch.rating)
        XuiElementSetShow(g_launch.rating, FALSE);
    // Available Content: a pale box, its caption, the disc.
    Box box = { kLaunchRightX + 139, 528, kLaunchRightW - 139, 59, 1.0f };
    g_launch.contentBox.clear();
    LaunchBox(scene, box, 0x30FFFFFF, 1.0f, 0x73FFFFFF, g_launch.contentBox);
    g_launch.contentLabel = MakeLabel(scene, L"Label_Body_1line", box.x + 9, box.y + 1, box.w - 12, 26, 11);
    if (g_launch.contentLabel)
        XuiControlSetText(g_launch.contentLabel, L"Available Content");
    Box disc = { box.x + 8, box.y + 25, 30, 30, 1.0f };
    g_launch.disc = LaunchImage(scene, disc, DashFile(L"gamesbla", L"ico_32x_demos.png"));
    // The logo in the header's right: the orb, then the wordmark, ending over the
    // screenshot's right edge.
    float right = kLaunchRightX + kLaunchRightW;
    Box words = { right - 126, 103, 126, 26, 1.0f };
    Box orb = { right - 126 - 46, 95, 42, 41, 1.0f };
    g_launch.logo = LaunchImage(scene, words, logoFile.empty() ? L"" : kLaunchWordmark + logoFile);
    g_launch.orb = LaunchImage(scene, orb, logoFile.empty() ? L"" : kLaunchOrb + logoFile);
    // Everything slow for each game, made ahead from the highlighted one outwards.
    {
        const std::vector<library::Game>& games = Games();
        std::vector<PrefetchJob> jobs(games.size());
        for (size_t i = 0; i < games.size(); i++)
        {
            jobs[i].shot = ShotLocator(i);
            jobs[i].path = games[i].path;
            jobs[i].contentType = games[i].contentType;
            jobs[i].titleId = games[i].titleId;
        }
        PrefetchStart(jobs, g_lv.sel >= 0 ? g_lv.sel : 0);
    }
    LaunchStep("built");
    // The screen's header: "Select a Game" while it's this view.
    if (HXUIOBJ head = Child(scene, L"labTitle"))
    {
        LPCWSTR now = XuiControlGetText(head);
        g_launch.header = now ? now : L"";
        XuiControlSetText(head, L"Select a Game");
    }
}

// The highlighted game's side: its picture, story, rating and Available Content.
void LaunchSide(int sel)
{
    const std::vector<library::Game>& games = Games();
    if (sel < 0 || sel >= (int)games.size())
        return;
    const library::Game& g = games[sel];
    if (g_launch.shot && g_launch.shotGame != sel)
    {
        g_launch.shotGame = sel;
        std::wstring shot = ShotLocator((size_t)sel);
        XuiImageElementSetImagePath(g_launch.shot, (shot.empty() ? CoverOrIcon(sel) : shot).c_str());
    }
    catalog::Details d;
    bool details = GameDetails((size_t)sel, d);
    std::wstring about = details ? d.description : (IsBoilerplate(g.description) ? L"" : g.description);
    if (g_launch.desc)
        XuiControlSetText(g_launch.desc, LaunchStory(about).c_str());
    bool content = LaunchHasContent(g.titleId);
    std::vector<HXUIOBJ> box = g_launch.contentBox;
    box.push_back(g_launch.contentLabel);
    box.push_back(g_launch.disc);
    for (size_t i = 0; i < box.size(); i++)
        if (box[i])
            XuiElementSetShow(box[i], content);
    int esrb = GameEsrb((size_t)sel);
    if (g_launch.rating && esrb != g_launch.ratingShown)
    {
        g_launch.ratingShown = esrb;
        std::wstring file = LaunchEsrbFile(esrb);
        std::wstring path = file.empty() ? L"" : DashFile(L"dashcomm", file.c_str());
        XuiElementSetShow(g_launch.rating, !path.empty());
        if (!path.empty())
            XuiImageElementSetImagePath(g_launch.rating, path.c_str());
    }
}

void LaunchSelect(int sel, int n)
{
    static int logged = 0;          // (the first two selections, step by step)
    bool log = logged < 2;
    if (log)
        logged++;
    if (log)
        LaunchStep("select");
    int oldTop = g_launch.top;
    if (sel < g_launch.top)
        g_launch.top = sel;
    if (sel >= g_launch.top + kLaunchRows)
        g_launch.top = sel - kLaunchRows + 1;
    // The rows slide from where their games were (a page's jump: no slide).
    int moved = g_launch.top - oldTop;
    if (!g_launch.placed || moved > 2 || moved < -2)
        g_launch.slide = 0;
    else
        g_launch.slide += moved * kLaunchRowH;
    const float slideMost = 2 * kLaunchRowH;
    g_launch.slide = g_launch.slide > slideMost ? slideMost : g_launch.slide < -slideMost ? -slideMost : g_launch.slide;
    g_launch.n = n;
    g_launch.rowsShown = n - g_launch.top < kLaunchRows + 1 ? n - g_launch.top : kLaunchRows + 1;
    g_launch.tabTo = kLaunchTop + (sel - g_launch.top) * kLaunchRowH;
    if (!g_launch.placed)
        g_launch.tab = g_launch.tabTo;
    const std::vector<library::Game>& games = Games();
    for (int k = 0; k < (int)g_launch.rows.size(); k++)
    {
        LaunchRow& r = g_launch.rows[k];
        int game = g_launch.top + k - 1;    // (slot k is row k - 1: one above the list, more below)
        bool shown = game >= 0 && game < n;
        r.outTo = game == sel ? kLaunchTabOut : 0.0f;
        if (!g_launch.placed)
            r.out = r.outTo;
        r.shown = shown;
        std::vector<HXUIOBJ> parts = r.tag;
        HXUIOBJ own[] = { r.bg, r.cover, r.name, r.maker };
        parts.insert(parts.end(), own, own + 4);
        for (size_t p = 0; p < parts.size(); p++)
            if (parts[p])
                XuiElementSetShow(parts[p], shown);
        if (!shown)
        {
            if (r.kind)
                XuiElementSetShow(r.kind, FALSE);
            if (r.mark)
                XuiElementSetShow(r.mark, FALSE);
            continue;
        }
        if (r.game != game)
        {
            r.game = game;
            const library::Game& g = games[game];
            // The game's own square tile (its title image), as the reference's
            // rows; the cover only for a game without one.
            if (r.cover)
                XuiImageElementSetImagePath(
                    r.cover, (g.icon.empty() ? CoverOrIcon(game) : IconLocator((size_t)game)).c_str());
            catalog::Details d;
            std::wstring maker = GameDetails((size_t)game, d) ? (d.developer.empty() ? d.publisher : d.developer)
                                                               : L"";
            if (r.name)
                XuiControlSetText(r.name, g.name.c_str());
            if (r.maker)
                XuiControlSetText(r.maker, maker.c_str());
            if (r.kind)
                XuiControlSetText(r.kind, LaunchKind(g));
        }
        // The row visual's GScore icon hidden again: a refresh brought it back
        // over the tag (2026-11-03).
        if (r.visual)
            if (HXUIOBJ gscore = Child(r.visual, L"XuiImage1"))
                if (XuiElementIsShown(gscore))
                    XuiElementSetShow(gscore, FALSE);
        // Dark after every show: made dark with the row, and again with its
        // text, it was still white (showing a label sets its skin colour back?).
        const unsigned oneLine = XUI_FONT_STYLE_NO_WORDWRAP | XUI_FONT_STYLE_SINGLE_LINE | XUI_FONT_STYLE_ELLIPSIS;
        int a = LaunchDarkText(r.name, kLaunchStoryColour, false, oneLine);
        int b = LaunchDarkText(r.maker, kLaunchStoryColour, false, oneLine);
        if (log && k == 0)
            Log("launch: row text presenters %d %d", a, b);
        // An Xbox 360 game's tag is the wordmark; anything else's, its kind in words.
        bool is360 = r.mark && games[game].kind == library::kXbox360 && games[game].place == kPlaceGames;
        if (r.mark)
            XuiElementSetShow(r.mark, is360);
        if (r.kind)
            XuiElementSetShow(r.kind, !is360);
        int focus = game == sel ? 1 : 0;
        HXUIOBJ anim = r.visual ? r.visual : r.bg;      // (the visual has the Focus / Normal frames)
        if (anim && r.focus != focus)
        {
            r.focus = focus;
            if (log)
                Log("launch: row %d %s", k, focus ? "Focus" : "Normal");
            PlayRange(anim, focus ? L"Focus" : L"Normal", focus ? L"EndFocus" : L"EndNormal", true);
        }
    }
    int rowsShown = g_launch.rowsShown;
    // Lines between the rows only: the row visual's own top line (its first
    // child) off on the first row, its bottom line (its second) on the last;
    // and where the body meets the tab, neither row's (LaunchPanes draws that
    // join, its ends curving into the body's edges: square, they stood out).
    int tab = sel - g_launch.top;
    for (int slot = 0; slot < (int)g_launch.rows.size(); slot++)
    {
        int k = slot - 1;
        HXUIOBJ v = g_launch.rows[slot].visual, topLine = NULL, bottomLine = NULL;
        if (!v || FAILED(XuiElementGetFirstChild(v, &topLine)) || !topLine)
            continue;
        XuiElementGetNext(topLine, &bottomLine);
        XuiElementSetShow(topLine, k > 0 && k != tab && k != tab + 1);
        if (bottomLine)
            XuiElementSetShow(bottomLine, k < rowsShown - 1 && k != tab && k != tab - 1);
    }
    if (g_launch.up)
        XuiElementSetShow(g_launch.up, g_launch.top > 0);
    if (g_launch.down)
        XuiElementSetShow(g_launch.down, g_launch.top + kLaunchRows < n);
    if (log)
        LaunchStep("rows shown");
    // Where everything is now (then LaunchAnimate eases it on), and the side:
    // at once the first time, else when the highlight rests.
    for (int slot = 0; slot < (int)g_launch.rows.size(); slot++)
        if (g_launch.rows[slot].shown)
            LaunchPlaceRow(g_launch.rows[slot], slot - 1);
    LaunchPanes(g_launch.tab, LaunchListFoot());
    PrefetchFocus(sel);
    // (the first time too: made on this thread, the first game's side stalled
    // the opening by over a second)
    g_launch.placed = true;
    g_launch.sideWanted = sel;
    g_launch.sideAt = GetTickCount();
    StartAnimation();
}

// One step of Launch's movement (StepAnimation's share k of the way left): the
// rows' slide, each row's tab, the tab's height; then, once the highlight has
// rested 150 ms and the prefetch has made its details (2.5 s at most), its side. True while anything is still to do.
bool LaunchAnimate(float k)
{
    if (g_launch.rows.empty() || !g_launch.placed)
        return false;
    bool moving = false;
    float* now[] = { &g_launch.slide, &g_launch.tab };
    const float to[] = { 0.0f, g_launch.tabTo };
    for (int i = 0; i < 2; i++)
    {
        float d = to[i] - *now[i];
        if (d > 0.4f || d < -0.4f)
        {
            *now[i] += d * k;
            moving = true;
        }
        else if (*now[i] != to[i])
        {
            *now[i] = to[i];
            moving = true;
        }
    }
    bool rowsMoved = moving;
    for (size_t r = 0; r < g_launch.rows.size(); r++)
    {
        LaunchRow& row = g_launch.rows[r];
        float d = row.outTo - row.out;
        if (d > 0.2f || d < -0.2f)
            row.out += d * k, rowsMoved = moving = true;
        else if (row.out != row.outTo)
            row.out = row.outTo, rowsMoved = moving = true;
    }
    if (rowsMoved)
    {
        for (int r = 0; r < (int)g_launch.rows.size(); r++)
            if (g_launch.rows[r].shown)
                LaunchPlaceRow(g_launch.rows[r], r - 1);
        LaunchPanes(g_launch.tab, LaunchListFoot());
    }
    DWORD rested = GetTickCount() - g_launch.sideAt;
    if (g_launch.sideWanted >= 0 && rested >= 150 && (PrefetchFinished(g_launch.sideWanted) || rested >= 2500))
    {
        LaunchSide(g_launch.sideWanted);
        g_launch.sideWanted = -1;
    }
    return moving || g_launch.sideWanted >= 0;
}

void ClearLaunch()
{
    if (g_launch.header.size() && g_lv.scene)
        if (HXUIOBJ head = Child(g_lv.scene, L"labTitle"))
            XuiControlSetText(head, g_launch.header.c_str());
    g_launch.header.clear();
    g_launch.rows.clear();
    g_launch.contentBox.clear();
    g_launch.edges.clear();
    g_launch.edgePaths.clear();
    g_launch.shot = g_launch.desc = g_launch.rating = g_launch.contentLabel = g_launch.disc = NULL;
    g_launch.logo = g_launch.orb = g_launch.up = g_launch.down = NULL;
    g_launch.paneAbove = g_launch.paneBelow = NULL;
    g_launch.placed = false;
    g_launch.sideWanted = -1;
    g_launch.clip = NULL;
}

// The highlighted game is now `sel` (was `prev`): covers keep their games and
// slide to their new places; only a cover coming into view loads a new one.
void ViewSelect(int sel, int prev)
{
    const std::vector<library::Game>& games = Games();
    int n = (int)games.size();
    g_lv.sel = sel;
    if (n == 0)
    {
        for (size_t i = 0; i < g_lv.covers.size(); i++)
            XuiElementSetShow(g_lv.covers[i].obj, FALSE);
        if (g_lv.frame.obj)
            XuiElementSetShow(g_lv.frame.obj, FALSE);
        ShowViewText(-1);
        return;
    }
    // The shortest way round from prev to sel (the list wraps).
    int delta = sel - prev;
    if (delta > n / 2)
        delta -= n;
    if (delta < -n / 2)
        delta += n;
    const std::string& v = g_lv.id;
    if (v == "coverflow")
    {
        int slots = (int)g_lv.covers.size();
        // The slot that showed the game now at offset o was at offset o + delta:
        // turn the slots round by delta, so each keeps its cover.
        if (delta != 0 && delta > -slots && delta < slots)
            std::rotate(g_lv.covers.begin(), g_lv.covers.begin() + WrapIndex(delta, slots), g_lv.covers.end());
        for (int i = 0; i < slots; i++)
        {
            int o = i - kFlowSide;
            ViewItem& c = g_lv.covers[i];
            int game = WrapIndex(sel + o, n);
            bool fresh = c.game != game;    // coming into view (or the first time)
            SetCover(c, game);
            c.target = FlowBox(o);
            if (!Visible(o, n))
                c.target.a = 0;
            if (fresh)
            {
                // It comes in from beyond the edge it enters by.
                int from = o + (delta > 0 ? 1 : delta < 0 ? -1 : 0);
                c.now = FlowBox(from);
                c.now.a = 0;
            }
        }
        // Back to front, so nearer covers draw over farther ones; text on top.
        for (int d = kFlowSide; d >= 0; d--)
        {
            int side[2] = { kFlowSide - d, kFlowSide + d };
            for (int k = 0; k < (d ? 2 : 1); k++)
            {
                HXUIOBJ obj = g_lv.covers[side[k]].obj;
                XuiElementUnlink(obj);
                XuiElementAddChild(ViewParent(), obj);
            }
        }
        RaiseText();
    }
    else if (v == "tiles")
    {
        // Tiles: the highlighted game's row kept in the middle row where it can
        // be, the rows rolling up and down past it; a row above and below the
        // three on show (faded) for the rows coming in.
        GridShape g = Shape(v);
        int rows = (n + g.cols - 1) / g.cols;
        int row = sel / g.cols, prevTop = g_lv.top;
        int top = row - g.rows / 2;
        if (top > rows - g.rows)
            top = rows - g.rows;
        if (top < 0)
            top = 0;
        g_lv.top = top;
        int slots = (int)g_lv.covers.size();       // (rows + 2) x cols
        int shift = (top - prevTop) * g.cols;
        if (shift != 0 && shift > -slots && shift < slots)
            std::rotate(g_lv.covers.begin(), g_lv.covers.begin() + WrapIndex(shift, slots), g_lv.covers.end());
        for (int k = 0; k < slots; k++)
        {
            int r = k / g.cols - 1, col = k % g.cols;
            int game = (top + r) * g.cols + col;
            ViewItem& c = g_lv.covers[k];
            bool shown = game >= 0 && game < n;
            bool fresh = c.game != game;
            if (shown)
                SetCover(c, game);
            c.target = GridBox(g, col, r, game == sel);
            if (!shown)
                c.target.a = 0;
            if (fresh || !c.now.w)
            {
                // A row coming in starts a row further out, faded.
                c.now = GridBox(g, col, r + (top > prevTop ? 1 : top < prevTop ? -1 : 0), false);
                c.now.a = 0;
            }
        }
        g_lv.frame.target = Around(GridBox(g, sel % g.cols, row - top, true), 8);
        if (!g_lv.frame.now.w)
            g_lv.frame.now = g_lv.frame.target;
    }
    else if (IsGrid(v))
    {
        GridShape g = Shape(v);
        int row = sel / g.cols;
        if (row < g_lv.top)
            g_lv.top = row;
        if (row >= g_lv.top + g.rows)
            g_lv.top = row - g.rows + 1;
        for (size_t k = 0; k < g_lv.covers.size(); k++)
        {
            int game = g_lv.top * g.cols + (int)k;
            ViewItem& c = g_lv.covers[k];
            bool shown = game < n;
            if (shown)
                SetCover(c, game);
            c.target = GridBox(g, (int)k % g.cols, (int)k / g.cols, game == sel);
            if (!shown)
                c.target.a = 0;
            if (!c.now.w)
                c.now = c.target;
        }
        int k = sel - g_lv.top * g.cols;
        g_lv.frame.target = Around(GridBox(g, k % g.cols, k / g.cols, true), 8);
        if (!g_lv.frame.now.w)
            g_lv.frame.now = g_lv.frame.target;
    }
    else if (v == "marquee")
    {
        ViewItem& c = g_lv.covers[0];
        SetCover(c, sel);
        c.target = kMarqueeCover;
        // A slide in from the side it came from.
        c.now = kMarqueeCover;
        c.now.x += delta > 0 ? 60.0f : delta < 0 ? -60.0f : 0.0f;
        c.now.a = delta ? 0.2f : 1.0f;
        g_lv.frame.target = Around(kMarqueeCover, 10);
        g_lv.frame.now = g_lv.frame.target;
    }
    else if (v == "index")
        ShowIndex(sel, n);
    else if (v == "launch")
    {
        LaunchSelect(sel, n);
        return;                     // (nothing of its eases: no animation)
    }
    ShowViewText(sel);
    StartAnimation();
}

// Sets up the chosen view on the library screen (again after a change of view,
// of its options, or a rescan).
void InstallLibraryKeyFilter();

void ApplyView(HXUIOBJ scene)
{
    InstallLibraryKeyFilter();
    ClearView();
    g_lv.scene = scene;
    g_lv.id = CurrentLibraryView();
    g_lv.stamp = LibraryViewStamp();
    g_lv.top = 0;
    g_listCoverMode = g_lv.id == "list" && ListShowsCover();
    HXUIOBJ list = Descendant(scene, L"listTitles");
    HXUIOBJ panel = Descendant(scene, L"scnTitle");
    HXUIOBJ totals = Descendant(scene, L"lblTotals");
    bool standard = IsListView(g_lv.id);
    if (totals)
        XuiElementSetShow(totals, standard || g_lv.id == "marquee");
    // The list keeps the focus and the selection; in the other views it's invisible.
    if (list)
        XuiElementSetOpacity(list, standard ? 1.0f : 0.0f);
    if (panel)
        XuiElementSetShow(panel, standard);
    Log("library view: %s", g_lv.stamp.c_str());
    if (standard)
    {
        if (list)
            ShowSelectedGame(scene, XuiListGetCurSel(list, NULL));   // the panel for this list view
        return;
    }
    if (g_lv.id == "launch")
    {
        BuildLaunch(scene);
        int sel = list ? XuiListGetCurSel(list, NULL) : 0;
        ViewSelect(sel < 0 ? 0 : sel, sel < 0 ? 0 : sel);
        return;
    }

    if (IsGrid(g_lv.id))
        MakeMask(scene, kGridTop - 12, kGridHeight + 24);
    else
        MakeMask(scene, kFaceY, kFaceH);
    // The highlight frame first (behind the covers), then the covers.
    // (Cover Flow has none: the user found the frame out of keeping there.)
    g_lv.frame.obj = g_lv.id == "coverflow" ? NULL : MakeElement(scene, L"XuiLabel", L"Metapane");
    memset(&g_lv.frame.now, 0, sizeof(Box));
    g_lv.frame.placed = false;
    int slots = 0;
    if (g_lv.id == "tiles")
        slots = (Shape("tiles").rows + 2) * Shape("tiles").cols;
    else if (IsGrid(g_lv.id))
        slots = Shape(g_lv.id).cols * Shape(g_lv.id).rows;
    else if (g_lv.id == "marquee")
        slots = 1;
    else if (g_lv.id == "index")
        slots = IndexIcons() ? kIndexRows * kIndexCols : 0;
    else
        slots = 2 * kFlowSide + 1;
    for (int i = 0; i < slots; i++)
    {
        ViewItem c;
        c.obj = MakeElement(scene, L"XuiImage", NULL);
        if (c.obj)
            SetUintProperty(c.obj, L"SizeMode", 0x10);      // fit, keeping the cover's shape
        memset(&c.now, 0, sizeof(Box));
        memset(&c.target, 0, sizeof(Box));
        c.game = -1;
        c.placed = false;
        g_lv.covers.push_back(c);
    }
    if (g_lv.id == "marquee")
    {
        g_lv.title = MakeLabel(scene, L"Label_MetaHead_1line", 420, 160, 580, 47, 0);
        g_lv.sub = MakeLabel(scene, L"Label_Body", 420, 210, 580, 40, kInfoPointSize);
        g_lv.body = MakeLabel(scene, L"Label_BodySmall", 420, 256, 580, 262, kDescriptionPointSize);
    }
    else if (g_lv.id == "index")
    {
        // Names only: one label a place, the skin's one-line body style, small.
        bool icons = IndexIcons();
        for (int k = 0; k < kIndexRows * kIndexCols; k++)
        {
            Box at = IndexBox(k / kIndexRows, k % kIndexRows, icons);
            HXUIOBJ l = MakeLabel(scene, L"Label_Body_1line", at.x, at.y, at.w, at.h, kIndexPoints);
            if (l)
                g_lv.names.push_back(l);
        }
        g_lv.count = MakeLabel(scene, L"XuiLabelCenterJustify", 110, 552, 900, 34, kInfoPointSize);
    }
    else if (IsGrid(g_lv.id))
    {
        // The grids: the covers take the room; only the game's name, small, at
        // the foot of the blade.
        // (Outside the mask, under it.)
        g_lv.title = MakeLabelIn(scene, L"XuiLabelCenterJustify", 110, kGridTitleY, 900, 28, kGridTitlePoints,
                                 g_lv.made);
    }
    else
    {
        float y = TitleY(g_lv.id);
        g_lv.title = MakeLabel(scene, L"XuiLabelCenterJustify", 110, y, 900, 40, 18);
        g_lv.sub = MakeLabel(scene, L"XuiLabelCenterJustify", 110, y + 34, 900, 32, kInfoPointSize);
        g_lv.count = MakeLabel(scene, L"XuiLabelCenterJustify", 110, 572, 900, 34, kInfoPointSize);
    }
    int sel = list ? XuiListGetCurSel(list, NULL) : 0;
    ViewSelect(sel < 0 ? 0 : sel, sel < 0 ? 0 : sel);
}

// The pad on the invisible list: moves along it. True if the key was taken.
bool ViewKey(HXUIOBJ list, DWORD key)
{
    if (IsListView(g_lv.id))
        return false;
    int n = (int)Games().size();
    if (n == 0)
        return false;
    int cur = XuiListGetCurSel(list, NULL);
    int next = cur;
    bool left = key == VK_PAD_DPAD_LEFT || key == VK_PAD_LTHUMB_LEFT;
    bool right = key == VK_PAD_DPAD_RIGHT || key == VK_PAD_LTHUMB_RIGHT;
    bool up = key == VK_PAD_DPAD_UP || key == VK_PAD_LTHUMB_UP;
    bool down = key == VK_PAD_DPAD_DOWN || key == VK_PAD_LTHUMB_DOWN;
    if (g_lv.id == "launch")
    {
        // Up and down a game, the shoulders and triggers a page; the ends stop it.
        if (up || down)
            next = up ? cur - 1 : cur + 1;
        else if (key == VK_PAD_LSHOULDER || key == VK_PAD_LTRIGGER)
            next = cur - kLaunchRows;
        else if (key == VK_PAD_RSHOULDER || key == VK_PAD_RTRIGGER)
            next = cur + kLaunchRows;
        else
            return false;
        if (next < 0)
            next = 0;
        if (next >= n)
            next = n - 1;
    }
    else if (IsGrid(g_lv.id) && (up || down))
    {
        // A row up or down (staying put at the first and last rows).
        int cols = Shape(g_lv.id).cols;
        next = up ? cur - cols : cur + cols;
        if (next < 0 || next >= n)
            next = cur;
    }
    else if (g_lv.id == "index" && (left || right))
    {
        // A column across.
        next = left ? cur - kIndexRows : cur + kIndexRows;
        if (next < 0)
            next = 0;
        if (next >= n)
            next = n - 1;
    }
    else if (left)
        next = WrapIndex(cur - 1, n);
    else if (right)
        next = WrapIndex(cur + 1, n);
    else if (IsGrid(g_lv.id) && (key == VK_PAD_LSHOULDER || key == VK_PAD_RSHOULDER || key == VK_PAD_LTRIGGER ||
                                 key == VK_PAD_RTRIGGER))
        return true;
    else
        return false;               // up/down elsewhere: the list's own move
    if (next != cur)
    {
        XuiListSetCurSel(list, next);
        ViewSelect(next, cur);
    }
    return true;
}

// Up and down in the grids: the list's focused row takes them itself (a game
// at a time) before the list's own code sees them, so the grids moved by one
// game, not a row (0.0.121). XUI's message filter sees every message first:
// up/down for the library's list, in Grid or Tiles, go to ViewKey there.
PFN_MESSAGE_FILTER g_prevFilter = NULL;
bool g_filterOn = false;

HRESULT APIENTRY LibraryKeyFilter(HXUIOBJ obj, XUIMessage* msg)
{
    if (msg->dwMessage == XM_KEYDOWN && g_lv.scene && (IsGrid(g_lv.id) || g_lv.id == "launch"))
    {
        DWORD key = ((XUIMessageInput*)msg->pvData)->dwKeyCode;
        bool vertical = key == VK_PAD_DPAD_UP || key == VK_PAD_DPAD_DOWN || key == VK_PAD_LTHUMB_UP ||
                        key == VK_PAD_LTHUMB_DOWN;
        HXUIOBJ list = vertical ? Descendant(g_lv.scene, L"listTitles") : NULL;
        bool inList = false;
        for (HXUIOBJ o = obj; list && o && !inList; XuiElementGetParent(o, &o))
            inList = o == list;
        if (inList && ViewKey(list, key))
        {
            msg->bHandled = TRUE;
            return S_OK;
        }
    }
    return g_prevFilter ? g_prevFilter(obj, msg) : S_OK;
}

void InstallLibraryKeyFilter()
{
    if (g_filterOn)
        return;
    g_prevFilter = XuiSetMessageFilter(LibraryKeyFilter);
    g_filterOn = true;
}

// The library screen's messages for the views; true if taken.
bool ViewMessage(HXUIOBJ scene, XUIMessage* msg)
{
    if (msg->dwMessage == XM_TIMER && ((XUIMessageTimer*)msg->pvData)->nId == kViewAnimTimer)
    {
        if (!StepAnimation())
        {
            XuiKillTimer(scene, kViewAnimTimer);
            g_lv.animating = false;
        }
        return true;
    }
    if (msg->dwMessage == XM_DESTROY && scene == g_lv.scene)
    {
        g_lv.made.clear();          // destroyed with the scene
        g_lv.covers.clear();
        g_lv.names.clear();
        g_lv.frame.obj = g_lv.title = g_lv.sub = g_lv.body = g_lv.count = g_lv.mask = NULL;
        g_launch.rows.clear();      // (Launch's parts went with the scene too)
        g_launch.header.clear();
        g_launch.shot = g_launch.desc = g_launch.rating = g_launch.contentLabel = g_launch.disc = NULL;
        g_launch.logo = g_launch.orb = g_launch.up = g_launch.down = NULL;
    g_launch.paneAbove = g_launch.paneBelow = NULL;
        g_lv.scene = NULL;
        g_lv.id.clear();
        g_lv.stamp.clear();
    }
    return false;
}
