/*
 * USB Host HID front-end: enumerates the Corne and pulls its HID reports.
 *
 * Phase 1 scope: enumerate every HID interface the keyboard exposes, select
 * Report protocol (not Boot), dump each interface's report descriptor, and log
 * incoming input reports.
 *
 * Phase 3 will replace the logging with a translation path into the BLE HID
 * report layer.
 */
#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bring up the USB host stack and the HID class driver.
 *
 * Starts two background tasks:
 *   - the USB Host library event task,
 *   - the HID host driver task (created by the class driver itself),
 * plus an application task that handles connect/disconnect events.
 *
 * @return ESP_OK on success.
 */
esp_err_t usb_kbd_start(void);

#ifdef __cplusplus
}
#endif
