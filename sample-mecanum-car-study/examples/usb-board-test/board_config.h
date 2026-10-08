#ifndef BOARD_TEST_CONFIG_H
#define BOARD_TEST_CONFIG_H
#ifndef USB_TEST_SLAVE
#define USB_TEST_SLAVE 0
#endif
#if USB_TEST_SLAVE
#define BOARD_TEST_FIRMWARE "usb-slave-test-v1"
#define BOARD_TEST_PRODUCT "F103 USB Slave Test"
#else
#define BOARD_TEST_FIRMWARE "usb-board-test-v1"
#define BOARD_TEST_PRODUCT "F103 USB Board Test"
#endif
#endif
