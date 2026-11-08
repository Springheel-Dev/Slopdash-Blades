#include "scenes.h"

#include "core/xex.h"
#include "core/xur.h"
#include "core/xzp.h"

struct StandInInfo
{
    const wchar_t* name;
    const wchar_t* base;
    int count;
    int types[10];
};
#include "theme/blades1888_classes.inc"

namespace scenes {

static std::string Narrow(const std::wstring& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); i++)
        s += w[i] < 0x80 ? (char)w[i] : '_';
    return s;
}

static const std::vector<xur::CustomClass>& Custom()
{
    static std::vector<xur::CustomClass> custom;
    if (custom.empty())
    {
        for (size_t i = 0; i < sizeof(kBlades1888Classes) / sizeof(kBlades1888Classes[0]); i++)
        {
            xur::CustomClass c;
            c.name = Narrow(kBlades1888Classes[i].name);
            c.base = Narrow(kBlades1888Classes[i].base);
            c.types.assign(kBlades1888Classes[i].types, kBlades1888Classes[i].types + kBlades1888Classes[i].count);
            custom.push_back(c);
        }
    }
    return custom;
}

bool Upgrade(const unsigned char* file, size_t size, std::vector<Scene>& out, std::vector<std::string>& failures,
             std::string& error)
{
    xex::Image img;
    if (!size || !xex::Load(file, size, img, error))
        return false;
    for (size_t r = 0; r < img.resources.size(); r++)
    {
        const xex::Resource& res = img.resources[r];
        std::vector<xzp::Entry> list;
        if (!xzp::List(&img.data[res.offset], res.size, list))
            continue;
        for (size_t e = 0; e < list.size(); e++)
        {
            unsigned char* d = &img.data[res.offset + list[e].offset];
            if (!xur::IsScene(d, list[e].size))
                continue;
            std::string name = res.name + "#" + Narrow(list[e].name);
            xur::UpgradeStats st;
            std::string err;
            if (!xur::Upgrade1888(d, list[e].size, Custom(), &st, err))
            {
                failures.push_back("FAIL: " + name + ": " + err);
                continue;
            }
            Scene s;
            s.name = name;
            s.data.assign(d, d + list[e].size);
            out.push_back(s);
        }
    }
    return true;
}

}  // namespace scenes
