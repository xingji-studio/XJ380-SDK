#include "./include/xlaunch.h"
#include "./include/xtuiapi.h"
#include "./include/string.h"
#include "./include/xposix/errno.h"

extern "C" int64_t xj380_launch_desktop_entry(char *path, char *argv[])
{
    if (!path) return -EINVAL;
    static const char *components[] = {
        "/apps/system/login/login.elf",
        "/apps/system/desktop/desktop.elf",
        "/apps/system/dock/dock.elf",
        "/apps/system/ctrlmenu/ctrlmenu.elf",
        "/apps/system/launcher/launcher.elf",
        "/apps/system/elfrun/elfrun.elf",
        "/apps/system/fmanager/fmanager.elf",
        "/apps/system/taskmgr/taskmgr.elf",
        "/apps/system/netcfg/netcfg.elf",
        "/apps/xingji-pinyin/settings.elf",
    };
    for (const char *component : components)
        if (!strcmp(path, component)) return (int64_t)xapi_SpawnInherited(path, argv);

    const char *extension = strrchr(path, '.');
    if (argv || (extension && (!strcmp(extension, ".elf") || !strcmp(extension, ".epf"))))
        return (int64_t)xapi_RunArgs(path, argv);
    xapi_Run(path); // Document association launches also always start a Visitor.
    return 0;
}
