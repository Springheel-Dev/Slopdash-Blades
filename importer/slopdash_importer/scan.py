"""Finds the user's dashboard files: every file under the folders they point
at, inside the .zip files there, and inside Xbox content packages (a system
update's su..._00000000: stfspkg). Names don't matter; a file that matches
nothing is ignored.

A file is recognised two ways:
  1. quickly, by size and SHA-1, as it comes off the console (the manifest's
     "original") or decrypted by XexTool (its "sha1");
  2. failing that, an .xex by what it holds (the manifest's "image"): its
     module's image, decrypted and decompressed, by size and SHA-1. Every form
     of a module holds the same image - encrypted, decrypted, decompressed,
     by any tool - so any of them is recognised (xexdecrypt.content_digests).
An encrypted file is decrypted as it's read (xexdecrypt)."""
import hashlib
import json
import os
import zipfile

from . import stfspkg, xexdecrypt

# A content package is looked inside when it has no extension (system updates'
# su20076000_00000000) and is no bigger than this: a whole drive stays quick,
# and a game's large packages aren't read. An .xex is looked inside (2) when
# it's no bigger than this, too.
PACKAGE_MAX = 64 * 1024 * 1024
XEX_HEADER = 0x10000                # an .xex's headers are read from its first bytes

with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "manifest.json"), encoding="utf-8") as _f:
    MANIFEST = json.load(_f)


class Found:
    """A manifest file found: where (a path, a member of a zip, a file in a
    content package, or a file in a package in a zip), and how it was
    recognised ("decrypted", "original" or "contents")."""

    def __init__(self, entry, path, member=None, inner=None, how="decrypted"):
        self.entry = entry
        self.path = path
        self.member = member        # in the zip at path
        self.inner = inner          # in the content package (at path, or the zip's member)
        self.how = how
        self.key_index = None       # the XEX key it was found to take, if that had to be tried

    def raw(self):
        if self.member is None:
            with open(self.path, "rb") as f:
                data = f.read()
        else:
            with zipfile.ZipFile(self.path) as z:
                data = z.read(self.member)
        return stfspkg.Package(data).read(self.inner) if self.inner else data

    def read(self):
        """The file, decrypted if it's an encrypted .xex."""
        data = self.raw()
        return xexdecrypt.decrypt(data, self.key_index) if xexdecrypt.is_encrypted(data) else data

    def where(self):
        parts = [self.path] + [p for p in (self.member, self.inner) if p]
        note = {"original": " (encrypted: decrypted here)", "contents": " (recognised by its contents)"}
        return " > ".join(parts) + note.get(self.how, "")


def might_be_package(name, size):
    return not os.path.splitext(os.path.basename(name))[1] and 0x1000 <= size <= PACKAGE_MAX


def key(entry):
    return "%s/%s" % (entry["set"], entry["name"])


def scan(sources, progress=None):
    """{manifest key: Found} for everything recognised in the given folders,
    files and zips. progress(text) is told what's being looked at."""
    by_size = {}                                # file size: [(entry, how)]
    by_image = {}                               # image size: [entry]
    for e in MANIFEST["files"]:
        by_size.setdefault(e["size"], []).append((e, "decrypted"))
        if "original" in e:
            by_size.setdefault(e["original"]["size"], []).append((e, "original"))
        if "image" in e:
            by_image.setdefault(e["image"]["size"], []).append(e)
    found = {}

    def check(size, digest_of, make):
        """By size and SHA-1 (1): whether it matched."""
        for e, how in by_size.get(size, ()):
            if key(e) in found:
                continue
            if digest_of() in (e["original"]["sha1"] if how == "original" else e["sha1"]):
                f = make(e)
                f.how = how
                found[key(e)] = f
                return True
        return False

    def check_contents(size, header, data_of, make):
        """An .xex by what it holds (2), if its image size is one still wanted."""
        if size > PACKAGE_MAX:
            return
        wanted = [e for e in by_image.get(xexdecrypt.image_size(header), ()) if key(e) not in found]
        if not wanted:
            return
        try:
            digests = xexdecrypt.content_digests(data_of())
        except (xexdecrypt.XexError, OSError, zipfile.BadZipFile, KeyError):
            return
        for e in wanted:
            for key_index, digest in digests:
                if digest in e["image"]["sha1"]:
                    f = make(e)
                    f.how = "contents"
                    f.key_index = key_index
                    found[key(e)] = f
                    return

    def look_at_package(data, path, member=None):
        """The files in a content package, if data is one."""
        if not stfspkg.is_package(data):
            return
        try:
            pkg = stfspkg.Package(data)
            for name, size in pkg.sizes().items():
                if size not in by_size and not name.lower().endswith(".xex"):
                    continue
                if progress:
                    progress(name)
                body = []

                def read(name=name):
                    if not body:
                        body.append(pkg.read(name))
                    return body[0]
                make = (lambda e, n=name: Found(e, path, member, n))
                if not (size in by_size and check(size, lambda: hashlib.sha1(read()).hexdigest(), make)):
                    if read()[:4] == b"XEX2":
                        check_contents(size, read()[:XEX_HEADER], read, make)
        except (stfspkg.PackageError, KeyError, IndexError, ValueError):
            pass                                    # (not a package after all, or a damaged one)

    def look_at_zip(path):
        try:
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    if info.is_dir():
                        continue
                    if progress:
                        progress(info.filename)
                    body = []

                    def read(info=info):
                        if not body:
                            body.append(z.read(info))
                        return body[0]
                    make = (lambda e, m=info.filename: Found(e, path, m))
                    matched = info.file_size in by_size and check(
                        info.file_size, lambda: hashlib.sha1(read()).hexdigest(), make)
                    if not matched and info.file_size <= PACKAGE_MAX:
                        with z.open(info) as f:
                            header = f.read(XEX_HEADER)
                        if header[:4] == b"XEX2":
                            check_contents(info.file_size, header, read, make)
                    if might_be_package(info.filename, info.file_size):
                        look_at_package(read(), path, info.filename)
                    body.clear()
        except (zipfile.BadZipFile, OSError):
            pass

    def look_at_file(path):
        if path.lower().endswith(".zip"):
            look_at_zip(path)
            return
        try:
            size = os.path.getsize(path)
            with open(path, "rb") as f:
                header = f.read(XEX_HEADER)
        except OSError:
            return
        if might_be_package(path, size) and stfspkg.is_package(header[:4]):
            try:
                with open(path, "rb") as f:
                    look_at_package(f.read(), path)
            except OSError:
                pass
        if size not in by_size and header[:4] != b"XEX2":
            return
        if progress:
            progress(path)
        digest = []

        def d():
            if not digest:
                h = hashlib.sha1()
                with open(path, "rb") as f:
                    for chunk in iter(lambda: f.read(1 << 20), b""):
                        h.update(chunk)
                digest.append(h.hexdigest())
            return digest[0]

        def read():
            with open(path, "rb") as f:
                return f.read()
        make = (lambda e: Found(e, path))
        if not (size in by_size and check(size, d, make)) and header[:4] == b"XEX2":
            check_contents(size, header, read, make)

    for src in sources:
        if os.path.isdir(src):
            for folder, _, names in os.walk(src):
                for n in names:
                    look_at_file(os.path.join(folder, n))
        elif os.path.isfile(src):
            look_at_file(src)
    return found


def checklist(found):
    """Per set: [(entry, Found or None)], in the manifest's order."""
    sets = {}
    for e in MANIFEST["files"]:
        sets.setdefault(e["set"], []).append((e, found.get(key(e))))
    return sets


def themes(found):
    """What can be built: {"blades": (ready, missing required names, missing
    recommended names)}. Blades is the one theme the importer builds."""
    missing_req, missing_rec = [], []
    for e in MANIFEST["files"]:
        if key(e) not in found:
            label = "%s %s" % (e["set"], e["name"])
            (missing_req if e["need"] == "required" else missing_rec).append(label)
    return {"blades": (not missing_req, missing_req, missing_rec)}
