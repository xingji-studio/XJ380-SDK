# XAPI KNOWLEDGE BASE

## OVERVIEW
User-space runtime/API layer. Produces startup objects and libc/POSIX-like wrappers consumed by almost every user app.

## STRUCTURE
```
src/
├── start.cpp           # 通用入口（GUI/普通程序）
├── constart.cpp        # 终端入口（fork + terminal-app-mode）
├── arch/x86_64/crt0.S  # `_start` -> `xapi_start`
├── include/            # 用户态 XAPI/libsys/libc 头
└── *.cpp               # 运行时 / syscall / libc / POSIX 封装
```

## WHERE TO LOOK
| Task | Location | Notes |
|------|----------|-------|
| Build runtime | `tools/gen_ninja.py` | 产出 `out/xapi/`、`out/runtime/`、`out/crt/` 与 `xact-crt` 包。 |
| ABI entry | `arch/x86_64/crt0.S` | Low-level `_start` trampoline. |
| GUI startup | `start.cpp` | Sets `environ`, calls weak `main`. |
| Console startup | `constart.cpp` | Shell/terminal synchronization. |
| Headers | `include/xapi.h`, `include/x3api.h`, `include/libsys.h` | App-facing API. |
| libc-ish code | `krlibc.cpp`, `stdlib.cpp`, `stdio.cpp` | Runtime support. |

## CONVENTIONS
- The generated Ninja graph builds runtime objects plus `start.cpp.o` and `constart.cpp.o`.
- It also publishes `out/runtime/` (runtime without allocator) and
  `out/crt/` (complete CRT with `liballoc.a`).
- `liballoc` comes from the pinned `third_party/liballoc` submodule
  (`plos-clan/liballoc`) when Rust is available, otherwise from the verified
  `vendor/liballoc-x86_64.a` fallback. `ninja pack` builds `xact-crt`.
- `libxact.a` is the core/TUI archive; `libxgui.a` holds the GUI client ABI
  (`xguiapi.cpp`) and is only linked by GUI programs.
- `start.cpp` and `constart.cpp` expose weak C `main` and legacy C++ mangled main fallback.
- Both startup paths set `environ = envp` before app code runs.
- `constart.cpp` forks and execs `/apps/system/shell.elf` to establish terminal mode, then waits for `SXAH_CHECK_TERMINAL_INIT_STATUS`.
- Headers here are part of the user/kernel contract; coordinate changes with `kernel/syscall/` and `include/syscall/`.
- Assembly startup marks `.note.GNU-stack`; keep ABI minimal and architecture-specific.

## ANTI-PATTERNS
- Do not delete or rename startup objects without updating every app link rule in `tools/gen_ninja.py`.
- Do not change syscall wrapper signatures in xapi only; update kernel handlers and user headers together.
- Do not assume hosted libc behavior; this runtime is freestanding and partial.
- Do not ignore existing TODOs in `krlibc.cpp` formatting/error paths when changing printf-like code.

## VERIFY
Build with `python3 tools/gen_ninja.py && ninja`. Runtime ABI changes require running representative apps: shell, GUI app, signal/libc/POSIX demos.
