#ifndef __NXDK_USB_H__
#define __NXDK_USB_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <windows.h>

int nxUsbInit();
void nxUsbShutdown();

#ifdef __cplusplus
}
#endif

#endif