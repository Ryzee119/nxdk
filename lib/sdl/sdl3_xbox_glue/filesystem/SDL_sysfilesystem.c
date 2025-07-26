#include "SDL_internal.h"

#if defined(SDL_FILESYSTEM_NXDK) && !defined(SDL_FILESYSTEM_DISABLED)

// The letter is arbritrary. Try keep away from stock letters, default to S for SDL
#ifndef SDL_BASE_PATH_LETTER_NXDK
#define SDL_BASE_PATH_LETTER_NXDK 'S'
#endif

#include <../src/filesystem/SDL_sysfilesystem.h>
#include <assert.h>
#include <nxdk/mount.h>
#include <nxdk/path.h>
#include <windows.h>

static bool base_mounted = false;

// As per SDL docs, the returned path is guaranteed to end with a path separator ('\' on Windows, '/' on most other platforms).

char *SDL_SYS_GetBasePath(void)
{
    if (base_mounted == false) {
        char targetPath[MAX_PATH];
        nxGetCurrentXbeNtPath(targetPath);
        *(strrchr(targetPath, '\\') + 1) = '\0';
        nxMountDrive(SDL_BASE_PATH_LETTER_NXDK, targetPath);
        base_mounted = true;
    }

    char base_path[] = { SDL_BASE_PATH_LETTER_NXDK, ':', '\\', '\0' };
    return SDL_strdup(base_path);
}

char *SDL_SYS_GetPrefPath(const char *org, const char *app)
{
    if (nxIsDriveMounted('E') == false) {
        nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    }

    // Create UDATA directory if it doesn't exist
    CreateDirectoryA("E:\\UDATA", NULL);

    return SDL_strdup("E:\\UDATA\\");
}

char *SDL_SYS_GetUserFolder(SDL_Folder folder)
{
    if (nxIsDriveMounted('E') == false) {
        nxMountDrive('E', "\\Device\\Harddisk0\\Partition1\\");
    }

    // Create UDATA directory if it doesn't exist
    CreateDirectoryA("E:\\UDATA", NULL);

    return SDL_strdup("E:\\UDATA\\");
}

char *SDL_SYS_GetCurrentDirectory(void)
{
    return SDL_SYS_GetBasePath();
}

#endif // SDL_FILESYSTEM_NXDK
