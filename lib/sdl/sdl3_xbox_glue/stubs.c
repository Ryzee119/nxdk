#include <SDL3/SDL.h>
#include <windows.h>

int WIN_SetError(const char *prefix)
{
    DWORD error = GetLastError();
    char buffer[256];
    SDL_snprintf(buffer, sizeof(buffer), "Error: %s: Code: %lu", (prefix) ? prefix : "", error);
    SDL_SetError("%s", buffer);
    return -1;
}

HMODULE GetModuleHandle(LPCSTR lpModuleName) {
    return NULL;
}

void SDL_InitSteamVirtualGamepadInfo(void)
{

}

bool SDL_SteamVirtualGamepadEnabled(void)
{
    return false;
}

void SDL_QuitSteamVirtualGamepadInfo(void)
{

}

typedef void *SDL_SteamVirtualGamepadInfo;
const SDL_SteamVirtualGamepadInfo *SDL_GetSteamVirtualGamepadInfo(int slot)
{
    return NULL;
}

bool SDL_UpdateSteamVirtualGamepadInfo(void)
{
    return false;
}
