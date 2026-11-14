#!/usr/bin/env python3
"""Build the XAPI runtime / CRT from this SDK source tree.

Outputs (out/xapi):

    arch/x86_64/crt0.S.o      _start -> xapi_start
    <runtime>.cpp.o           libc / POSIX / XAPI wrappers
    start.cpp.o               GUI/general entry: environ -> main -> exit
    constart.cpp.o            terminal entry: fork + shell.elf terminal mode
    libxact.a                 static core/TUI runtime archive
    liballoc-x86_64.a         official allocator (see lib/README.md)

The source tree does not include third_party media/network code, so this
build covers the core/TUI runtime.  GUI/audio/network objects from the
release package can be linked alongside these objects.

Usage: python3 tools/gen_ninja.py && ninja
"""
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INC = SRC / "include"
OUT = ROOT / "out" / "xapi"
LIBALLOC_DST = OUT / "liballoc-x86_64.a"
LIBXACT = OUT / "libxact.a"
LIBXGUI = OUT / "libxgui.a"
CRT = ROOT / "out" / "crt"
CRT_INC = CRT / "include"
CRT_LIB = CRT / "lib" / "x86_64"

RUNTIME = [
    "libsys.cpp", "xtuiapi.cpp", "krlibc.cpp", "string.cpp", "stdlib.cpp",
    "stdio.cpp", "unistd.cpp", "errno.cpp", "posix_ret.cpp", "ctype.cpp",
    "permissions.cpp", "posix_fs.cpp", "posix_time.cpp", "posix_ioctl.cpp",
    "xcrypto_random.cpp", "poll.cpp", "posix_mman.cpp", "posix_select.cpp",
    "posix_uio.cpp", "xattr.cpp", "xclipboard.cpp", "xanimation.cpp",
    "xapi_xml.cpp", "xime.cpp", "xtext.cpp", "xhighlight.cpp", "launch.cpp",
    "installer_native.cpp",
]
# GUI client ABI stays optional: TUI apps never pull this archive member.
GUI = ["xguiapi.cpp"]
CRT0 = "arch/x86_64/crt0.S"
START = "start.cpp"
CONSTART = "constart.cpp"


def find_liballoc():
    """Use the vendored archive, or the official XACT release if present."""
    xact = Path(os.environ.get("XACT_ROOT", ROOT / ".." / "XJ380_XACT_2026v5_linux"))
    for candidate in (
        ROOT / "lib" / "liballoc-x86_64.a",
        ROOT / "depend" / "obj-tui" / "liballoc-x86_64.a",
        xact / "depend" / "obj-tui" / "liballoc-x86_64.a",
        ROOT / ".." / "OpenXJ380" / "liballoc-x86_64.a",
    ):
        if candidate.is_file():
            return candidate
    raise SystemExit(
        "liballoc-x86_64.a not found; place it in lib/ or set XACT_ROOT to the "
        "extracted XACT distribution (depend/obj-tui/liballoc-x86_64.a)"
    )


def rel(p):
    return str(Path(p).resolve().relative_to(ROOT))


def esc(p):
    return rel(p).replace("$", "$$").replace(":", "$:").replace(" ", "$ ")


def main():
    cc = os.environ.get("CC", "clang")
    cxx = os.environ.get("CXX", "clang++")
    ar = os.environ.get("AR", "ar")
    opt = "-O" + os.environ.get("OPT_LEVEL", "2")
    col = os.environ.get("DIAGNOSTICS_COLOR", "-fdiagnostics-color=always")

    common = (f"{col} {opt} -Wall -g -ffreestanding -fno-builtin -m64 -mstackrealign "
              "-fno-stack-protector -fno-exceptions -fno-strict-aliasing -fshort-wchar -nostdinc")
    cxx_flags = f"{common} -std=c++11 -I {rel(INC)} -Wno-write-strings -MMD -MP"
    asm_flags = f"{col} -g -m64 -nostdinc"

    liballoc_src = find_liballoc()

    L = ["# XJ380 SDK runtime / CRT", "ninja_required_version = 1.10",
         f"cc = {cc}", f"cxx = {cxx}", f"ar = {ar}",
         f"cxx_flags = {cxx_flags}", f"asm_flags = {asm_flags}", ""]
    L += [
        "rule cxx",
        "  command = mkdir -p $$(dirname $out) && $cxx $cxx_flags -MF $out.d -c $in -o $out",
        "  depfile = $out.d", "  deps = gcc", "",
        "rule asm",
        "  command = mkdir -p $$(dirname $out) && $cc $asm_flags -c $in -o $out", "",
        "rule archive",
        "  command = rm -f $out && $ar rcs $out $in", "",
        "rule copy",
        "  command = mkdir -p $$(dirname $out) && cp $in $out", "",
        "rule copy_dir",
        "  command = rm -rf $out && mkdir -p $$(dirname $out) && cp -a $in $out", "",
    ]

    objs = []
    crt0 = OUT / "arch" / "x86_64" / "crt0.S.o"
    L += [f"build {esc(crt0)}: asm {esc(SRC / CRT0)}", ""]
    objs.append(crt0)

    runtime_objs = []
    for name in RUNTIME:
        obj = OUT / (name + ".o")
        L += [f"build {esc(obj)}: cxx {esc(SRC / name)}", ""]
        runtime_objs.append(obj)
        objs.append(obj)

    for name in (START, CONSTART):
        obj = OUT / (name + ".o")
        L += [f"build {esc(obj)}: cxx {esc(SRC / name)}", ""]
        objs.append(obj)

    gui_objs = []
    for name in GUI:
        obj = OUT / (name + ".o")
        L += [f"build {esc(obj)}: cxx {esc(SRC / name)}", ""]
        gui_objs.append(obj)
        objs.append(obj)

    L += [f"build {esc(LIBXACT)}: archive " + " ".join(esc(o) for o in runtime_objs), ""]
    L += [f"build {esc(LIBXGUI)}: archive " + " ".join(esc(o) for o in gui_objs), ""]
    L += [f"build {esc(LIBALLOC_DST)}: copy {esc(liballoc_src)}", ""]
    objs += [LIBXACT, LIBALLOC_DST]

    # Windows-style CRT layout: include/ + lib/x86_64/{startup,libs}
    L += [f"build {esc(CRT_INC)}: copy_dir {esc(INC)}", ""]
    crt_libs = {
        "crt0.o": crt0,
        "start.o": OUT / (START + ".o"),
        "constart.o": OUT / (CONSTART + ".o"),
        "libxact.a": LIBXACT,
        "libxgui.a": LIBXGUI,
        "liballoc.a": LIBALLOC_DST,
    }
    for name, source in crt_libs.items():
        L += [f"build {esc(CRT_LIB / name)}: copy {esc(source)}", ""]
        objs.append(CRT_LIB / name)
    objs.append(CRT_INC)

    L += ["build all: phony " + " ".join(esc(o) for o in objs), "default all", ""]
    L += ["rule clean", "  command = rm -rf out", "build clean: clean"]

    (ROOT / "build.ninja").write_text("\n".join(L) + "\n", encoding="utf-8")
    print(f"已生成 build.ninja（XJ380 SDK runtime / CRT，{len(objs)} 个产物）")


if __name__ == "__main__":
    main()
