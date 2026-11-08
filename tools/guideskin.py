"""Builds the "Blades 6717" Guide skin: the Blades-era Guide's skin (2.0.6717
huduiskin.xex, skin.xur, XUIB version 5; rebuilt from the 6717 system update
with tools/xexpatch.py), plus stand-ins for the visuals the current Guide
(17559) uses that the 6717 skin never had, each a copy of its nearest 6717
visual under the new name.

    python tools/guideskin.py <6717 skin.xur> <out skin.xur>

Each top-level visual is a self-contained record in DATA (class XuiVisual, its
Id the first property); tools/xur.py finds where each starts and ends. A
stand-in is that record copied with its Id pointing at a new string, appended
to the canvas's children. Everything else (string, vector and colour tables,
timelines) is shared by index and stays as it is. The output holds
Microsoft's art: it's built from the user's own files and never committed.
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import xur  # noqa: E402

# The current Guide's visual -> the 6717 visual it's made from. Picked by what
# each does (a list, a list row, a spinner...); the 17559 originals can't be
# read yet (XUIB version 8), so these are to be checked on the console.
STAND_INS = {
    "XuiButtonGuide": "XuiButton",
    "List_standard1": "List_standard",
    "List_2Line_ico_Right1String": "List_2Line_ico_Right2Strings",
    "btn_standard_list": "btn_standard",
    "tab_activeGS1": "tab_active_A",
    "tab_inactiveGS2": "tab_inactive_A",
    "btn_Count": "btn_Cost",                    # a row with a value on the right
    "btn_Count_achiev": "btn_Cost",
    "Loading_Small": "Loading_Inline",
    "HUD_Bladedark": "HUD_blade",
    "HUD_Bladegrey": "HUD_blade",
    "RightPanelShader": "back panel",
    "XuiLabelLeftJustify1": "XuiLabel",
}

# The Guide's own scene class (hud.xex), as its scenes store it: worked out
# from all 28 6717 hud scenes, which then read to the last byte. The names
# are ours.
HUD_SCENE = ("XuiScene", [
    xur.Prop(1000, "HudStyle", xur.INT, 0),     # 1-3
    xur.Prop(1001, "LegendA", xur.STRING, 0),   # "Select", "Apply"
    xur.Prop(1002, "LegendB", xur.STRING, 0),   # "Back"
    xur.Prop(1003, "LegendX", xur.STRING, 0),   # "Sign In", "Switch Profile"...
    xur.Prop(1004, "LegendY", xur.STRING, 0),   # "Xbox Dashboard"
])
SCHEMA = os.path.join(os.path.dirname(os.path.abspath(__file__)), "xui_schema.txt")


class Skin:
    def __init__(self, data):
        self.d = bytearray(data)
        magic, ver, flags, tool, size, nsec = struct.unpack(">4sIIHIH", self.d[:20])
        assert magic == b"XUIB" and ver == 5, (magic, ver)
        self.table_at = 20 + (40 if flags & 1 else 0)
        self.sections = []                  # (name, offset, size), in table order
        for i in range(nsec):
            n, o, s = struct.unpack(">4sII", self.d[self.table_at + 12 * i:self.table_at + 12 * i + 12])
            self.sections.append([n, o, s])
        self.strings = []
        o, s = self.section(b"STRN")
        q = o
        while q < o + s:
            n = struct.unpack(">H", self.d[q:q + 2])[0]
            self.strings.append(self.d[q + 2:q + 2 + 2 * n].decode("utf-16-be"))
            q += 2 + 2 * n

    def section(self, name):
        for n, o, s in self.sections:
            if n == name:
                return o, s
        raise KeyError(name)

    def visuals(self):
        """[(name, start, end, where its Id is)] of the canvas's visuals, and
        where the canvas's child count is."""
        r = xur.Reader(bytes(self.d), xur.Schema(SCHEMA), {"HUDScene": HUD_SCENE})
        root = r.obj()
        o, s = self.section(b"DATA")
        assert r.pos == o + s, "DATA read to %#x of %#x" % (r.pos, o + s)
        assert not root.get("frames"), "the canvas has timelines after its children"
        kids = root["children"]
        out = []
        for i, k in enumerate(kids):
            start = k["at"]
            end = kids[i + 1]["at"] if i + 1 < len(kids) else o + s
            # class u16, flags u8, property count u16, then XuiElement's block:
            # a length/count byte, the mask, and the Id (bit 0) first.
            mlen = self.d[start + 5] & 7
            mask = int.from_bytes(self.d[start + 6:start + 6 + mlen], "big")
            assert self.d[start + 2] & 1 and mask & 1, "visual at %#x has no Id" % start
            out.append((k["props"]["Id"], start, end, start + 6 + mlen))
        count_at = kids[0]["at"] - 4
        assert struct.unpack(">I", self.d[count_at:count_at + 4])[0] == len(kids)
        return out, count_at

    def add_stand_ins(self, mapping):
        visuals, count_at = self.visuals()
        by_name = {name: (start, end, idpos) for name, start, end, idpos in visuals}
        new_records, new_strings = bytearray(), []
        for new, old in mapping.items():
            if new in by_name:
                continue                        # the skin has it already
            if old not in by_name:
                raise KeyError("no visual %s for %s" % (old, new))
            start, end, idpos = by_name[old]
            record = bytearray(self.d[start:end])
            if new in self.strings:
                sid = self.strings.index(new) + 1   # strings are numbered from 1
            else:
                new_strings.append(new)
                sid = len(self.strings) + len(new_strings)
            struct.pack_into(">H", record, idpos - start, sid)
            new_records += record
            print("  %-28s <- %s (%d bytes)" % (new, old, end - start))
        struct.pack_into(">I", self.d, count_at, len(visuals) + len(mapping) - sum(n in by_name for n in mapping))
        strn = bytearray()
        for t in new_strings:
            strn += struct.pack(">H", len(t)) + t.encode("utf-16-be")
        self.rebuild({b"STRN": strn, b"DATA": new_records})
        self.strings += new_strings

    def rebuild(self, additions):
        """Appends bytes to the end of sections, moving the ones after."""
        order = sorted(self.sections, key=lambda e: e[1])
        body = bytearray()
        first = order[0][1]
        new_offsets = {}
        pos = first
        for n, o, s in order:
            chunk = bytes(self.d[o:o + s]) + bytes(additions.get(n, b""))
            new_offsets[n] = (pos, len(chunk))
            body += chunk
            pos += len(chunk)
        out = bytearray(self.d[:first]) + body
        for i, (n, o, s) in enumerate(self.sections):
            no, ns = new_offsets[n]
            struct.pack_into(">4sII", out, self.table_at + 12 * i, n, no, ns)
            self.sections[i] = [n, no, ns]
        struct.pack_into(">I", out, 14, len(out))   # the header's total size
        self.d = out


if __name__ == "__main__":
    skin = Skin(open(sys.argv[1], "rb").read())
    visuals, _ = skin.visuals()
    print("6717 skin: %d visuals" % len(visuals))
    skin.add_stand_ins(STAND_INS)
    after, _ = skin.visuals()               # reads the result back, every byte
    print("Blades 6717 skin: %d visuals, %d bytes" % (len(after), len(skin.d)))
    open(sys.argv[2], "wb").write(skin.d)
