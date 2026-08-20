/*
 * USB Host HID front-end - see usb_kbd.h
 *
 * API verified against espressif/usb_host_hid v1.2.0.
 *
 * -------------------------------------------------------------------------
 * IMPORTANT DEVIATION FROM ESPRESSIF'S EXAMPLE
 * -------------------------------------------------------------------------
 * The upstream esp-idf example (examples/peripherals/usb/host/hid) only opens
 * interfaces where `dev_params.proto != HID_PROTOCOL_NONE`, i.e. only those
 * advertising the Boot keyboard/mouse protocols.
 *
 * That would be wrong here. QMK splits its HID surface across several
 * interfaces, and the interesting ones report HID_PROTOCOL_NONE:
 *
 *   - Keyboard        (Boot subclass, HID_PROTOCOL_KEYBOARD)  - basic + NKRO
 *   - Mouse/Extrakey  (proto NONE)  - consumer + system control + mouse
 *   - Raw HID / VIA   (proto NONE, vendor-defined)  - if RAW_ENABLE=yes
 *   - Console         (proto NONE, vendor-defined)  - if CONSOLE_ENABLE=yes
 *
 * Skipping NONE would silently drop media keys and mouse support. So we open
 * everything and let Phase 3 decide what to forward over BLE.
 * -------------------------------------------------------------------------
 */

#include "usb_kbd.h"
#include "hid_dump.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_log.h"
#include "esp_err.h"

#include "usb/usb_host.h"
#include "usb/hid_host.h"

static const char *TAG = "usb_kbd";

/*
 * Core affinity.
 *
 * The BLE controller pins to core 0 by default on ESP32-S3, so we keep the USB
 * host stack on core 1 to avoid fighting it once Phase 2 lands. Revisit if
 * profiling says otherwise.
 */
#define USB_TASK_CORE     1
#define USB_TASK_PRIO     5
#define HID_DRV_TASK_PRIO 5
#define HID_DRV_STACK     4096

#define MAX_REPORT_LEN    64

typedef struct {
    hid_host_device_handle_t handle;
    hid_host_driver_event_t  event;
    void                    *arg;
} hid_driver_evt_t;

static QueueHandle_t s_driver_evt_q;

/* ------------------------------------------------------------------------ */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------ */

static const char *subclass_str(uint8_t sc)
{
    return (sc == HID_SUBCLASS_BOOT_INTERFACE) ? "Boot" : "None";
}

static const char *proto_str(uint8_t p)
{
    switch (p) {
    case HID_PROTOCOL_NONE:     return "None";
    case HID_PROTOCOL_KEYBOARD: return "Keyboard";
    case HID_PROTOCOL_MOUSE:    return "Mouse";
    default:                    return "?";
    }
}

/*
 * wchar_t on ESP32 is 4 bytes and newlib's %ls is unreliable here, so flatten
 * USB string descriptors to ASCII by hand. Non-ASCII becomes '?'.
 */
static void wstr_to_ascii(const wchar_t *src, char *dst, size_t dst_len)
{
    size_t i = 0;
    for (; i + 1 < dst_len && src[i] != L'\0'; i++) {
        wchar_t c = src[i];
        dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
    }
    dst[i] = '\0';
}

static void log_device_info(hid_host_device_handle_t handle)
{
    hid_host_dev_info_t info;
    if (hid_host_get_device_info(handle, &info) != ESP_OK) {
        ESP_LOGW(TAG, "could not read device info");
        return;
    }

    char mfr[HID_STR_DESC_MAX_LENGTH + 1];
    char prod[HID_STR_DESC_MAX_LENGTH + 1];
    char serial[HID_STR_DESC_MAX_LENGTH + 1];
    wstr_to_ascii(info.iManufacturer, mfr, sizeof(mfr));
    wstr_to_ascii(info.iProduct, prod, sizeof(prod));
    wstr_to_ascii(info.iSerialNumber, serial, sizeof(serial));

    ESP_LOGI(TAG, "device VID=0x%04X PID=0x%04X", info.VID, info.PID);
    ESP_LOGI(TAG, "  manufacturer: '%s'", mfr);
    ESP_LOGI(TAG, "  product     : '%s'", prod);
    ESP_LOGI(TAG, "  serial      : '%s'", serial);

    /* Corne v4.1 ships QMK's foostan VID. Flag anything unexpected loudly. */
    if (info.VID != 0x4653) {
        ESP_LOGW(TAG, "  (expected VID 0x4653 for a stock Corne v4.1 - got 0x%04X)", info.VID);
    }
}

/* ------------------------------------------------------------------------ */
/* Interface (per-endpoint) events                                           */
/* ------------------------------------------------------------------------ */

static void iface_event_cb(hid_host_device_handle_t handle,
                           const hid_host_interface_event_t event,
                           void *arg)
{
    hid_host_dev_params_t params;
    if (hid_host_device_get_params(handle, &params) != ESP_OK) {
        return;
    }

    switch (event) {
    case HID_HOST_INTERFACE_EVENT_INPUT_REPORT: {
        uint8_t data[MAX_REPORT_LEN];
        size_t len = 0;

        /*
         * Must copy promptly - the driver reuses this buffer for the next
         * transfer. Phase 1 just prints it; Phase 3 will hand it straight to
         * the BLE layer instead of doing I/O in this callback.
         */
        if (hid_host_device_get_raw_input_report_data(handle, data, sizeof(data), &len) != ESP_OK) {
            return;
        }

        char prefix[32];
        snprintf(prefix, sizeof(prefix), "  IN  if%u ", params.iface_num);
        printf("[report] iface %u, %u bytes\n", params.iface_num, (unsigned)len);
        hid_dump_hex(prefix, data, len);
        break;
    }

    case HID_HOST_INTERFACE_EVENT_TRANSFER_ERROR:
        ESP_LOGW(TAG, "iface %u: transfer error", params.iface_num);
        break;

    case HID_HOST_INTERFACE_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "iface %u: disconnected", params.iface_num);
        ESP_ERROR_CHECK(hid_host_device_close(handle));
        break;

    default:
        ESP_LOGW(TAG, "iface %u: unhandled interface event %d", params.iface_num, (int)event);
        break;
    }
}

/* ------------------------------------------------------------------------ */
/* Device connect                                                            */
/* ------------------------------------------------------------------------ */

static void handle_connected(hid_host_device_handle_t handle)
{
    hid_host_dev_params_t params;
    ESP_ERROR_CHECK(hid_host_device_get_params(handle, &params));

    ESP_LOGI(TAG, "--------------------------------------------------------");
    ESP_LOGI(TAG, "HID interface connected: addr=%u iface=%u subclass=%s proto=%s",
             params.addr, params.iface_num,
             subclass_str(params.sub_class), proto_str(params.proto));

    const hid_host_device_config_t dev_cfg = {
        .callback     = iface_event_cb,
        .callback_arg = NULL,
    };
    ESP_ERROR_CHECK(hid_host_device_open(handle, &dev_cfg));

    /* Device-level info is the same for every interface; log it once. */
    if (params.iface_num == 0) {
        log_device_info(handle);
    }

    /*
     * Select Report protocol.
     *
     * Only meaningful on Boot-subclass interfaces; a non-boot interface will
     * STALL Set_Protocol, so guard it. We deliberately choose REPORT over BOOT:
     * Boot protocol would give us a fixed 8-byte 6KRO report and throw away
     * NKRO, consumer/media keys and mouse - the whole point of Phase 3.
     */
    if (params.sub_class == HID_SUBCLASS_BOOT_INTERFACE) {
        esp_err_t err = hid_class_request_set_protocol(handle, HID_REPORT_PROTOCOL_REPORT);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "  Set_Protocol(REPORT) failed: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "  Set_Protocol(REPORT) ok");
        }
    }

    /*
     * Set_Idle(0,0) = report only on change, no periodic resend.
     * Not all interfaces implement it; a STALL here is harmless, so this must
     * NOT be ESP_ERROR_CHECK'd.
     */
    esp_err_t idle_err = hid_class_request_set_idle(handle, 0, 0);
    if (idle_err != ESP_OK) {
        ESP_LOGD(TAG, "  Set_Idle unsupported on this interface (%s) - fine",
                 esp_err_to_name(idle_err));
    }

    /* The Phase 1 payload: what does this interface actually report? */
    size_t desc_len = 0;
    uint8_t *desc = hid_host_get_report_descriptor(handle, &desc_len);
    if (desc != NULL) {
        char label[48];
        snprintf(label, sizeof(label), "iface %u (%s/%s)",
                 params.iface_num, subclass_str(params.sub_class), proto_str(params.proto));
        hid_dump_report_descriptor(desc, desc_len, label);
    } else {
        ESP_LOGW(TAG, "  no report descriptor available for iface %u", params.iface_num);
    }

    ESP_ERROR_CHECK(hid_host_device_start(handle));
    ESP_LOGI(TAG, "  iface %u started", params.iface_num);
}

/* ------------------------------------------------------------------------ */
/* Tasks                                                                     */
/* ------------------------------------------------------------------------ */

/* Called from the HID class driver's own task; keep it short. */
static void driver_event_cb(hid_host_device_handle_t handle,
                            const hid_host_driver_event_t event,
                            void *arg)
{
    const hid_driver_evt_t evt = {
        .handle = handle,
        .event  = event,
        .arg    = arg,
    };
    if (s_driver_evt_q != NULL) {
        xQueueSend(s_driver_evt_q, &evt, 0);
    }
}

static void hid_app_task(void *arg)
{
    hid_driver_evt_t evt;
    while (true) {
        if (xQueueReceive(s_driver_evt_q, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (evt.event == HID_HOST_DRIVER_EVENT_CONNECTED) {
            handle_connected(evt.handle);
        } else {
            ESP_LOGW(TAG, "unhandled driver event %d", (int)evt.event);
        }
    }
}

static void usb_lib_task(void *arg)
{
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags     = ESP_INTR_FLAG_LOWMED,
    };
    ESP_ERROR_CHECK(usb_host_install(&host_config));
    ESP_LOGI(TAG, "USB host library installed");

    xTaskNotifyGive((TaskHandle_t)arg);

    while (true) {
        uint32_t event_flags;
        usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            ESP_LOGI(TAG, "no more USB clients");
            usb_host_device_free_all();
        }
        if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) {
            ESP_LOGI(TAG, "all USB devices freed");
        }
    }
}

/* ------------------------------------------------------------------------ */
/* Public                                                                    */
/* ------------------------------------------------------------------------ */

esp_err_t usb_kbd_start(void)
{
    s_driver_evt_q = xQueueCreate(8, sizeof(hid_driver_evt_t));
    if (s_driver_evt_q == NULL) {
        return ESP_ERR_NO_MEM;
    }

    /* Start the USB host library and wait until it is actually installed. */
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    BaseType_t ok = xTaskCreatePinnedToCore(usb_lib_task, "usb_lib", 4096, self,
                                            USB_TASK_PRIO, NULL, USB_TASK_CORE);
    if (ok != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

    const hid_host_driver_config_t hid_cfg = {
        .create_background_task = true,
        .task_priority          = HID_DRV_TASK_PRIO,
        .stack_size             = HID_DRV_STACK,
        .core_id                = USB_TASK_CORE,
        .callback               = driver_event_cb,
        .callback_arg           = NULL,
    };
    ESP_ERROR_CHECK(hid_host_install(&hid_cfg));
    ESP_LOGI(TAG, "HID host class driver installed");

    ok = xTaskCreatePinnedToCore(hid_app_task, "hid_app", 4096, NULL,
                                 USB_TASK_PRIO - 1, NULL, USB_TASK_CORE);
    if (ok != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "waiting for a HID device on the USB port...");
    return ESP_OK;
}
