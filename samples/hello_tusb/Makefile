XBE_TITLE = nxdk\ sample\ -\ hello
GEN_XISO = $(XBE_TITLE).iso
SRCS = $(CURDIR)/main.c $(CURDIR)/lib/nxdk/nxusb.c

USBH_SRC_DIR = $(CURDIR)/tinyusb_src/src
TUSB_SRCS = \
	$(USBH_SRC_DIR)/tusb.c \
	$(USBH_SRC_DIR)/common/tusb_fifo.c \
	$(USBH_SRC_DIR)/host/hub.c \
	$(USBH_SRC_DIR)/host/usbh.c \
	$(USBH_SRC_DIR)/portable/ohci/ohci.c \
	$(USBH_SRC_DIR)/class/hid/hid_host.c
TUSB_CFLAGS = -I${USBH_SRC_DIR} -DCFG_TUH_TASK_QUEUE_SZ=32 -DCFG_TUSB_DEBUG_PRINTF=tusb_print_hook -DCFG_TUSB_DEBUG=3 -DTUH_OPT_RHPORT=0

CFLAGS += ${TUSB_CFLAGS} -DDEBUG_CONSOLE -O2 -I$(CURDIR) -I$(CURDIR)/lib
SRCS += ${TUSB_SRCS}

NXDK_DIR ?= $(CURDIR)/../..

include $(NXDK_DIR)/Makefile
