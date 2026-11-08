"""The importer without its window, for testing:

    python -m slopdash_importer.cli <your files>... --out <folder> [--work <folder>]

Scans the files and folders given, builds, and copies the result into --out
(a USB stick's root, or a test folder: ImporterAssets/output)."""
import argparse
import os
import sys
import time

from . import VERSION, pipeline, scan, usb


def main():
    p = argparse.ArgumentParser(prog="SlopDash Importer")
    p.add_argument("sources", nargs="+")
    p.add_argument("--out", required=True)
    p.add_argument("--work", default=pipeline.DEFAULT_WORK)
    a = p.parse_args()
    print("SlopDash Importer %s" % VERSION)
    found = scan.scan(a.sources)
    for s, rows in scan.checklist(found).items():
        print("\n" + scan.MANIFEST["sets"][s])
        for e, f in rows:
            mark = "ok " if f else ("-- " if e["need"] != "required" else "!! ")
            print("  %s%-22s %-12s %s" % (mark, e["name"], e["need"], f.where() if f else ""))
    t = scan.themes(found)
    if not t["blades"][0]:
        print("\nBlades can't be built: missing " + ", ".join(t["blades"][1]))
        return 1
    t0 = time.time()
    try:
        stage = pipeline.Build(found, a.work).run()
    except pipeline.BuildError as e:
        print("\nnot built: %s" % e)
        return 1
    print("\nbuilt in %.0f s" % (time.time() - t0))
    n = usb.copy_tree(stage, a.out, log=print)
    print("%d files copied to %s and checked" % (n, a.out))
    print("\n" + usb.launch_ini_advice(a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
