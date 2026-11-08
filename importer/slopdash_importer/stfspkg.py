"""Reads the files inside an Xbox 360 content package (STFS: the "LIVE",
"CON " and "PIRS" packages): enough for a system update package, such as
2.0.6717's $SystemUpdate\\su20076000_00000000, whose $flash_dash.xex is the
importer's 6717 dash.xex. Read-only: nothing here writes.

Layout (big-endian unless noted):
  0x000 magic; 0x340 header size; 0x379 volume descriptor: 0x37B block
  separation, 0x37C file table block count (u16 LE), 0x37E its first block
  (u24 LE), 0x395 allocated blocks.
Blocks are 0x1000 bytes from the first hash table, (header size + 0xFFF) &
~0xFFF, with a hash table every 0xAA data blocks (level 0) and one per 0x70E4
(level 1). A package whose block separation has bit 0 clear keeps two copies
of every table; which one is current is in the level above (status bit 0x40)
or, for the top table, bit 1 of block separation. A hash entry: SHA-1 (0x14),
status (1), next block (u24). File table entries are 0x40 bytes: name (0x28),
flags (0x28: name length in the low 6 bits, 0x80 a folder, 0x40 contiguous),
block count (0x29, u24 LE), first block (0x2F, u24 LE), size (0x34).
"""
import struct

MAGICS = (b"LIVE", b"CON ", b"PIRS")
BLOCK = 0x1000
PER_TABLE = 0xAA
PER_L1 = 0x70E4


class PackageError(Exception):
    pass


def is_package(head):
    return head[:4] in MAGICS


def _u24le(b, o):
    return b[o] | (b[o + 1] << 8) | (b[o + 2] << 16)


def _u24be(b, o):
    return (b[o] << 16) | (b[o + 1] << 8) | b[o + 2]


class Package:
    def __init__(self, data):
        self.d = data
        if len(data) < 0x1000 or not is_package(data):
            raise PackageError("not an Xbox content package")
        header = struct.unpack_from(">I", data, 0x340)[0]
        self.first = (header + 0xFFF) & ~0xFFF
        sep = data[0x37B]
        self.sep = sep
        self.two = (~sep) & 1                       # 1: two copies of each table
        self.step0 = 0xAC if self.two else 0xAB
        self.step1 = 0x723A if self.two else 0x718F
        self.allocated = struct.unpack_from(">I", data, 0x395)[0]
        self.top_level = 0 if self.allocated <= PER_TABLE else 1 if self.allocated <= PER_L1 else 2
        self.files = self._file_table()

    # -- block numbers -> offsets ---------------------------------------------
    def _backing_data(self, n):
        r = (((n + PER_TABLE) // PER_TABLE) << self.two) + n
        if n < PER_TABLE:
            return r
        if n < PER_L1:
            return r + (((n + PER_L1) // PER_L1) << self.two)
        return (1 << self.two) + r + (((n + PER_L1) // PER_L1) << self.two)

    def _backing_l0(self, n):
        if n < PER_TABLE:
            return 0
        r = (n // PER_TABLE) * self.step0
        if n // PER_L1 == 0:
            return r + (1 << self.two)
        return r + ((n // PER_L1 + 1) << self.two) + (1 << self.two)

    def _backing_l1(self, n):
        return self.step0 if n < PER_L1 else (1 << self.two) + (n // PER_L1) * self.step1

    def _table(self, backing, second):
        return self.first + (backing << 12) + (BLOCK if (second and self.two) else 0)

    def _top(self):
        backing = 0 if self.top_level == 0 else self._backing_l1(0) if self.top_level == 1 else self.step1
        return self._table(backing, bool(self.sep & 2))

    def _l1(self, n):
        if self.top_level == 1:
            return self._top()
        status = self._byte(self._top() + (n // PER_L1) * 0x18 + 0x14)
        return self._table(self._backing_l1(n), bool(status & 0x40))

    def _l0(self, n):
        if self.top_level == 0:
            return self._top()
        status = self._byte(self._l1(n) + ((n // PER_TABLE) % PER_TABLE) * 0x18 + 0x14)
        return self._table(self._backing_l0(n), bool(status & 0x40))

    def _byte(self, at):
        if at >= len(self.d):
            raise PackageError("the package is cut short")
        return self.d[at]

    def _block(self, n):
        at = self.first + (self._backing_data(n) << 12)
        if at + BLOCK > len(self.d):
            raise PackageError("the package is cut short")
        return self.d[at:at + BLOCK]

    def _next(self, n):
        e = self._l0(n) + (n % PER_TABLE) * 0x18
        if e + 0x18 > len(self.d):
            raise PackageError("the package is cut short")
        return _u24be(self.d, e + 0x15)

    def _blocks(self, start, count, contiguous):
        out, n = [], start
        for _ in range(count):
            out.append(n)
            n = n + 1 if contiguous else self._next(n)
        return out

    # -- files ----------------------------------------------------------------------
    def _file_table(self):
        count = struct.unpack_from("<H", self.d, 0x37C)[0]
        first = _u24le(self.d, 0x37E)
        if not count or count > 1024:
            raise PackageError("no file table")
        files = {}
        for b in self._blocks(first, count, False):
            blk = self._block(b)
            for i in range(0, BLOCK, 0x40):
                e = blk[i:i + 0x40]
                flags = e[0x28]
                length = flags & 0x3F
                if not length or flags & 0x80:          # (empty, or a folder)
                    continue
                name = bytes(e[:length]).decode("latin-1")
                files[name] = (_u24le(e, 0x2F), _u24le(e, 0x29), struct.unpack_from(">I", e, 0x34)[0],
                               bool(flags & 0x40))
        return files

    def sizes(self):
        """{file name: size}."""
        return {n: f[2] for n, f in self.files.items()}

    def read(self, name):
        start, count, size, contiguous = self.files[name]
        data = b"".join(self._block(n) for n in self._blocks(start, count, contiguous))
        if len(data) < size:
            raise PackageError("%s is cut short" % name)
        return data[:size]
