"""SlopDash Importer's window: five steps, one after another.

  1. Welcome     what it does; files from hardware you own
  2. Your files  folders and zips to look in; the checklist of what was found
  3. USB stick   which removable drive (FAT32, enough room), or a folder
  4. Build       the build and the copy, with progress
  5. Done        where the files go, and what to change in launch.ini (never edited)
"""
import os
import queue
import shutil
import threading
import tkinter as tk
from tkinter import filedialog, messagebox, ttk

from . import VERSION, pipeline, scan, usb

PAD = 12


def dpi_aware():
    """Sharp text at Windows' display scaling (else it's drawn small and blurred up)."""
    try:
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        pass


class App(tk.Tk):
    def __init__(self):
        dpi_aware()
        super().__init__()
        self.title("SlopDash Importer %s" % VERSION)
        self.geometry("%dx%d" % (self.winfo_fpixels("9.5i"), self.winfo_fpixels("7i")))
        self.minsize(int(self.winfo_fpixels("8i")), int(self.winfo_fpixels("6i")))
        self.sources = []
        self.found = {}
        self.target = None
        self.work = pipeline.DEFAULT_WORK
        self.events = queue.Queue()
        style = ttk.Style(self)
        if "vista" in style.theme_names():
            style.theme_use("vista")
        style.configure("Title.TLabel", font=("Segoe UI", 16, "bold"))
        style.configure("Step.TLabel", font=("Segoe UI", 9), foreground="#666")
        self.header = ttk.Frame(self, padding=(PAD, PAD, PAD, 0))
        self.header.pack(fill="x")
        self.step_label = ttk.Label(self.header, style="Step.TLabel")
        self.step_label.pack(anchor="w")
        self.title_label = ttk.Label(self.header, style="Title.TLabel")
        self.title_label.pack(anchor="w")
        self.body = ttk.Frame(self, padding=PAD)
        self.body.pack(fill="both", expand=True)
        nav = ttk.Frame(self, padding=PAD)
        nav.pack(fill="x", side="bottom")
        self.back_btn = ttk.Button(nav, text="< Back", command=self.back)
        self.back_btn.pack(side="left")
        self.next_btn = ttk.Button(nav, text="Next >", command=self.next)
        self.next_btn.pack(side="right")
        self.pages = [self.page_welcome, self.page_files, self.page_usb, self.page_build, self.page_launch]
        self.titles = ["Welcome", "Your dashboard files", "Your USB stick", "Building SlopDash",
                       "You've made your SlopDash files"]
        self.to_stick = True                # the target is a USB stick (else a folder: copy it over)
        self.at = 0
        self.show(0)
        self.after(100, self.pump)

    # --- page flow ----------------------------------------------------------------
    def show(self, i):
        self.at = i
        for w in self.body.winfo_children():
            w.destroy()
        self.step_label.config(text="Step %d of %d" % (i + 1, len(self.pages)))
        self.title_label.config(text=self.titles[i])
        self.back_btn.config(state="normal" if 0 < i < 3 else "disabled")
        self.next_btn.config(text="Next >", state="normal", command=self.next)
        self.pages[i]()

    def next(self):
        self.show(self.at + 1)

    def back(self):
        self.show(self.at - 1)

    def pump(self):
        """Work from the build thread, onto the window."""
        try:
            while True:
                kind, *args = self.events.get_nowait()
                getattr(self, "on_" + kind)(*args)
        except queue.Empty:
            pass
        self.after(100, self.pump)

    def text(self, parent, words, **kw):
        lab = ttk.Label(parent, text=words, wraplength=int(self.winfo_fpixels("8.8i")), justify="left", **kw)
        lab.pack(anchor="w", pady=(0, 8))
        return lab

    # --- 1. Welcome -----------------------------------------------------------------
    def page_welcome(self):
        self.text(self.body, "SlopDash brings back the Xbox 360's 2005 Blades dashboard. Its look comes from "
                             "Microsoft's own dashboard files, which SlopDash can't include: you bring your own, "
                             "and this puts them together with SlopDash on a USB stick for your console.")
        self.text(self.body, "You'll need:")
        for line in ("The Xbox 360 2.0.1888 dashboard files (dash.xex, xam.xex, hud.xex and the others)",
                     "The 2.0.6717 dashboard (its dash.xex)",
                     "A FAT32 USB stick (the one your console starts from)"):
            self.text(self.body, "    •  " + line)
        self.text(self.body, "Use files from hardware you own. Nothing is downloaded or sent anywhere: "
                             "everything happens on this PC.", foreground="#555")

    # --- 2. Your files ----------------------------------------------------------------
    def page_files(self):
        top = ttk.Frame(self.body)
        top.pack(fill="x")
        ttk.Button(top, text="Add a folder...", command=self.add_folder).pack(side="left")
        ttk.Button(top, text="Add a zip...", command=self.add_zip).pack(side="left", padx=6)
        ttk.Button(top, text="Clear", command=self.clear_sources).pack(side="left")
        self.sources_label = ttk.Label(self.body, foreground="#555", wraplength=int(self.winfo_fpixels("8.8i")), justify="left")
        self.sources_label.pack(anchor="w", pady=(8, 6))
        cols = ("mark", "file", "need", "where")
        tree = ttk.Treeview(self.body, columns=cols, show="tree headings", height=14)
        tree.heading("#0", text="")
        tree.column("#0", width=int(self.winfo_fpixels("0.3i")), stretch=False)
        for c, w, t in (("mark", 0.3, ""), ("file", 3.3, "File"), ("need", 1.1, "Needed"), ("where", 3.6, "Where")):
            tree.heading(c, text=t, anchor="w")
            tree.column(c, width=int(self.winfo_fpixels("%gi" % w)), anchor="w", stretch=c == "where")
        tree.tag_configure("set", font=("Segoe UI", 9, "bold"))
        tree.tag_configure("have", foreground="#060")
        tree.tag_configure("missing", foreground="#a00")
        tree.tag_configure("optional", foreground="#777")
        tree.pack(fill="both", expand=True)
        self.tree = tree
        self.summary = ttk.Label(self.body, wraplength=int(self.winfo_fpixels("8.8i")), justify="left")
        self.summary.pack(anchor="w", pady=(8, 0))
        self.refresh_checklist()

    def add_folder(self):
        d = filedialog.askdirectory(title="A folder with your dashboard files")
        if d:
            self.sources.append(d)
            self.rescan()

    def add_zip(self):
        f = filedialog.askopenfilename(title="A zip with your dashboard files", filetypes=[("Zip", "*.zip")])
        if f:
            self.sources.append(f)
            self.rescan()

    def clear_sources(self):
        self.sources = []
        self.found = {}
        self.refresh_checklist()

    def rescan(self):
        self.config(cursor="watch")
        self.update()
        try:
            self.found = scan.scan(self.sources)
        finally:
            self.config(cursor="")
        self.refresh_checklist()

    def refresh_checklist(self):
        self.sources_label.config(text="Looking in: " + ("; ".join(self.sources) if self.sources
                                                          else "nothing yet: add a folder or a zip."))
        self.tree.delete(*self.tree.get_children())
        for s, rows in scan.checklist(self.found).items():
            got = sum(1 for _, f in rows if f)
            parent = self.tree.insert("", "end", open=True, tags=("set",),
                                      values=("", "%s   %d of %d" % (scan.MANIFEST["sets"][s], got, len(rows)), "", ""))
            for e, f in rows:
                mark = "✔" if f else ("✘" if e["need"] == "required" else "○")
                tag = "have" if f else ("missing" if e["need"] == "required" else "optional")
                where = os.path.basename(os.path.dirname(f.path)) + "\\" + os.path.basename(f.path) if f else e["for"]
                if f and f.member:
                    where = os.path.basename(f.path) + " > " + f.member
                if f and f.inner:                   # (in a content package: a system update)
                    where += " > " + f.inner
                self.tree.insert(parent, "end", tags=(tag,), values=(mark, e["name"], e["need"], where))
        blades = scan.themes(self.found)["blades"]
        if blades[0]:
            words = "Blades: ready."
            if blades[2]:
                words += " Without %s, some screens won't open." % ", ".join(blades[2])
        else:
            words = "Blades needs: " + ", ".join(blades[1]) + "."
        self.summary.config(text=words)
        self.next_btn.config(state="normal" if blades[0] else "disabled")

    # --- 3. USB stick -----------------------------------------------------------------
    def page_usb(self):
        self.text(self.body, "Choose the USB stick your console starts from. SlopDash goes in its "
                             "\\Apps\\SlopDash folder; nothing else on it is changed.")
        row = ttk.Frame(self.body)
        row.pack(fill="x", pady=4)
        self.drive_var = tk.StringVar()
        self.drive_box = ttk.Combobox(row, textvariable=self.drive_var, state="readonly", width=70)
        self.drive_box.pack(side="left")
        self.drive_box.bind("<<ComboboxSelected>>", lambda e: self.check_drive())
        ttk.Button(row, text="Refresh", command=self.list_drives).pack(side="left", padx=6)
        ttk.Button(self.body, text="Use a folder instead (for testing)...", command=self.pick_folder).pack(
            anchor="w", pady=(6, 0))
        self.drive_note = ttk.Label(self.body, wraplength=int(self.winfo_fpixels("8.8i")), justify="left")
        self.drive_note.pack(anchor="w", pady=10)
        self.next_btn.config(text="Build and copy", state="disabled")
        self.list_drives()

    def list_drives(self):
        self.drives = usb.removable_drives()
        self.drive_box["values"] = [str(d) for d in self.drives]
        if self.drives:
            self.drive_box.current(0)
            self.check_drive()
        else:
            self.drive_var.set("")
            self.drive_note.config(text="No USB stick found. Plug one in and press Refresh.", foreground="#a00")

    def check_drive(self):
        d = self.drives[self.drive_box.current()]
        why = usb.problems(d, 200 * 1000 * 1000)
        if why:
            self.drive_note.config(text=" ".join(why), foreground="#a00")
            self.next_btn.config(state="disabled")
        else:
            self.target = d.root
            self.to_stick = True
            self.say_target("SlopDash will be copied to %sApps\\SlopDash." % d.root)

    def pick_folder(self):
        d = filedialog.askdirectory(title="A folder to put SlopDash in (as if it were the stick)")
        if d:
            self.target = d
            self.to_stick = False
            self.drive_var.set(d)
            self.say_target("SlopDash will be copied to %s\\Apps\\SlopDash." % d)

    def say_target(self, words):
        """Where it goes, and what kind of stick it is (usb.stick_note)."""
        note = usb.stick_note(self.target)
        self.drive_note.config(text=words + ("\n\n" + note if note else ""),
                               foreground="#8a5a00" if note else "#060")
        self.next_btn.config(state="normal")

    # --- 4. Build -----------------------------------------------------------------------
    def page_build(self):
        self.next_btn.config(state="disabled")
        self.status = ttk.Label(self.body, text="Starting...")
        self.status.pack(anchor="w")
        self.bar = ttk.Progressbar(self.body, maximum=100)
        self.bar.pack(fill="x", pady=8)
        frame = ttk.Frame(self.body)
        frame.pack(fill="both", expand=True)
        self.log_box = tk.Text(frame, height=16, wrap="none", font=("Consolas", 9))
        sb = ttk.Scrollbar(frame, command=self.log_box.yview)
        self.log_box.config(yscrollcommand=sb.set)
        sb.pack(side="right", fill="y")
        self.log_box.pack(fill="both", expand=True)
        threading.Thread(target=self.build_thread, daemon=True).start()

    def build_thread(self):
        put = self.events.put
        try:
            b = pipeline.Build(self.found, self.work, log=lambda t: put(("log", t)),
                               step=lambda i, n, t: put(("step", i, n, t)))
            stage = b.run()
            put(("step", 8, 10, "Copying to the stick and checking each file"))
            n = usb.copy_tree(stage, self.target, log=lambda t: put(("log", t)),
                              progress=lambda i, n, rel: put(("copied", i, n)))
            put(("done", n))
        except Exception as e:              # (shown, not lost: the build's messages say what)
            put(("failed", str(e)))

    def on_log(self, t):
        self.log_box.insert("end", t + "\n")
        self.log_box.see("end")

    def on_step(self, i, n, t):
        self.status.config(text=t)
        self.bar["value"] = 80.0 * i / n

    def on_copied(self, i, n):
        self.bar["value"] = 80 + 20.0 * i / n

    def on_done(self, n):
        self.bar["value"] = 100
        self.status.config(text="Done: %d files copied and checked." % n)
        self.next_btn.config(state="normal")

    def on_failed(self, why):
        self.status.config(text="Stopped: " + why, foreground="#a00")
        messagebox.showerror("SlopDash Importer", "The build stopped:\n\n" + why)
        self.back_btn.config(state="normal")

    # --- 5. Done: where the files go, and launch.ini ------------------------------------------
    def page_launch(self):
        here = os.path.normpath(os.path.join(self.target, "Apps", "SlopDash"))
        peer = usb.stick_kind(self.target) == usb.PEER_PRESSURE

        def heading(words):
            ttk.Label(self.body, text=words, font=("Segoe UI", 11, "bold")).pack(anchor="w", pady=(10, 2))

        def para(words):
            ttk.Label(self.body, text=words, wraplength=int(self.winfo_fpixels("8.8i")),
                      justify="left").pack(anchor="w", pady=(0, 4))

        def code(words):
            tk.Label(self.body, text=words, font=("Consolas", 10), bg="white", fg="#222", justify="left",
                     anchor="w", relief="solid", borderwidth=1, padx=10, pady=6,
                     wraplength=int(self.winfo_fpixels("8.6i"))).pack(anchor="w", fill="x", pady=(2, 4))

        ttk.Label(self.body, text="Well done! SlopDash is built from your dashboard files, and every file "
                                  "was checked after copying. Three steps and it's on your console.",
                  font=("Segoe UI", 10), wraplength=int(self.winfo_fpixels("8.8i")),
                  justify="left").pack(anchor="w")

        heading("1.  Your files on the USB stick")
        if self.to_stick:
            para("They're already in place, in %s:" % here)
        else:
            para("They're in %s. Copy the whole Apps folder to the root of the USB stick your console "
                 "starts from, so they end up in Usb:\\Apps\\SlopDash:" % here)
        para("      •  SlopDash.xex: the dashboard\n"
             "      •  SlopGuide.xex: the Guide\n"
             "      •  Guide and Themes: the 2005 look, built from your files\n"
             "Keep them together in that folder.")

        if peer:
            heading("2.  Set SlopDash as the dashboard")
        else:
            heading("2.  Edit launch.ini")
            para("launch.ini is the text file in the stick's root that your softmod reads as the console "
                 "starts. Open it in Notepad and set these lines:")
        # The advice's words as text, its indented lines (what goes in the file) as code.
        blocks = []
        for chunk in usb.launch_ini_advice(self.target).strip().split("\n\n"):
            lines = chunk.split("\n")
            words = " ".join(l for l in lines if not l.startswith("  "))
            ini = "\n".join(l.strip() for l in lines if l.startswith("  "))
            if words:
                blocks.append(["p", words])
            if ini:
                if blocks and blocks[-1][0] == "c":
                    blocks[-1][1] += "\n\n" + ini
                else:
                    blocks.append(["c", ini])
        for kind, words in blocks:
            (code if kind == "c" else para)(words)

        heading("3.  Start your console")
        para("Eject the stick safely, plug it into your console and start it with your softmod as usual. "
             "SlopDash starts instead of the Xbox dashboard, and the Xbox button opens the Guide.")

        self.keep_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(self.body, text="Keep the build folder (it holds your dashboard files, decrypted)",
                        variable=self.keep_var).pack(anchor="w", pady=(12, 0))
        self.next_btn.config(text="Finish", command=self.finish)

    def finish(self):
        if not self.keep_var.get():
            pipeline.remove_workspace(self.work)     # (only the folder the importer made)
        self.destroy()


def main():
    App().mainloop()
