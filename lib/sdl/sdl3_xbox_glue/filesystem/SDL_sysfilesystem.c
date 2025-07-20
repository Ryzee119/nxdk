#include "SDL_internal.h"

#if defined(SDL_FILESYSTEM_NXDK) && !defined(SDL_FILESYSTEM_DISABLED)

#include <../src/filesystem/SDL_sysfilesystem.h>
#include <assert.h>
#include <nxdk/mount.h>
#include <nxdk/path.h>
#include <windows.h>

static bool base_mounted = false;

char *SDL_SYS_GetBasePath(void)
{
    if (base_mounted == false) {
        char targetPath[MAX_PATH];
        nxGetCurrentXbeNtPath(targetPath);
        *(strrchr(targetPath, '\\') + 1) = '\0';
        nxMountDrive('S', targetPath);
        base_mounted = true;
    }

    return SDL_strdup("S:\\");
}

char *SDL_SYS_GetPrefPath(const char *org, const char *app)
{
    if (nxIsDriveMounted('E') == false) {
        nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    }
    return SDL_strdup("E:\\UDATA");
}

char *SDL_SYS_GetUserFolder(SDL_Folder folder)
{
    if (nxIsDriveMounted('E') == false) {
        nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    }
    return SDL_strdup("E:\\UDATA");
}

char *SDL_SYS_GetCurrentDirectory(void)
{
    return SDL_SYS_GetBasePath();
}

#endif // SDL_FILESYSTEM_NXDK
