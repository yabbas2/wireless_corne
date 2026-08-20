/*
 * Host-side test for the HID report descriptor parser.
 *
 * hid_dump.c has no ESP-IDF dependencies, so it can be compiled and exercised
 * with a plain host compiler. That lets us validate the parser without any
 * hardware - useful, because getting the report-size accounting right is the
 * whole basis of the Phase 3 BLE report map.
 *
 * Build & run:  make -C test/host run
 */

#include <stdio.h>
#include "hid_dump.h"

/*
 * Case 1: the canonical USB HID boot keyboard descriptor (HID 1.11, App. B.1).
 *
 * Known-good expected output:
 *   Uses Report IDs : NO
 *   Input           : 8 (modifiers) + 8 (reserved) + 48 (6 keycodes) = 64 bits = 8 bytes
 *   Output          : 5 (LEDs) + 3 (padding) = 8 bits = 1 byte
 */
static const uint8_t boot_keyboard[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)     */
    0x09, 0x06,        /* Usage (Keyboard)                 */
    0xA1, 0x01,        /* Collection (Application)         */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad)   */
    0x19, 0xE0,        /*   Usage Minimum (0xE0)           */
    0x29, 0xE7,        /*   Usage Maximum (0xE7)           */
    0x15, 0x00,        /*   Logical Minimum (0)            */
    0x25, 0x01,        /*   Logical Maximum (1)            */
    0x75, 0x01,        /*   Report Size (1)                */
    0x95, 0x08,        /*   Report Count (8)               */
    0x81, 0x02,        /*   Input (Data,Var,Abs)           */
    0x95, 0x01,        /*   Report Count (1)               */
    0x75, 0x08,        /*   Report Size (8)                */
    0x81, 0x03,        /*   Input (Const,Var,Abs)          */
    0x95, 0x05,        /*   Report Count (5)               */
    0x75, 0x01,        /*   Report Size (1)                */
    0x05, 0x08,        /*   Usage Page (LED)               */
    0x19, 0x01,        /*   Usage Minimum (1)              */
    0x29, 0x05,        /*   Usage Maximum (5)              */
    0x91, 0x02,        /*   Output (Data,Var,Abs)          */
    0x95, 0x01,        /*   Report Count (1)               */
    0x75, 0x03,        /*   Report Size (3)                */
    0x91, 0x03,        /*   Output (Const,Var,Abs)         */
    0x95, 0x06,        /*   Report Count (6)               */
    0x75, 0x08,        /*   Report Size (8)                */
    0x15, 0x00,        /*   Logical Minimum (0)            */
    0x25, 0x65,        /*   Logical Maximum (101)          */
    0x05, 0x07,        /*   Usage Page (Keyboard/Keypad)   */
    0x19, 0x00,        /*   Usage Minimum (0)              */
    0x29, 0x65,        /*   Usage Maximum (101)            */
    0x81, 0x00,        /*   Input (Data,Array,Abs)         */
    0xC0               /* End Collection                   */
};

/*
 * Case 2: a multi-Report-ID descriptor shaped like QMK's "extrakey" interface -
 * System Control (report 2) plus Consumer Control (report 3). This is the
 * layout we expect to see on the Corne's non-boot interface.
 *
 * Expected:
 *   Uses Report IDs : YES
 *   0x02 input      : 16 bits = 2 bytes + 1 ID byte = 3
 *   0x03 input      : 16 bits = 2 bytes + 1 ID byte = 3
 */
static const uint8_t extrakey[] = {
    0x05, 0x01,        /* Usage Page (Generic Desktop)     */
    0x09, 0x80,        /* Usage (System Control)           */
    0xA1, 0x01,        /* Collection (Application)         */
    0x85, 0x02,        /*   Report ID (2)                  */
    0x19, 0x01,        /*   Usage Minimum (1)              */
    0x2A, 0xB7, 0x00,  /*   Usage Maximum (0xB7)           */
    0x15, 0x01,        /*   Logical Minimum (1)            */
    0x26, 0xB7, 0x00,  /*   Logical Maximum (0xB7)         */
    0x95, 0x01,        /*   Report Count (1)               */
    0x75, 0x10,        /*   Report Size (16)               */
    0x81, 0x00,        /*   Input (Data,Array,Abs)         */
    0xC0,              /* End Collection                   */

    0x05, 0x0C,        /* Usage Page (Consumer)            */
    0x09, 0x01,        /* Usage (Consumer Control)         */
    0xA1, 0x01,        /* Collection (Application)         */
    0x85, 0x03,        /*   Report ID (3)                  */
    0x19, 0x01,        /*   Usage Minimum (1)              */
    0x2A, 0xA0, 0x02,  /*   Usage Maximum (0x2A0)          */
    0x15, 0x01,        /*   Logical Minimum (1)            */
    0x26, 0xA0, 0x02,  /*   Logical Maximum (0x2A0)        */
    0x95, 0x01,        /*   Report Count (1)               */
    0x75, 0x10,        /*   Report Size (16)               */
    0x81, 0x00,        /*   Input (Data,Array,Abs)         */
    0xC0               /* End Collection                   */
};

int main(void)
{
    hid_dump_report_descriptor(boot_keyboard, sizeof(boot_keyboard),
                               "TEST: boot keyboard");
    printf("EXPECT -> no Report IDs; input 8 bytes; output 1 byte\n\n");

    hid_dump_report_descriptor(extrakey, sizeof(extrakey),
                               "TEST: system + consumer control");
    printf("EXPECT -> Report IDs YES; 0x02 input 3 bytes; 0x03 input 3 bytes\n\n");

    return 0;
}
