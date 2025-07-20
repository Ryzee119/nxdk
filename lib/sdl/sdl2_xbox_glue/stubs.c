#include <windows.h>
#include <SDL.h>

int WIN_SetError(const char *prefix)
{
    DWORD error = GetLastError();
    char buffer[256];
    SDL_snprintf(buffer, sizeof(buffer), "Error: %s: Code: %lu", (prefix) ? prefix : "", error);
    SDL_SetError("%s", buffer);
    return -1;
}

HMODULE GetModuleHandle(LPCSTR lpModuleName)
{
    return NULL;
}
