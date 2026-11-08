"""Prototype: decrypt + decompress a retail XEX2 (normal/LZX compression) and
dump its embedded resources. Scratch tool to learn the format; the real code
will live in SlopDash's core."""
import hashlib, struct, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

RETAIL = bytes.fromhex("20B185A59D28FDC340583FBB0896BF91")


class Bits:
    def __init__(s, data):
        s.d = data; s.p = 0; s.buf = 0; s.n = 0
    def fill(s):
        while s.n <= 16:
            if s.p + 1 < len(s.d):
                w = s.d[s.p] | (s.d[s.p + 1] << 8)
            else:
                w = 0
            s.p += 2
            s.buf = (s.buf << 16) | w; s.n += 16
    def peek(s, k):
        s.fill()
        return (s.buf >> (s.n - k)) & ((1 << k) - 1)
    def skip(s, k):
        s.n -= k; s.buf &= (1 << s.n) - 1
    def read(s, k):
        if k == 0: return 0
        if k > 16:
            hi = s.read(16); return (hi << (k - 16)) | s.read(k - 16)
        v = s.peek(k); s.skip(k); return v
    def align16(s):
        # drop to the next 16-bit boundary of the input stream
        s.skip(s.n & 15)


class Tree:
    def __init__(s, lengths, bits=16):
        s.bits = bits
        s.table = [None] * (1 << bits)
        code = 0
        pairs = sorted((l, i) for i, l in enumerate(lengths) if l)
        s.empty = not pairs
        prev = 0
        for l, sym in pairs:
            code <<= (l - prev); prev = l
            start = code << (bits - l)
            entry = (sym, l)
            for j in range(start, start + (1 << (bits - l))):
                s.table[j] = entry
            code += 1
    def decode(s, b):
        sym, l = s.table[b.peek(s.bits)]
        b.skip(l); return sym


EXTRA = []
BASE = []
_b = 0
for i in range(51):
    e = 0 if i < 4 else min((i - 2) // 2, 17)
    EXTRA.append(e); BASE.append(_b); _b += 1 << e


def read_lengths(b, lens, first, last):
    pre = Tree([b.read(4) for _ in range(20)])
    i = first
    while i < last:
        z = pre.decode(b)
        if z == 17:
            n = 4 + b.read(4); lens[i:i + n] = [0] * n; i += n
        elif z == 18:
            n = 20 + b.read(5); lens[i:i + n] = [0] * n; i += n
        elif z == 19:
            n = 4 + b.read(1); z = pre.decode(b)
            v = (lens[i] - z) % 17
            lens[i:i + n] = [v] * n; i += n
        else:
            lens[i] = (lens[i] - z) % 17; i += 1


def lzx(data, window_bits, out_len, ref=b""):
    """ref: a delta patch's old data, which matches may reach back into (it
    sits just before the output, as libmspack places reference data)."""
    b = Bits(data)
    slots = {15: 30, 16: 32, 17: 34, 18: 36, 19: 38, 20: 42, 21: 50}[window_bits]
    main_n = 256 + slots * 8
    main_lens = [0] * main_n; len_lens = [0] * 249
    base = len(ref)
    out = bytearray(ref)
    out_len += base
    r0 = r1 = r2 = 1
    # The E8 ("Intel call") translation: dash.xex doesn't use it, the system
    # files (hud.xex...) do. Undone per 32 KB frame at the end (as libmspack).
    e8_size = b.read(32) if b.read(1) else 0
    remaining = 0; btype = 0
    frame_end = base + 32768
    while len(out) < out_len:
        if remaining == 0:
            btype = b.read(3)
            remaining = (b.read(16) << 8) | b.read(8)
            if btype == 2:
                aligned = Tree([b.read(3) for _ in range(8)], 7)
            if btype in (1, 2):
                read_lengths(b, main_lens, 0, 256)
                read_lengths(b, main_lens, 256, main_n)
                main = Tree(main_lens)
                read_lengths(b, len_lens, 0, 249)
                length = Tree(len_lens)
            elif btype == 3:
                b.fill()
                if b.n > 16: b.p -= 2
                b.n = 0; b.buf = 0
                r0, r1, r2 = struct.unpack_from("<III", data, b.p); b.p += 12
            else:
                raise ValueError("bad LZX block type %d at out %d" % (btype, len(out)))
        # decode up to the end of this block or frame
        stop = min(len(out) + remaining, frame_end, out_len)
        if btype == 3:
            n = stop - len(out)
            out += data[b.p:b.p + n]; b.p += n; remaining -= n
            if remaining == 0 and (b.p & 1): b.p += 1      # padding after odd-length block
        else:
            start = len(out)
            while len(out) < stop:
                sym = main.decode(b)
                if sym < 256:
                    out.append(sym); continue
                sym -= 256
                mlen = sym & 7
                if mlen == 7: mlen += length.decode(b)
                mlen += 2
                slot = sym >> 3
                if slot == 0: off = r0
                elif slot == 1: off = r1; r1 = r0; r0 = off
                elif slot == 2: off = r2; r2 = r0; r0 = off
                else:
                    e = EXTRA[slot]
                    off = BASE[slot] - 2
                    if btype == 2 and e >= 3:
                        if e > 3: off += b.read(e - 3) << 3
                        off += aligned.decode(b)
                    elif e:
                        off += b.read(e)
                    r2 = r1; r1 = r0; r0 = off
                src = len(out) - off
                if off >= mlen:
                    out += out[src:src + mlen]
                else:
                    for k in range(mlen): out.append(out[src + k])
            remaining -= len(out) - start
            if remaining < 0:
                raise ValueError("match ran past block end")
        if len(out) >= frame_end:
            frame_end += 32768
            if btype != 3 and b.n: b.align16()
    out = out[base:out_len]
    if e8_size:
        undo_e8(out, e8_size)
    return bytes(out)


def undo_e8(out, filesize):
    for frame_start in range(0, len(out), 32768):
        frame_size = min(32768, len(out) - frame_start)
        if frame_start // 32768 >= 32768 or frame_size <= 10:
            continue
        i = frame_start
        end = frame_start + frame_size - 10
        while i < end:
            if out[i] != 0xE8:
                i += 1
                continue
            curpos = i
            abs_off = struct.unpack_from("<i", out, i + 1)[0]
            if -curpos <= abs_off < filesize:
                rel = abs_off - curpos if abs_off >= 0 else abs_off + filesize
                struct.pack_into("<i", out, i + 1, rel)
            i += 5


def xex_image(d):
    dataoff = struct.unpack(">I", d[8:12])[0]; secoff = struct.unpack(">I", d[16:20])[0]
    n = struct.unpack(">I", d[20:24])[0]
    hdrs = dict(struct.unpack(">II", d[24 + 8 * i:32 + 8 * i]) for i in range(n))
    image_size = struct.unpack(">I", d[secoff + 4:secoff + 8])[0]
    load = struct.unpack(">I", d[secoff + 0x110:secoff + 0x114])[0]
    key = Cipher(algorithms.AES(RETAIL), modes.ECB()).decryptor().update(d[secoff + 0x150:secoff + 0x160])
    body = d[dataoff:]; body = body[:len(body) // 16 * 16]
    plain = Cipher(algorithms.AES(key), modes.CBC(b"\0" * 16)).decryptor().update(body)
    fmt = hdrs[0x3FF]
    info_size, enc, comp = struct.unpack(">IHH", d[fmt:fmt + 8])
    if enc == 0:
        plain = body                    # not encrypted (system files, or decrypted)
    if comp in (0, 1):
        if comp == 0:                   # none: the image as it is
            image = bytes(plain[:image_size])
        else:                           # basic: runs of [data size][zeros size]
            image = bytearray(); pos = 0
            for i in range((info_size - 8) // 8):
                dsz, zsz = struct.unpack(">II", d[fmt + 8 + 8 * i:fmt + 16 + 8 * i])
                image += plain[pos:pos + dsz] + b"\0" * zsz; pos += dsz
            image = bytes(image[:image_size])
        image = image.ljust(image_size, b"\0")
        return image, load, _resources(d, hdrs, load)
    if comp != 2:
        raise ValueError("XEX compression %d isn't none, basic or LZX" % comp)
    _, _, _, window, size, digest = struct.unpack(">IHHII20s", d[fmt:fmt + 36])
    # walk the hash-chained blocks, collecting the LZX chunks
    stream = bytearray(); pos = 0
    while size:
        block = plain[pos:pos + size]
        if hashlib.sha1(block).digest() != digest:
            raise ValueError("block hash mismatch at %#x" % pos)
        nsize, digest = struct.unpack(">I20s", block[:24])
        p = 24
        while True:
            c = struct.unpack(">H", block[p:p + 2])[0]; p += 2
            if c == 0: break
            stream += block[p:p + c]; p += c
        pos += size; size = nsize
    print("compressed stream", len(stream), "window", hex(window))
    image = lzx(bytes(stream), window.bit_length() - 1, image_size)
    return image, load, _resources(d, hdrs, load)


def _resources(d, hdrs, load):
    """[(name, offset in the image, size)]: the module's resources (none if it has none)."""
    res = []
    o = hdrs.get(0x2FF)
    if o is None:
        return res
    rs = struct.unpack(">I", d[o:o + 4])[0]
    for i in range((rs - 4) // 16):
        name, addr, sz = struct.unpack(">8sII", d[o + 4 + 16 * i:o + 20 + 16 * i])
        res.append((name.rstrip(b"\0").decode(), addr - load, sz))
    return res


if __name__ == "__main__":
    import os
    d = open(sys.argv[1], "rb").read()
    image, load, res = xex_image(d)
    print("image", len(image), "load", hex(load), "PE magic", image[:2])
    os.makedirs(sys.argv[2], exist_ok=True)
    for name, off, sz in res:
        blob = image[off:off + sz]
        open(os.path.join(sys.argv[2], name + ".bin"), "wb").write(blob)
        print(name, sz, blob[:4])
