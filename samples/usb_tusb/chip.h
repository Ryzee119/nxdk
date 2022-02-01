#include <xboxkrnl/xboxkrnl.h>
#define LPC_USB_BASE 0xFED00000
#define OHCI_RH_PORTS 4

static inline void *tusb_virtobus(void *x)
{
    if ((uint32_t)x == 0x80000000)
        return NULL;

    //Already is a physical address
    if (((uint32_t)x & 0x80000000) == 0)
    {
        TU_LOG1("Already a physical address %d\n");
        return x;
    }
    return (void *)MmGetPhysicalAddress(x);
}

static inline void *tusb_bustovir(void *x)
{
    if (x == NULL)
        return NULL;

    //Already is a virtual address
    if (((uint32_t)x & 0x80000000) != 0)
    {
        TU_LOG1("Already a virtual address\n");
        return x;
    }
        

    return (void *)(0x80000000 | (uint32_t)x);
}

static inline void *tusb_busalloc(uint32_t x)
{
    return MmAllocateContiguousMemoryEx(x,
                                        0,
                                        64 * 1024 * 1024,
                                        PAGE_SIZE,
                                        PAGE_READWRITE | PAGE_NOCACHE);
}

static inline void tusb_busfree(void *x)
{
    MmFreeContiguousMemory(x);
}

static inline bool tusb_candma(void *x)
{
    if (((uint32_t)x & 0x80000000) != 0)
    {
        return true;
    }
    return false;
}

#define CAN_DMA(x) tusb_candma((x))
#define VIR2BUS(x) tusb_virtobus((x))   /* Conversion of Address from Virtual to Physical       */
#define BUS2VIR(x) tusb_bustovir((x))   /* Conversion of Address from Physical to Virtual       */
#define BUS_ALLOC(x) tusb_busalloc((x)) /* Allocate DMA friendly contiguous memory */
#define BUS_FREE(x) tusb_busfree((x)) /* Free contiguous memory */