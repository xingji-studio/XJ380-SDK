#!/usr/bin/env python3
"""Build the XAPI runtime and the complete XACT CRT from this SDK tree.

Outputs:

    out/xapi/        runtime objects + libxact.a + libxgui.a + liballoc.a
    out/runtime/     include/ + startup objects + libxact.a + libxgui.a
    out/crt/         complete CRT: include/ + startup objects + runtime + liballoc

liballoc comes from the pinned third_party/liballoc submodule when Rust
nightly/rustup is available; otherwise the vendored prebuilt archive is used
after SHA-256 verification.  `ninja pack` builds the xact-crt xbps package.

Usage:
    git submodule update --init third_party/liballoc
    python3 tools/gen_ninja.py
    ninja            # runtime + CRT layouts
    ninja pack       # out/xact-crt-2026v5_1.x86_64.xbps
"""
import hashlib
import os
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "src"
INC = SRC / "include"
OUT = ROOT / "out" / "xapi"
LIBXACT = OUT / "libxact.a"
LIBXGUI = OUT / "libxgui.a"

RUNTIME_ROOT = ROOT / "out" / "runtime"
RUNTIME_INC = RUNTIME_ROOT / "include"
RUNTIME_LIB = RUNTIME_ROOT / "lib" / "x86_64"

CRT = ROOT / "out" / "crt"
CRT_INC = CRT / "include"
CRT_LIB = CRT / "lib" / "x86_64"

LIBALLOC_SUBMODULE = ROOT / "third_party" / "liballoc"
# Only upstream sources: the vendored fallback and the OpenXJ380 copy.
# Never read a downstream checkout or the XACT v5 release tree.
LIBALLOC_FALLBACKS = (
    ROOT / "vendor" / "liballoc-x86_64.a",
    ROOT / ".." / "OpenXJ380" / "liballoc-x86_64.a",
)
LIBALLOC_SHA256 = "2137fa65410bfecc22371769f3557e8f342216254afe67ad72e76117d1446d08"
LIBALLOC_OBJ = OUT / "liballoc.a"

PKGNAME = "xact-crt"
PKGVER = "2026v5_1"
ARCH = "x86_64"
DESC = "XACT CRT (XJ380 application compile suite runtime)"
PKG_PREFIX = "system/xact/crt"

RUNTIME = [
    "libsys.cpp", "xtuiapi.cpp", "krlibc.cpp", "string.cpp", "stdlib.cpp",
    "stdio.cpp", "unistd.cpp", "errno.cpp", "posix_ret.cpp", "ctype.cpp",
    "permissions.cpp", "posix_fs.cpp", "posix_time.cpp", "posix_ioctl.cpp",
    "xcrypto_random.cpp", "poll.cpp", "posix_mman.cpp", "posix_select.cpp",
    "posix_uio.cpp", "xattr.cpp", "xclipboard.cpp", "xanimation.cpp",
    "xapi_xml.cpp", "xime.cpp", "xtext.cpp", "xhighlight.cpp", "launch.cpp",
    "installer_native.cpp", "xaudio.cpp", "netdb.cpp", "socket.cpp", "xnet.cpp",
]
GUI = ["xguiapi.cpp"]
CRT0 = "arch/x86_64/crt0.S"
START = "start.cpp"
CONSTART = "constart.cpp"

SDK_FILES = ["crt0.o", "start.o", "constart.o", "libxact.a", "libxgui.a"]
CRT_FILES = SDK_FILES + ["liballoc.a"]


def find_liballoc():
    for candidate in LIBALLOC_FALLBACKS:
        if candidate.is_file():
            digest = hashlib.sha256(candidate.read_bytes()).hexdigest()
            if digest != LIBALLOC_SHA256:
                raise SystemExit(
                    f"liballoc checksum mismatch: {candidate}\n"
                    f"expected {LIBALLOC_SHA256}, got {digest}"
                )
            return candidate
    raise SystemExit(
        "liballoc fallback not found; provide vendor/liballoc-x86_64.a or "
        "install rustup + Rust nightly and initialize third_party/liballoc"
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

    cargo = shutil.which(os.environ.get("CARGO", "cargo"))
    rustup = shutil.which("rustup")
    source_liballoc = ((LIBALLOC_SUBMODULE / "Cargo.toml").is_file() and cargo is not None
                       and rustup is not None and os.environ.get("LIBALLOC_PREBUILT") != "1")
    liballoc = None if source_liballoc else find_liballoc()

    L = ["# XJ380 SDK runtime + XACT CRT", "ninja_required_version = 1.10",
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
        "rule cargo_liballoc",
        "  command = mkdir -p $$(dirname $out) && cd third_party/liballoc && "
        "cargo build --release --target x86_64-unknown-none && "
        "cp target/x86_64-unknown-none/release/liballoc.a \"../../$out\"",
        "  restat = 1", "",
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
    if source_liballoc:
        L += [f"build {esc(LIBALLOC_OBJ)}: cargo_liballoc | "
              f"{esc(LIBALLOC_SUBMODULE / 'Cargo.toml')} "
              f"{esc(LIBALLOC_SUBMODULE / 'Cargo.lock')} "
              f"{esc(LIBALLOC_SUBMODULE / 'src' / 'lib.rs')}", ""]
    else:
        L += [f"build {esc(LIBALLOC_OBJ)}: copy {esc(liballoc)}", ""]
    objs += [LIBXACT, LIBXGUI, LIBALLOC_OBJ]

    # Runtime layout (no allocator).
    L += [f"build {esc(RUNTIME_INC)}: copy_dir {esc(INC)}", ""]
    runtime_files = {
        "crt0.o": crt0,
        "start.o": OUT / (START + ".o"),
        "constart.o": OUT / (CONSTART + ".o"),
        "libxact.a": LIBXACT,
        "libxgui.a": LIBXGUI,
    }
    for name, source in runtime_files.items():
        L += [f"build {esc(RUNTIME_LIB / name)}: copy {esc(source)}", ""]
        objs.append(RUNTIME_LIB / name)
    objs.append(RUNTIME_INC)

    # Complete CRT layout used by downstream components.
    L += [f"build {esc(CRT_INC)}: copy_dir {esc(INC)}", ""]
    crt_files = dict(runtime_files)
    crt_files["liballoc.a"] = LIBALLOC_OBJ
    for name, source in crt_files.items():
        L += [f"build {esc(CRT_LIB / name)}: copy {esc(source)}", ""]
        objs.append(CRT_LIB / name)
    objs.append(CRT_INC)

    L += ["build all: phony " + " ".join(esc(o) for o in objs), "default all", ""]

    # ---- xbps: xact-crt ----
    pkg_out = OUT.parent / f"{PKGNAME}-{PKGVER}.{ARCH}.xbps"
    args = f"-A {ARCH} -n {PKGNAME}-{PKGVER} -s \"{DESC}\""
    vendor = ROOT / ".pkg" / PKG_PREFIX
    cmd = f"rm -rf {esc(ROOT / '.pkg')}"
    cmd += f" && mkdir -p {esc(vendor / 'lib' / 'x86_64')}"
    cmd += f" && cp -a {esc(CRT_INC)} {esc(vendor / 'include')}"
    for name in CRT_FILES:
        cmd += f" && cp {esc(CRT_LIB / name)} {esc(vendor / 'lib' / 'x86_64' / name)}"
    cmd += f" && cp {esc(ROOT / 'LICENSE')} {esc(vendor / 'LICENSE')}"
    cmd += f" && cd {esc(pkg_out.parent)} && xbps-create --compression gzip {args} ../.pkg"
    L += ["rule package", f"  command = {cmd}", ""]
    pkg_inputs = [CRT_INC] + [CRT_LIB / n for n in CRT_FILES] + [ROOT / "LICENSE"]
    L += [f"build {esc(pkg_out)}: package " + " ".join(esc(x) for x in pkg_inputs), ""]
    L += [f"build pack: phony {esc(pkg_out)}", ""]
    L += ["rule clean", "  command = rm -rf out .pkg", "build clean: clean"]

    (ROOT / "build.ninja").write_text("\n".join(L) + "\n", encoding="utf-8")
    where = "submodule source" if source_liballoc else f"prebuilt fallback {liballoc}"
    print(f"已生成 build.ninja（XJ380 SDK runtime + CRT，liballoc: {where}）")
    print("目标：ninja（runtime/CRT 布局）/ ninja pack（xact-crt xbps）")


if __name__ == "__main__":
    main()
