// SlopDash Importer's scene upgrader: every XUI scene in the given 2.0.1888
// modules renumbered for the XDK's XUI runtime (core/xur Upgrade1888, as
// SlopDash does it on the console), each written to <out dir>/<package>#<file>.
// The Guide (tools/guide1888.py) and the tab strip (tools/dashmain5.py) are
// built from these. Our own code only: built with g++ (build.sh), no XDK.
//
//   upgrade_scenes.exe <out dir> <module.xex>...
//
// The upgrade itself is scenes.cpp.
#include "scenes.h"

#include <stdio.h>
#include <string>
#include <vector>

static std::vector<unsigned char> ReadAll(const char* path)
{
    std::vector<unsigned char> d;
    FILE* f = fopen(path, "rb");
    if (!f)
        return d;
    fseek(f, 0, SEEK_END);
    d.resize((size_t)ftell(f));
    fseek(f, 0, SEEK_SET);
    if (!d.empty() && fread(&d[0], 1, d.size(), f) != d.size())
        d.clear();
    fclose(f);
    return d;
}

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        printf("usage: upgrade_scenes <out dir> <module.xex>...\n");
        return 2;
    }
    int scenesDone = 0, failed = 0;
    for (int a = 2; a < argc; a++)
    {
        std::vector<unsigned char> file = ReadAll(argv[a]);
        std::vector<scenes::Scene> out;
        std::vector<std::string> failures;
        std::string err;
        if (!scenes::Upgrade(file.empty() ? NULL : &file[0], file.size(), out, failures, err))
        {
            printf("FAIL: %s: %s\n", argv[a], err.c_str());
            return 1;
        }
        for (size_t i = 0; i < failures.size(); i++)
            printf("%s\n", failures[i].c_str());
        failed += (int)failures.size();
        for (size_t i = 0; i < out.size(); i++)
        {
            std::string path = std::string(argv[1]) + "/" + out[i].name;
            FILE* f = fopen(path.c_str(), "wb");
            if (!f || fwrite(&out[i].data[0], 1, out[i].data.size(), f) != out[i].data.size())
            {
                printf("FAIL: can't write %s\n", path.c_str());
                failed++;
            }
            else
                scenesDone++;
            if (f)
                fclose(f);
        }
    }
    printf("%d scenes upgraded, %d failed\n", scenesDone, failed);
    return failed ? 1 : 0;
}
