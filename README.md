# XJ380 SDK
本仓库用于存储 XJ380 SDK 的源代码，与 XXCC/XACT 最新版同步，编译好的二进制文件请前往本仓库 Release 页面或 XXCC-suite 仓库或官网获取。

## 构建运行时 / CRT

```bash
python3 tools/gen_ninja.py
ninja
```

产物：

`out/xapi/`（与官方 XAPI 目标文件布局一致）：

- `arch/x86_64/crt0.S.o`：`_start -> xapi_start`
- `start.cpp.o`：通用入口（`environ -> main -> exit`）
- `constart.cpp.o`：终端入口（`fork + shell.elf` terminal-app-mode）
- `*.cpp.o`：libc / POSIX / XAPI 运行时对象
- `libxact.a`：核心/TUI 运行时静态库
- `libxgui.a`：GUI 客户端 ABI（`xguiapi.cpp`），TUI 程序不会拉入
- `liballoc-x86_64.a`：官方预编译分配器

`out/crt/`（Windows CRT 风格，可直接打包/分发）：

```
include/
lib/x86_64/
    crt0.o
    start.o
    constart.o
    libxact.a
    libxgui.a
    liballoc.a
```

本仓库只包含核心/TUI 运行时源码；GUI/音频/网络对象依赖发行包中的
third_party 代码，不在本次源码构建范围内。`lib/liballoc-x86_64.a` 的
来源与校验值见 `lib/README.md`。
