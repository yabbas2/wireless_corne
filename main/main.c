/*
 * wireless_corne - USB-host to BLE HID bridge for a Corne v4.1 on a XIAO ESP32-S3.
 *
 * Phase 0/1: bring up the console on UART0, enumerate the keyboard over USB
 * host, and dump every HID report descriptor it exposes.
 *
 * Read docs/hardware.md before powering anything on. In particular: the boost
 * converter's trimpot must be set to 5.00 V under load and verified with a
 * meter BEFORE the keyboard is ever connected.
 */

#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "nvs_flash.h"

#include "usb_kbd.h"

static const char *TAG = "main";

static void log_banner(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    printf("\n\n");
    printf("=================================================================\n");
    printf(" wireless_corne  -  Corne v4.1 USB -> BLE HID bridge\n");
    printf(" phase 1: USB host enumeration + report descriptor dump\n");
    printf("=================================================================\n");
    ESP_LOGI(TAG, "chip: %d core(s), silicon rev %d.%d",
             chip.cores, chip.full_revision / 100, chip.full_revision % 100);
    ESP_LOGI(TAG, "free heap: %u bytes", (unsigned)esp_get_free_heap_size());
    printf("-----------------------------------------------------------------\n");
    printf("Console is on UART0 (D6=GPIO43 TX, D7=GPIO44 RX) @115200.\n");
    printf("USB-OTG and USB-Serial-JTAG share GPIO19/20, so USB serial is\n");
    printf("unavailable while running as a host. To flash: hold BOOT, tap\n");
    printf("RESET, then `idf.py flash`.\n");
    printf("-----------------------------------------------------------------\n\n");
}

void app_main(void)
{
    log_banner();

    /*
     * NVS is not strictly needed until Phase 4 (BLE bond storage), but bringing
     * it up now means a corrupt/absent partition surfaces immediately rather
     * than three phases later.
     */
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS needs erasing (%s), reformatting", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(usb_kbd_start());

    /*
     * Heartbeat. If this stops printing, something wedged - which is worth
     * knowing during bring-up when the keyboard may simply not be enumerating.
     */
    uint32_t ticks = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        ESP_LOGI(TAG, "alive (%us), free heap %u",
                 (unsigned)(++ticks * 10), (unsigned)esp_get_free_heap_size());
    }
}
