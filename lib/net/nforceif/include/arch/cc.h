/*
 * Copyright (c) 2001-2003 Swedish Institute of Computer Science.
 * Copyright (c) 2015 Matt Borgerson
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, 
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT 
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN 
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING 
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY 
 * OF SUCH DAMAGE.
 *
 * This file is part of the lwIP TCP/IP stack.
 * 
 * Author: Adam Dunkels <adam@sics.se>
 * Xbox Support: Matt Borgerson
 *
 */
#ifndef LWIP_ARCH_CC_H
#define LWIP_ARCH_CC_H

/* Include some files for defining library routines */
#include <string.h>
#include <hal/debug.h>
#include <windows.h>

#define printf debugPrint
#define fflush(...) 
void abort(void);

//#define MEM_ALIGNMENT 4

/* Define platform endianness */
//#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
//#endif /* BYTE_ORDER */

typedef int sys_prot_t;

/* Define generic types used in lwIP */
typedef unsigned   char    u8_t;
typedef signed     char    s8_t;
typedef unsigned   short   u16_t;
typedef signed     short   s16_t;
typedef unsigned   int     u32_t;
typedef signed     int     s32_t;

//FIXME: Stub how win32 crypto functions
#define HCRYPTPROV HANDLE
#define PROV_RSA_FULL 0
#define CRYPT_NEWKEYSET 0
static inline BOOL CryptAcquireContext(HCRYPTPROV *phProv, LPCSTR szContainer, LPCSTR szProvider, DWORD dwProvType, DWORD dwFlags)
{
  return TRUE;
}

static inline BOOL CryptGenRandom(HCRYPTPROV hProv, DWORD dwLen, BYTE *pbBuffer)
{
  *pbBuffer = (u32_t)GetTickCount();
  return TRUE;
}

// FIXME: Stub control win32 stuff
#define MOUSE_EVENT_RECORD PVOID
#define MENU_EVENT_RECORD PVOID
#define WINDOW_BUFFER_SIZE_RECORD PVOID
#define FOCUS_EVENT_RECORD PVOID
#define STD_INPUT_HANDLE ((DWORD)-10)
#define KEY_EVENT 0x1
#define WINBOOL BOOL
typedef struct _KEY_EVENT_RECORD
{
  WINBOOL bKeyDown;
  WORD wRepeatCount;
  WORD wVirtualKeyCode;
  WORD wVirtualScanCode;
  union
  {
    WCHAR UnicodeChar;
    CHAR AsciiChar;
  } uChar;
  DWORD dwControlKeyState;
} KEY_EVENT_RECORD, *PKEY_EVENT_RECORD;

typedef struct _INPUT_RECORD
{
  WORD EventType;
  union
  {
    KEY_EVENT_RECORD KeyEvent;
    MOUSE_EVENT_RECORD MouseEvent;
    WINDOW_BUFFER_SIZE_RECORD WindowBufferSizeEvent;
    MENU_EVENT_RECORD MenuEvent;
    FOCUS_EVENT_RECORD FocusEvent;
  } Event;
} INPUT_RECORD, *PINPUT_RECORD;

static inline BOOL WINAPI PeekConsoleInput(HANDLE hConsoleInput, PINPUT_RECORD lpBuffer, DWORD nLength, LPDWORD lpNumberOfEventsRead)
{
  return FALSE;
}

static inline HANDLE WINAPI GetStdHandle(DWORD nStdHandle)
{
  return NULL;
}

static inline BOOL WINAPI ReadConsoleInput(HANDLE hConsoleInput, PINPUT_RECORD lpBuffer, DWORD nLength, LPDWORD lpNumberOfEventsRead)
{
  return FALSE;
}


typedef unsigned long mem_ptr_t;

/* Define (sn)printf formatters for these lwIP types */
#define X8_F  "02x"
#define U16_F "hu"
#define S16_F "hd"
#define X16_F "hx"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"

/* If only we could use C99 and get %zu */
#if defined(__x86_64__)
#define SZT_F "lu"
#else
#define SZT_F "u"
#endif

/* Compiler hints for packing structures */
#define PACK_STRUCT_FIELD(x) x
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_END

/* Plaform specific diagnostic output */
#define LWIP_PLATFORM_DIAG(x)	do {printf x;} while(0)

#define LWIP_ERROR(message, expression, handler) do { if (!(expression)) { \
  LWIP_PLATFORM_DIAG(("Assertion \"%s\" failed at line %d in %s\n", message, __LINE__, __FILE__)); \
  handler;} } while(0)

#ifdef LWIP_UNIX_EMPTY_ASSERT
#define LWIP_PLATFORM_ASSERT(x)
#else
#define LWIP_PLATFORM_ASSERT(x) do {printf("Assertion \"%s\" failed at line %d in %s\n", \
                                     x, __LINE__, __FILE__); fflush(NULL); abort();} while(0)
#endif

#define LWIP_RAND() ((u32_t)GetTickCount()) /* Not really random... */

struct sio_status_s;
typedef struct sio_status_s sio_status_t;
#define sio_fd_t sio_status_t*
#define __sio_fd_t_defined

#endif /* LWIP_ARCH_CC_H */
