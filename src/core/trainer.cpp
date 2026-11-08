#include "core/trainer.h"

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "core/json.h"
#include "core/xex.h"

namespace trainer {

unsigned Info::DefaultMask() const
{
    unsigned mask = 0;
    for (size_t i = 0; i < cheats.size() && i < 32; i++)
        if (cheats[i].on)
            mask |= 1u << i;
    return mask;
}

int Info::Toggles() const
{
    int n = 0;
    for (size_t i = 0; i < cheats.size(); i++)
        n += cheats[i].button ? 0 : 1;
    return n;
}

std::wstring Info::UpdatesText() const
{
    if (titleUpdates.empty())
        return L"No Title Update";
    // Runs of numbers: "Title Updates 0-4", "Title Updates 1, 3".
    std::vector<unsigned> u(titleUpdates);
    for (size_t i = 1; i < u.size(); i++)
        for (size_t j = i; j > 0 && u[j - 1] > u[j]; j--)
        {
            unsigned t = u[j];
            u[j] = u[j - 1];
            u[j - 1] = t;
        }
    std::wstring s = u.size() == 1 ? L"Title Update " : L"Title Updates ";
    wchar_t b[24];
    for (size_t i = 0; i < u.size();)
    {
        size_t j = i;
        while (j + 1 < u.size() && u[j + 1] <= u[j] + 1)
            j++;
        if (j > i && u[j] != u[i])
            swprintf(b, 24, L"%ls%u-%u", i ? L", " : L"", u[i], u[j]);
        else
            swprintf(b, 24, L"%ls%u", i ? L", " : L"", u[i]);
        s += b;
        i = j + 1;
    }
    return s;
}

Fit Check(const Info& t, const GameVersion& g)
{
    const unsigned kAny = 0xFFFFFFFFu;
    if (t.titleId != g.titleId || (t.mediaId != kAny && t.mediaId != g.mediaId) ||
        (t.baseVersion != kAny && t.baseVersion != g.baseVersion) || (t.discNum != kAny && t.discNum != g.discNum))
        return kOtherGame;
    if (g.titleUpdate < 0)
        return kUnknownUpdate;
    if (t.titleUpdates.empty())
        return g.titleUpdate == 0 ? kFits : kWrongUpdate;
    for (size_t i = 0; i < t.titleUpdates.size(); i++)
        if (t.titleUpdates[i] == (unsigned)g.titleUpdate)
            return kFits;
    return kWrongUpdate;
}

static unsigned Be32(const unsigned char* p)
{
    return ((unsigned)p[0] << 24) | ((unsigned)p[1] << 16) | ((unsigned)p[2] << 8) | p[3];
}

static std::wstring TextOf(const json::Value* node, const char* key)
{
    const json::Value* v = node ? node->Get(key) : NULL;
    return v ? v->Text() : std::wstring();
}

static unsigned NumberOf(const json::Value* node, const char* key)
{
    const json::Value* v = node ? node->Get(key) : NULL;
    return v ? v->Unsigned() : 0;
}

bool Read(const unsigned char* file, size_t size, Info& out, std::string& error)
{
    out = Info();
    if (size < 8 || memcmp(file, "XEX2", 4))
    {
        error = "not an XEX2 file";
        return false;
    }
    // (Aurora's check: module flags 0x8 DLL and 0x2 exports to title)
    if ((Be32(file + 4) & 0xA) != 0xA)
    {
        error = "not a system DLL";
        return false;
    }
    xex::Image img;
    if (!xex::Load(file, size, img, error))
        return false;
    const xex::Resource* r = img.Find("aurora");
    if (!r || !r->size)
    {
        error = "no aurora resource";
        return false;
    }
    json::Value root;
    if (!json::Parse((const char*)&img.data[r->offset], r->size, root, error))
    {
        error = "its JSON: " + error;
        return false;
    }
    const json::Value* header = root.Get("header");
    const json::Value* exec = root.Get("execinfo");
    const json::Value* meta = root.Get("metadata");
    const json::Value* cheats = root.Get("cheats");
    if (!cheats && meta)
        cheats = meta->Get("cheats");       // (where the shared packs have it)
    if (!exec || !meta || !cheats || cheats->type != json::Value::kArray)
    {
        error = "its JSON lacks execinfo, metadata or cheats";
        return false;
    }
    out.framework = TextOf(header, "framework");
    out.engine = TextOf(header, "engine");
    out.titleId = NumberOf(exec, "titleid");
    out.mediaId = NumberOf(exec, "mediaid");
    out.baseVersion = NumberOf(exec, "basever");
    out.discNum = NumberOf(exec, "discnum");
    if (const json::Value* tu = exec->Get("tuver"))
    {
        if (tu->type == json::Value::kArray)
            for (size_t i = 0; i < tu->items.size(); i++)
                out.titleUpdates.push_back(tu->items[i].Unsigned());
        else
            out.titleUpdates.push_back(tu->Unsigned());
    }
    out.name = TextOf(meta, "displayname");
    out.author = TextOf(meta, "author");
    out.version = TextOf(meta, "version");
    out.description = TextOf(meta, "description");
    for (size_t i = 0; i < cheats->items.size(); i++)
    {
        const json::Value& c = cheats->items[i];
        Cheat cheat;
        cheat.text = TextOf(&c, "text");
        cheat.button = TextOf(&c, "type") == L"button";
        const json::Value* d = c.Get("default");
        cheat.on = d ? d->Truth() : false;
        out.cheats.push_back(cheat);
    }
    if (const xex::Resource* icon = img.Find("icon"))
        out.icon.assign(&img.data[icon->offset], &img.data[icon->offset] + icon->size);
    return true;
}

}  // namespace trainer
