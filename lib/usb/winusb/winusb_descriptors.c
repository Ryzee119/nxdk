/*
 * SPDX-License-Identifier: CC0-1.0
 *
 * WinUSB descriptor helpers: descriptor tree walking and lookups.
 */

#include <stdint.h>
#include <string.h>

#include "winusb_internal.h"

#define USB_INTERFACE_DESCRIPTOR_TYPE 4
#define USB_ENDPOINT_DESCRIPTOR_TYPE  5

PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseDescriptors(
    _In_ PVOID DescriptorBuffer,
    _In_ ULONG TotalLength,
    _In_ PVOID StartPosition,
    _In_ LONG  DescriptorType
)
{
    const PUCHAR buffer = (PUCHAR)DescriptorBuffer;
    PUCHAR position = (PUCHAR)StartPosition;
    PUCHAR end;

    if (buffer == NULL || position == NULL || TotalLength < 2) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    end = buffer + TotalLength;

    if (position < buffer) {
        position = buffer;
    }

    while ((position + 2) <= end) {
        PUSB_COMMON_DESCRIPTOR descriptor = (PUSB_COMMON_DESCRIPTOR)position;

        if (descriptor->bLength < 2) {
            break;
        }

        if (descriptor->bDescriptorType == (UCHAR)DescriptorType) {
            return descriptor;
        }

        position += descriptor->bLength;
    }

    SetLastError(ERROR_NO_MORE_ITEMS);
    return NULL;
}

PUSB_COMMON_DESCRIPTOR WINAPI WinUsb_ParseConfigurationDescriptor(
    _In_ PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor,
    _In_ PVOID                         StartPosition,
    _In_ LONG                          InterfaceNumber,
    _In_ LONG                          AlternateSetting,
    _In_ LONG                          InterfaceClass,
    _In_ LONG                          InterfaceSubClass,
    _In_ LONG                          InterfaceProtocol
)
{
    const PUCHAR buffer = (PUCHAR)ConfigurationDescriptor;
    PUCHAR position;
    PUCHAR end;

    if (ConfigurationDescriptor == NULL || StartPosition == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return NULL;
    }

    position = (PUCHAR)StartPosition;
    end = buffer + ConfigurationDescriptor->wTotalLength;

    if (position < buffer) {
        position = buffer;
    }

    while ((position + 2) <= end) {
        PUSB_COMMON_DESCRIPTOR descriptor = (PUSB_COMMON_DESCRIPTOR)position;

        if (descriptor->bLength < 2) {
            break;
        }

        if (descriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE &&
            descriptor->bLength >= sizeof(USB_INTERFACE_DESCRIPTOR)) {
            PUSB_INTERFACE_DESCRIPTOR itf = (PUSB_INTERFACE_DESCRIPTOR)descriptor;

            if ((InterfaceNumber  < 0 || itf->bInterfaceNumber  == (UCHAR)InterfaceNumber) &&
                (AlternateSetting < 0 || itf->bAlternateSetting == (UCHAR)AlternateSetting) &&
                (InterfaceClass   < 0 || itf->bInterfaceClass   == (UCHAR)InterfaceClass) &&
                (InterfaceSubClass< 0 || itf->bInterfaceSubClass== (UCHAR)InterfaceSubClass) &&
                (InterfaceProtocol< 0 || itf->bInterfaceProtocol== (UCHAR)InterfaceProtocol)) {
                return descriptor;
            }
        }

        position += descriptor->bLength;
    }

    SetLastError(ERROR_NO_MORE_ITEMS);
    return NULL;
}

/*--------------------------------------------------------------------
 * INTERNAL HELPERS
 *--------------------------------------------------------------------*/

PUSB_INTERFACE_DESCRIPTOR winusb_find_interface_descriptor(winusb_device_t *dev,
                                                           LONG itf_num, LONG alt_setting)
{
    if (dev == NULL || dev->config_descriptor == NULL) {
        return NULL;
    }

    return (PUSB_INTERFACE_DESCRIPTOR)WinUsb_ParseConfigurationDescriptor(
        (PUSB_CONFIGURATION_DESCRIPTOR)dev->config_descriptor,
        dev->config_descriptor,
        itf_num, alt_setting, -1, -1, -1);
}

PUSB_ENDPOINT_DESCRIPTOR winusb_find_endpoint_descriptor(winusb_device_t *dev,
                                                         PUSB_INTERFACE_DESCRIPTOR itf,
                                                         UCHAR index)
{
    PUCHAR position;
    PUCHAR end;
    UCHAR found = 0;

    if (dev == NULL || itf == NULL || dev->config_descriptor == NULL) {
        return NULL;
    }

    if (index >= itf->bNumEndpoints) {
        return NULL;
    }

    position = (PUCHAR)itf + itf->bLength;
    end = dev->config_descriptor + dev->config_descriptor_len;

    while ((position + 2) <= end) {
        PUSB_COMMON_DESCRIPTOR descriptor = (PUSB_COMMON_DESCRIPTOR)position;

        if (descriptor->bLength < 2) {
            break;
        }

        /* Stop when the next interface starts */
        if (descriptor->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE) {
            break;
        }

        if (descriptor->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE) {
            if (found == index) {
                return (PUSB_ENDPOINT_DESCRIPTOR)descriptor;
            }
            found++;
        }

        position += descriptor->bLength;
    }

    return NULL;
}

/*--------------------------------------------------------------------
 * GET DESCRIPTOR
 *--------------------------------------------------------------------*/

BOOL WINAPI WinUsb_GetDescriptor(
    _In_  WINUSB_INTERFACE_HANDLE InterfaceHandle,
    _In_  UCHAR                   DescriptorType,
    _In_  UCHAR                   Index,
    _In_  USHORT                  LanguageID,
    _Out_ PUCHAR                  Buffer,
    _In_  ULONG                   BufferLength,
    _Out_ PULONG                  LengthTransferred
)
{
    winusb_interface_t *itf;
    WINUSB_SETUP_PACKET setup;

    if (Buffer == NULL || LengthTransferred == NULL || BufferLength == 0) {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    *LengthTransferred = 0;

    itf = winusb_interface_validate(InterfaceHandle);
    if (itf == NULL) {
        return FALSE;
    }

    memset(&setup, 0, sizeof(setup));
    setup.RequestType = 0x80;   /* device to host, standard, device */
    setup.Request     = 0x06;   /* GET_DESCRIPTOR */
    setup.Value       = (USHORT)(((USHORT)DescriptorType << 8) | Index);
    setup.Index       = (DescriptorType == 3 /* STRING */) ? LanguageID : 0;
    setup.Length      = (USHORT)BufferLength;

    return winusb_xfer(itf, 0, Buffer, BufferLength, &setup, NULL, 0, LengthTransferred);
}
