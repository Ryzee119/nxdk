#include <hal/debug.h>
#include <hal/video.h>
#include <nxdk/nxusb.h>
#include <windows.h>


int main(void)
{
    XVideoSetMode(640, 480, 32, REFRESH_DEFAULT);

    //usbhcd_context_t usb;
    
    nxUsbInit();
    while(1) {
        debugPrint("USB!\n");
        Sleep(2000);
    }

    return 0;
}
