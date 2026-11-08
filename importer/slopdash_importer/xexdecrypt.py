"""The SlopDash Importer's XEX decryption, and what identifies an .xex.

decrypt(): a retail .xex as it comes off the console (its body encrypted) made
into the same file unencrypted, which is what the importer's build reads.
Nothing else changes: the headers stay where they are, the body is decrypted
in place, and the file format header says "not encrypted". Compression (none,
basic or LZX) is left alone.

The key: the file's own image key (security info + 0x150), decrypted with one
of the XEX keys (AES-128-ECB); the body (from the PE data offset to the end) is
AES-128-CBC with that key and a zero IV. Most of 2.0.1888's files use the
retail key, its xam.xex the older XEX1 key: each is tried, and the result
says which (LZX: the first block's SHA-1 in the file format header; none or
basic: the image starts with the PE's "MZ").

content_digests(): the SHA-1 of the module's image (decrypted and decompressed).
The same module comes in many files - encrypted as on the console, or
decrypted, or decompressed too, by XexTool or another tool, each laying out
its headers its own way - and every one of them holds the same image. The
importer recognises a module by it.

As a tool:  python -m slopdash_importer.xexdecrypt <in.xex> <out.xex>
"""
import contextlib
import hashlib
import io
import os
import struct
import sys

from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

try:
    import xexres                       # SlopDash's tools/ (bundled in the exe)
except ImportError:
    sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
    import xexres

# The XEX keys, publicly documented (Free60, Xenia, XexTool): retail, then
# XEX1 (early system files, such as 2.0.1888's xam.xex).
KEYS = (bytes.fromhex("20B185A59D28FDC340583FBB0896BF91"), bytes.fromhex("A26C10F71FD935E98B99922CE9321572"))
FILE_FORMAT = 0x000003FF


class XexError(ValueError):
    pass


def _layout(data):
    if len(data) < 24 or data[:4] != b"XEX2":
        raise XexError("not an XEX2 file")
    data_off, sec_off, count = struct.unpack(">I", data[8:12])[0], struct.unpack(">I", data[16:20])[0], \
        struct.unpack(">I", data[20:24])[0]
    if 24 + 8 * count > len(data):
        raise XexError("damaged XEX headers")
    headers = dict(struct.unpack(">II", data[24 + 8 * i:32 + 8 * i]) for i in range(count))
    fmt = headers.get(FILE_FORMAT)
    if fmt is None or not (0 < data_off <= len(data)) or sec_off + 0x160 > len(data) or fmt + 8 > len(data):
        raise XexError("damaged XEX headers")
    return data_off, sec_off, fmt


def image_size(header):
    """The module's image size, from an .xex's headers (the first bytes of the
    file, up to its data); None if it isn't an XEX2 file."""
    try:
        _, sec_off, _ = _layout(header)
    except XexError:
        return None
    return struct.unpack(">I", header[sec_off + 4:sec_off + 8])[0]


def is_encrypted(data):
    """Whether data is an XEX2 file with an encrypted body."""
    try:
        _, _, fmt = _layout(data)
    except XexError:
        return False
    return struct.unpack(">H", data[fmt + 4:fmt + 6])[0] == 1


def decrypt(data, key_index=None):
    """data (an encrypted XEX2) with its body decrypted; XexError if it isn't
    one, or no key decrypts it. key_index: the key (in KEYS) to use, when it's
    known (content_digests says which); otherwise each is tried."""
    data_off, sec_off, fmt = _layout(data)
    enc, comp = struct.unpack(">HH", data[fmt + 4:fmt + 8])
    if enc != 1:
        raise XexError("not encrypted")
    if comp > 2:
        raise XexError("unknown compression %d" % comp)
    body_len = (len(data) - data_off) // 16 * 16
    if comp == 2:
        size, digest = struct.unpack(">I20s", data[fmt + 12:fmt + 36])      # the first LZX block
        if size > body_len:
            raise XexError("damaged XEX headers")
    for xex_key in (KEYS if key_index is None else KEYS[key_index:key_index + 1]):
        image_key = Cipher(algorithms.AES(xex_key), modes.ECB()).decryptor().update(
            data[sec_off + 0x150:sec_off + 0x160])
        if comp == 2:
            first = Cipher(algorithms.AES(image_key), modes.CBC(b"\0" * 16)).decryptor().update(
                data[data_off:data_off + size])
            if hashlib.sha1(first).digest() == digest:
                break
        else:
            # No hash to check: the key given, or an image that starts as a
            # program ("MZ") or a resource-only module's package ("XUIZ") does.
            first = Cipher(algorithms.AES(image_key), modes.CBC(b"\0" * 16)).decryptor().update(
                data[data_off:data_off + 16])
            if key_index is not None or first[:2] == b"MZ" or first[:4] == b"XUIZ":
                break
    else:
        raise XexError("no XEX key decrypts it (not an Xbox 360 system file?)")
    plain = Cipher(algorithms.AES(image_key), modes.CBC(b"\0" * 16)).decryptor().update(
        data[data_off:data_off + body_len])
    out = bytearray(data)
    out[data_off:data_off + body_len] = plain
    out[fmt + 4:fmt + 6] = b"\0\0"      # encryption: none
    return bytes(out)


def plain(data):
    """data decrypted if it's encrypted, else as it is."""
    return decrypt(data) if is_encrypted(data) else data


def _image_sha1(plain_data):
    try:
        with contextlib.redirect_stdout(io.StringIO()):
            image, _, _ = xexres.xex_image(plain_data)
    except Exception as e:                  # (a damaged or unknown file)
        raise XexError("can't be read: %s" % e)
    return hashlib.sha1(image).hexdigest()


def content_digests(data):
    """[(key index or None, SHA-1 of the module's image)]: the image the file
    holds, the same for every form of the same module. An encrypted file
    without LZX's block hash can't say which key it takes, so one entry per
    key that decrypts it: only the right one's image will match a known one.
    XexError if it can't be read at all."""
    if not is_encrypted(data):
        return [(None, _image_sha1(data))]
    _, _, fmt = _layout(data)
    if struct.unpack(">H", data[fmt + 6:fmt + 8])[0] == 2:
        return [(None, _image_sha1(decrypt(data)))]
    out = []
    for i in range(len(KEYS)):
        try:
            out.append((i, _image_sha1(decrypt(data, i))))
        except XexError:
            pass
    if not out:
        raise XexError("can't be read")
    return out


def content_sha1(data):
    """The SHA-1 (hex) of the module's image (the first candidate's)."""
    return content_digests(data)[0][1]


def main(argv):
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[-1].strip())
        return 2
    with open(argv[1], "rb") as f:
        data = f.read()
    try:
        out = decrypt(data)
    except XexError as e:
        print("%s: %s" % (argv[1], e))
        return 1
    with open(argv[2], "wb") as f:
        f.write(out)
    print("%s: decrypted to %s" % (argv[1], argv[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
