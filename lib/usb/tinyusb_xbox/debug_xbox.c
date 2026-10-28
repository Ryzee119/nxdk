/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * TinyUSB debug output for nxdk.
 *
 * TinyUSB's TU_LOG()/tu_printf() is redirected here through
 * CFG_TUSB_DEBUG_PRINTF. The message is formatted with vsnprintf() and then
 * handed to the Xbox kernel's DbgPrint(), which the kernel routes to the
 * debug serial port (xemu needs "-device lpc47m157 -serial stdio" for it).
 *
 * DbgPrint() is called with a plain "%s" because its own format support is
 * limited to what the kernel implements.
 */

#include <stdarg.h>
#include <stdio.h>

#include <xboxkrnl/xboxkrnl.h>

#include "tusb_option.h"

int nxdk_tu_printf(const char* format, ...)
{
  char buffer[256];

  va_list args;
  va_start(args, format);
  int length = vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  if (length > 0) {
    DbgPrint("%s", buffer);
  }

  return length;
}
