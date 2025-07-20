#include <SDL3/SDL.h>
#include <windows.h>
#include "../SDL_build_config.h"
#include <../src/time/SDL_time_c.h>

#ifdef SDL_TIME_NXDK

#define NS_PER_WINDOWS_TICK 100ULL
#define WINDOWS_TICK 10000000ULL
#define UNIX_EPOCH_OFFSET_SEC 11644473600ULL

void SDL_GetSystemTimeLocalePreferences(SDL_DateFormat *df, SDL_TimeFormat *tf)
{
    // SDL defaults to ISO 8061 date format already.
    return;
}

bool SDL_GetCurrentTime(SDL_Time *ticks)
{
    FILETIME ft;

    if (!ticks)
    {
        return SDL_InvalidParamError("ticks");
    }

    GetSystemTimePreciseAsFileTime(&ft);

    ULARGE_INTEGER uli = {
        .LowPart = ft.dwLowDateTime,
        .HighPart = ft.dwHighDateTime};

    *ticks = SDL_TimeFromWindows(ft.dwLowDateTime, ft.dwHighDateTime);

    return true;
}

bool SDL_TimeToDateTime(SDL_Time ticks, SDL_DateTime *dt, bool localTime)
{
    if (!dt)
    {
        return SDL_InvalidParamError("dt");
    }

    if (localTime)
    {
        TIME_ZONE_INFORMATION timezone;
        DWORD result = GetTimeZoneInformation(&timezone);

        dt->utc_offset = -timezone.Bias;
        if (result == TIME_ZONE_ID_STANDARD)
        {
            dt->utc_offset -= timezone.StandardBias;
        }
        else if (result == TIME_ZONE_ID_DAYLIGHT)
        {
            dt->utc_offset -= timezone.DaylightBias;
        }
        dt->utc_offset *= 60;

        ticks += SDL_NS_PER_SECOND * dt->utc_offset;
    }
    else
    {
        dt->utc_offset = 0;
    }

    Uint32 low, high;
    SDL_TimeToWindows(ticks, &low, &high);

    LARGE_INTEGER file_time = {
        .LowPart = low,
        .HighPart = high};

    TIME_FIELDS time_field;
    RtlTimeToTimeFields(&file_time, &time_field);

    dt->year = time_field.Year;
    dt->month = time_field.Month;
    dt->day = time_field.Day;
    dt->hour = time_field.Hour;
    dt->minute = time_field.Minute;
    dt->second = time_field.Second;
    dt->nanosecond = ticks % SDL_NS_PER_SECOND;
    dt->day_of_week = time_field.Weekday;
    return true;
}

#endif
