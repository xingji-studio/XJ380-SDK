/*
 *
 *
 *      XJ380 终端应用程序启动框架
 *      2025/11/5 - GuoqiFish
 *      Copyright(C) XINGJI Interactive Software 2017 - 2026 All rights reserved.
 * 
 * 
 */

#include "./include/x3api.h"
#include "./include/libsys.h"

extern "C" int main(int argc, char *argv[], char *envp[]) __attribute__((weak));
extern "C" int xapi_legacy_cpp_main(int argc, char *argv[], char *envp[]) asm("_Z4mainiPPcS0_") __attribute__((weak));
// extern void _init();
// extern void __preinit_array_start();
// extern void __preinit_array_end();
// extern void __init_array_start();
// extern void __init_array_end();
// extern void __fini_array_start();
// extern void __fini_array_end();

#define SXAH_CHECK_TERMINAL_INIT_STATUS  3801    // 查询终端初始化状态

extern "C" __attribute__((force_align_arg_pointer)) void xapi_start(int argc, char *argv[], char *envp[])
{
    // 终端启动器同样先挂接 environ，子进程进入 main 前即可读取环境变量。
    environ = envp;

    //{
    //    void (*fn)() = __preinit_array_start;
    //    while (fn != __preinit_array_end)
    //    {
    //        (*fn)();
    //        fn++;
    //    }
    //}

    //_init();

    //{
    //    void (*fn)() = __init_array_start;
    //    while (fn != __init_array_end)
    //    {
    //        (*fn)();
    //        fn++;
    //    }
    //}

    uint64_t pid  = xapi_Fork();

    if (pid == 0)
    {
        // 是子进程！
        // 等待命令行初始化
        while (true)
        {
            // 这时命令行肯定是他的父进程，循环检查是否标记为命令行，检测到标记即代表可以吃了
            if (enter_syscall(SXAH_CHECK_TERMINAL_INIT_STATUS, 0, 0, 0, 0, 0, 0)) break;
        }
        if (main != NULL)
        {
            exit(main(argc, argv, envp));
        }

        if (xapi_legacy_cpp_main != NULL)
        {
            exit(xapi_legacy_cpp_main(argc, argv, envp));
        }

        exit(0);
    }

    char *execve_argv[2] = {"-xj380sys-terminal-app-mode"};
    xapi_Execve("/apps/system/shell/shell.elf", execve_argv, NULL);

    if (main != NULL)
    {
        exit(main(argc, argv, envp));
    }

    if (xapi_legacy_cpp_main != NULL)
    {
        exit(xapi_legacy_cpp_main(argc, argv, envp));
    }

    exit(0);
}
