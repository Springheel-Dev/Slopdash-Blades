#include "core/sevenzip.h"

#ifdef _XBOX
#include <xtl.h>
#else
#include <windows.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

extern "C" {
#include "7z.h"
#include "7zCrc.h"
#include "Bra.h"
#include "Delta.h"
#include "Lzma2Dec.h"
#include "LzmaDec.h"
}

namespace sevenzip {

namespace {

// Method IDs (as 7zDec.c has them).
const UInt32 kCopy = 0, kLzma = 0x30101, kLzma2 = 0x21, kDelta = 3, kBcj = 0x3030103, kPpc = 0x3030205,
             kIa64 = 0x3030401, kArm = 0x3030501, kArmt = 0x3030701, kSparc = 0x3030805;
const UInt64 kInMemoryMax = 64u * 1024 * 1024;
const size_t kInChunk = 1 << 16, kOutChunk = 1 << 18;

void* AllocFn(ISzAllocPtr, size_t size) { return size ? malloc(size) : NULL; }
void FreeFn(ISzAllocPtr, void* p) { free(p); }
const ISzAlloc kAlloc = { AllocFn, FreeFn };

struct FileIn
{
    ISeekInStream vt;
    HANDLE h;
};

SRes FileRead(ISeekInStreamPtr pp, void* buf, size_t* size)
{
    const FileIn* p = (const FileIn*)pp;
    DWORD got = 0;
    if (*size && !ReadFile(p->h, buf, (DWORD)*size, &got, NULL))
        return SZ_ERROR_READ;
    *size = got;
    return SZ_OK;
}

SRes FileSeek(ISeekInStreamPtr pp, Int64* pos, ESzSeek origin)
{
    const FileIn* p = (const FileIn*)pp;
    LONG high = (LONG)(*pos >> 32);
    DWORD method = origin == SZ_SEEK_SET ? FILE_BEGIN : origin == SZ_SEEK_CUR ? FILE_CURRENT : FILE_END;
    DWORD low = SetFilePointer(p->h, (LONG)(*pos & 0xFFFFFFFF), &high, method);
    if (low == INVALID_SET_FILE_POINTER && GetLastError() != NO_ERROR)
        return SZ_ERROR_READ;
    *pos = ((Int64)high << 32) | low;
    return SZ_OK;
}

const char* ResText(SRes r)
{
    switch (r)
    {
    case SZ_ERROR_DATA: return "the archive is damaged";
    case SZ_ERROR_MEM: return "not enough memory";
    case SZ_ERROR_CRC: return "a checksum is wrong";
    case SZ_ERROR_UNSUPPORTED: return "it uses a method that isn't supported";
    case SZ_ERROR_NO_ARCHIVE: return "it isn't a 7z archive";
    case SZ_ERROR_ARCHIVE: return "the archive is damaged";
    case SZ_ERROR_INPUT_EOF: return "the archive is cut short";
    case SZ_ERROR_READ: return "it couldn't be read";
    default: return "it couldn't be unpacked";
    }
}

std::string Join(const std::string& dir, const std::string& name)
{
    if (dir.empty())
        return name;
    char last = dir[dir.size() - 1];
    return last == '\\' || last == '/' ? dir + name : dir + "\\" + name;
}

void MakeDirs(const std::string& path)
{
    // Every parent in turn ("hdd:\\a\\b": "hdd:\\a", then "hdd:\\a\\b").
    for (size_t i = 0; i < path.size(); i++)
        if ((path[i] == '\\' || path[i] == '/') && i > 0 && path[i - 1] != ':')
            CreateDirectoryA(path.substr(0, i).c_str(), NULL);
    CreateDirectoryA(path.c_str(), NULL);
}

// The archive's name for file i as a relative path ("a\\b.xex"), or false
// for one that could reach outside the destination.
bool SafeName(const CSzArEx& db, UInt32 i, std::string& out)
{
    size_t len = SzArEx_GetFileNameUtf16(&db, i, NULL);
    std::vector<UInt16> w(len + 1);
    SzArEx_GetFileNameUtf16(&db, i, &w[0]);
    out.clear();
    for (size_t k = 0; k < len && w[k]; k++)
    {
        UInt16 c = w[k];
        out += c == '/' ? '\\' : (c < 0x80 ? (char)c : '_');
    }
    if (out.empty() || out[0] == '\\' || out.find(':') != std::string::npos)
        return false;
    // no ".." component
    size_t start = 0;
    for (;;)
    {
        size_t end = out.find('\\', start);
        std::string part = out.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part == "..")
            return false;
        if (end == std::string::npos)
            break;
        start = end + 1;
    }
    return true;
}

// The files of one block, written as its data arrives.
struct Sink
{
    const CSzArEx* db;
    std::string dest;
    UInt32 folder;
    UInt32 file;                // current file index
    UInt64 left;                // its bytes still to come
    HANDLE h;
    UInt32 crc;
    unsigned long long* done;
    unsigned long long total;
    Progress progress;
    void* context;
    std::string error;

    bool NextFile()
    {
        for (; file < db->NumFiles; file++)
        {
            if (db->FileToFolder[file] != folder || SzArEx_IsDir(db, file) || SzArEx_GetFileSize(db, file) == 0)
                continue;
            std::string name;
            if (!SafeName(*db, file, name))
            {
                error = "the archive holds an unsafe file name";
                return false;
            }
            std::string path = Join(dest, name);
            size_t slash = path.find_last_of('\\');
            if (slash != std::string::npos)
                MakeDirs(path.substr(0, slash));
            h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE)
            {
                char b[64];
                _snprintf(b, sizeof(b), " (%u)", (unsigned)GetLastError());
                error = "couldn't create " + path + b;
                return false;
            }
            left = SzArEx_GetFileSize(db, file);
            crc = CRC_INIT_VAL;
            return true;
        }
        error = "the archive's data runs past its files";
        return false;
    }

    bool Write(const Byte* data, size_t size)
    {
        while (size)
        {
            if (h == INVALID_HANDLE_VALUE && !NextFile())
                return false;
            size_t n = (size_t)(left < size ? left : size);
            DWORD put = 0;
            if (!WriteFile(h, data, (DWORD)n, &put, NULL) || put != n)
            {
                error = "couldn't write (the drive may be full)";
                return false;
            }
            crc = CrcUpdate(crc, data, n);
            data += n;
            size -= n;
            left -= n;
            *done += n;
            if (left == 0)
            {
                CloseHandle(h);
                h = INVALID_HANDLE_VALUE;
                if (SzBitWithVals_Check(&db->CRCs, file) && db->CRCs.Vals[file] != CRC_GET_DIGEST(crc))
                {
                    error = "a file's checksum is wrong (the download may be damaged)";
                    return false;
                }
                file++;
            }
            if (progress && !progress(context, *done, total))
            {
                error = "cancelled";
                return false;
            }
        }
        return true;
    }

    void Close()
    {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
        h = INVALID_HANDLE_VALUE;
    }
};

// A filter after the decoder (branch converters keep a few bytes back until
// more data shows whether they're part of an instruction).
struct Filter
{
    UInt32 method;
    UInt32 pc, x86State;
    Byte delta[DELTA_STATE_SIZE];
    unsigned deltaDist;
    std::vector<Byte> held;

    void Init(UInt32 m, const Byte* props, unsigned propsSize)
    {
        method = m;
        pc = 0;
        x86State = Z7_BRANCH_CONV_ST_X86_STATE_INIT_VAL;
        Delta_Init(delta);
        deltaDist = m == kDelta && propsSize >= 1 ? props[0] + 1u : 1u;
        held.clear();
    }

    // Converts what it can of held + data; out gets the finished bytes.
    void Run(const Byte* data, size_t size, bool last, std::vector<Byte>& out)
    {
        out.clear();
        if (method == kCopy)
        {
            out.assign(data, data + size);
            return;
        }
        if (method == kDelta)
        {
            out.assign(data, data + size);
            if (size)
                Delta_Decode(delta, deltaDist, &out[0], size);
            return;
        }
        held.insert(held.end(), data, data + size);
        size_t n = held.size(), done = 0;
        if (n)
        {
            Byte* p = &held[0];
            Byte* end = p;
            switch (method)
            {
            case kBcj: end = z7_BranchConvSt_X86_Dec(p, n, pc, &x86State); break;
            case kPpc: end = z7_BranchConv_PPC_Dec(p, n, pc); break;
            case kIa64: end = z7_BranchConv_IA64_Dec(p, n, pc); break;
            case kArm: end = z7_BranchConv_ARM_Dec(p, n, pc); break;
            case kArmt: end = z7_BranchConv_ARMT_Dec(p, n, pc); break;
            case kSparc: end = z7_BranchConv_SPARC_Dec(p, n, pc); break;
            }
            done = (size_t)(end - p);
        }
        if (last)
            done = n;               // the tail is left as it is
        pc += (UInt32)done;
        out.assign(held.begin(), held.begin() + done);
        held.erase(held.begin(), held.begin() + done);
    }
};

bool IsFilter(UInt32 m)
{
    return m == kDelta || m == kBcj || m == kPpc || m == kIa64 || m == kArm || m == kArmt || m == kSparc;
}

bool IsStreamable(UInt32 m)
{
    return m == kCopy || m == kLzma || m == kLzma2;
}

// One block (folder) streamed from its packed bytes to the sink.
bool StreamFolder(HANDLE file, const CSzArEx& db, UInt32 fi, const CSzFolder& f, const Byte* coderData,
                  Sink& sink)
{
    unsigned m = f.NumCoders == 1 ? 0 : 1;
    const CSzCoderInfo& main = f.Coders[m];
    const Byte* props = coderData + main.PropsOffset;
    UInt64 unpack = db.db.CoderUnpackSizes[db.db.FoToCoderUnpackSizes[fi] + m];
    UInt32 packIndex = db.db.FoStartPackStreamIndex[fi];
    UInt64 packPos = db.dataPos + db.db.PackPositions[packIndex];
    UInt64 packLeft = db.db.PackPositions[packIndex + 1] - db.db.PackPositions[packIndex];

    Filter filter;
    if (f.NumCoders == 2)
        filter.Init(f.Coders[0].MethodID, coderData + f.Coders[0].PropsOffset, f.Coders[0].PropsSize);
    else
        filter.Init(kCopy, NULL, 0);

    LONG high = (LONG)(packPos >> 32);
    if (SetFilePointer(file, (LONG)(packPos & 0xFFFFFFFF), &high, FILE_BEGIN) == INVALID_SET_FILE_POINTER &&
        GetLastError() != NO_ERROR)
    {
        sink.error = "the archive couldn't be read";
        return false;
    }

    CLzmaDec lzma;
    CLzma2Dec lzma2;
    LzmaDec_Construct(&lzma);
    Lzma2Dec_Construct(&lzma2);
    SRes r = SZ_OK;
    if (main.MethodID == kLzma)
    {
        r = LzmaDec_Allocate(&lzma, props, main.PropsSize, &kAlloc);
        LzmaDec_Init(&lzma);
    }
    else if (main.MethodID == kLzma2)
    {
        r = main.PropsSize == 1 ? Lzma2Dec_Allocate(&lzma2, props[0], &kAlloc) : SZ_ERROR_UNSUPPORTED;
        Lzma2Dec_Init(&lzma2);
    }
    if (r != SZ_OK)
    {
        sink.error = std::string("it couldn't be unpacked: ") + ResText(r);
        return false;
    }

    std::vector<Byte> in(kInChunk), out(kOutChunk), filtered;
    size_t inPos = 0, inSize = 0;
    UInt64 produced = 0;
    bool ok = true;
    while (ok && produced < unpack)
    {
        if (inPos == inSize && packLeft)
        {
            DWORD want = (DWORD)(packLeft < kInChunk ? packLeft : kInChunk), got = 0;
            if (!ReadFile(file, &in[0], want, &got, NULL) || got == 0)
            {
                sink.error = "the archive is cut short";
                ok = false;
                break;
            }
            inPos = 0;
            inSize = got;
            packLeft -= got;
        }
        SizeT outLen = (SizeT)(unpack - produced < kOutChunk ? unpack - produced : kOutChunk);
        SizeT inLen = inSize - inPos;
        ELzmaStatus status = LZMA_STATUS_NOT_SPECIFIED;
        if (main.MethodID == kCopy)
        {
            outLen = inLen < outLen ? inLen : outLen;
            memcpy(&out[0], &in[inPos], outLen);
            inLen = outLen;
        }
        else if (main.MethodID == kLzma)
            r = LzmaDec_DecodeToBuf(&lzma, &out[0], &outLen, inLen ? &in[inPos] : NULL, &inLen, LZMA_FINISH_ANY,
                                    &status);
        else
            r = Lzma2Dec_DecodeToBuf(&lzma2, &out[0], &outLen, inLen ? &in[inPos] : NULL, &inLen, LZMA_FINISH_ANY,
                                     &status);
        if (r != SZ_OK)
        {
            sink.error = std::string("it couldn't be unpacked: ") + ResText(r);
            ok = false;
            break;
        }
        inPos += inLen;
        produced += outLen;
        if (outLen == 0 && inLen == 0 && (inPos < inSize || !packLeft))
        {
            sink.error = "the archive is damaged (its data ends early)";
            ok = false;
            break;
        }
        filter.Run(&out[0], outLen, produced == unpack, filtered);
        if (!filtered.empty() && !sink.Write(&filtered[0], filtered.size()))
            ok = false;
    }
    LzmaDec_Free(&lzma, &kAlloc);
    Lzma2Dec_Free(&lzma2, &kAlloc);
    return ok;
}

// A block with methods that don't stream: decoded whole, when small enough.
bool MemoryFolder(const CSzArEx& db, UInt32 fi, ILookInStreamPtr look, Sink& sink)
{
    UInt64 size = SzAr_GetFolderUnpackSize(&db.db, fi);
    if (size > kInMemoryMax)
    {
        sink.error = "it uses a compression method SlopDash can only unpack for archives up to 64 MB";
        return false;
    }
    Byte* buf = (Byte*)malloc((size_t)size + 1);
    if (!buf)
    {
        sink.error = "not enough memory to unpack it";
        return false;
    }
    SRes r = SzAr_DecodeFolder(&db.db, fi, look, db.dataPos, buf, (size_t)size, &kAlloc);
    bool ok = r == SZ_OK;
    if (!ok)
        sink.error = std::string("it couldn't be unpacked: ") + ResText(r);
    else
        ok = sink.Write(buf, (size_t)size);
    free(buf);
    return ok;
}

}  // namespace

bool Extract(const std::string& archive, const std::string& destDir, Progress progress, void* context,
             std::string& error)
{
    static bool crcReady = false;
    if (!crcReady)
    {
        CrcGenerateTable();
        crcReady = true;
    }
    FileIn in;
    in.vt.Read = FileRead;
    in.vt.Seek = FileSeek;
    in.h = CreateFileA(archive.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (in.h == INVALID_HANDLE_VALUE)
    {
        error = "couldn't open the download";
        return false;
    }
    std::vector<Byte> lookBuf(1 << 16);
    CLookToRead2 look;
    LookToRead2_CreateVTable(&look, 0);
    look.buf = &lookBuf[0];
    look.bufSize = lookBuf.size();
    look.realStream = &in.vt;
    LookToRead2_INIT(&look);

    CSzArEx db;
    SzArEx_Init(&db);
    SRes r = SzArEx_Open(&db, &look.vt, &kAlloc, &kAlloc);
    if (r != SZ_OK)
    {
        error = std::string("couldn't read the archive: ") + ResText(r);
        CloseHandle(in.h);
        return false;
    }

    unsigned long long total = 0, done = 0;
    for (UInt32 i = 0; i < db.NumFiles; i++)
        if (!SzArEx_IsDir(&db, i))
            total += SzArEx_GetFileSize(&db, i);

    MakeDirs(destDir);
    bool ok = true;
    // Folders and empty files first (they have no data).
    for (UInt32 i = 0; ok && i < db.NumFiles; i++)
    {
        bool dir = SzArEx_IsDir(&db, i) != 0;
        if (!dir && SzArEx_GetFileSize(&db, i) != 0)
            continue;
        std::string name;
        if (!SafeName(db, i, name))
        {
            error = "the archive holds an unsafe file name";
            ok = false;
            break;
        }
        std::string path = Join(destDir, name);
        if (dir)
            MakeDirs(path);
        else
        {
            size_t slash = path.find_last_of('\\');
            if (slash != std::string::npos)
                MakeDirs(path.substr(0, slash));
            HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (h == INVALID_HANDLE_VALUE)
            {
                error = "couldn't create " + path;
                ok = false;
            }
            else
                CloseHandle(h);
        }
    }

    for (UInt32 fi = 0; ok && fi < db.db.NumFolders; fi++)
    {
        const Byte* coderData = db.db.CodersData + db.db.FoCodersOffsets[fi];
        CSzData sd = { coderData, db.db.FoCodersOffsets[fi + 1] - db.db.FoCodersOffsets[fi] };
        CSzFolder f;
        r = SzGetNextFolderItem(&f, &sd);
        if (r != SZ_OK)
        {
            error = std::string("couldn't read the archive: ") + ResText(r);
            ok = false;
            break;
        }
        Sink sink;
        sink.db = &db;
        sink.dest = destDir;
        sink.folder = fi;
        sink.file = db.FolderToFile[fi];
        sink.left = 0;
        sink.h = INVALID_HANDLE_VALUE;
        sink.crc = CRC_INIT_VAL;
        sink.done = &done;
        sink.total = total;
        sink.progress = progress;
        sink.context = context;
        bool streamable = f.NumPackStreams == 1 &&
                          ((f.NumCoders == 1 && IsStreamable(f.Coders[0].MethodID)) ||
                           (f.NumCoders == 2 && IsFilter(f.Coders[0].MethodID) &&
                            IsStreamable(f.Coders[1].MethodID) && f.Coders[0].NumStreams == 1 &&
                            f.Coders[1].NumStreams == 1));
        ok = streamable ? StreamFolder(in.h, db, fi, f, coderData, sink) : MemoryFolder(db, fi, &look.vt, sink);
        if (ok && sink.h != INVALID_HANDLE_VALUE)
        {
            sink.error = "the archive's data ends before its files do";
            ok = false;
        }
        sink.Close();
        if (!ok)
            error = sink.error;
    }

    SzArEx_Free(&db, &kAlloc);
    CloseHandle(in.h);
    return ok;
}

}  // namespace sevenzip
