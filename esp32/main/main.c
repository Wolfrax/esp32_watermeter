#include <math.h>
#include <string.h>
#include <time.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

#include "globals.h"
#include "mqtt_app.h"
#include "ota.h"
#include "pn5180.h"
#include "watermeter.h"
#include "wifi.h"

static pn5180_t s_pn5180;
static bool s_have_previous = false;
static watermeter_reading_t s_previous;

static void obtain_time(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1); // Sweden
    tzset();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retries = 0;
    while (timeinfo.tm_year < (2020 - 1900) && retries++ < 20) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        localtime_r(&now, &timeinfo);
    }
}

static bool reading_changed(const watermeter_reading_t *a, const watermeter_reading_t *b)
{
    if (a->decode_valid != b->decode_valid) return true;
    if (!a->decode_valid) return false; // both invalid: nothing to compare
    if (fabs(a->volume_m3 - b->volume_m3) > 0.00005) return true;
    return a->year != b->year || a->month != b->month || a->day != b->day;
}

static void poll_and_publish(void)
{
    watermeter_reading_t reading;
    if (!watermeter_read(&s_pn5180, &reading)) {
        ESP_LOGW(TAG, "Read failed (communication error), skipping this cycle");
        return;
    }

    if (!reading.tag_present) {
        ESP_LOGW(TAG, "No tag in range");
    } else if (!reading.uid_match) {
        ESP_LOGW(TAG, "Tag present but UID doesn't match the meter");
    } else if (!reading.decode_valid) {
        ESP_LOGW(TAG, "Tag matched but daily-log record didn't decode");
    } else {
        ESP_LOGI(TAG, "Read OK: %.4f m3 (tag date %04d-%02d-%02d)",
                 reading.volume_m3, reading.year, reading.month, reading.day);
    }

    bool changed = s_have_previous && reading_changed(&reading, &s_previous);
    mqtt_app_publish_reading(&reading, changed);

    s_previous = reading;
    s_have_previous = true;
}

void app_main(void)
{
    const esp_app_desc_t *app_desc = esp_app_get_description();
    ESP_LOGI(TAG, "%s starting, firmware version %s", TAG, app_desc->version);

    ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_base();
    wifi_start_sta();

    esp_err_t wifi_err = wifi_wait_connected(15000);

    if (wifi_err != ESP_OK && ota_is_pending_verify()) {
        // This is a freshly-flashed, not-yet-confirmed OTA image that can't
        // even reach WiFi — revert to the previous working image rather
        // than sit here bricked and unreachable for a future OTA push.
        ESP_LOGE(TAG, "New OTA image failed to connect to WiFi, rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
    ESP_ERROR_CHECK(wifi_err);

    obtain_time();
    ESP_LOGI(TAG, "Connected as %s (%s)", hostname, ip_str);

    ota_mark_valid_if_pending();
    ota_check_and_update();

    ESP_ERROR_CHECK(mqtt_app_start());

    ESP_ERROR_CHECK(pn5180_init(&s_pn5180, SPI2_HOST,
                                 PN5180_PIN_SCK, PN5180_PIN_MISO, PN5180_PIN_MOSI,
                                 PN5180_PIN_NSS, PN5180_PIN_BUSY, PN5180_PIN_RST,
                                 PN5180_PIN_CE));
    ESP_ERROR_CHECK(watermeter_init(&s_pn5180));

    ESP_LOGI(TAG, "Setup complete, polling every %d ms", POLL_INTERVAL_MS);

    while (true) {
        poll_and_publish();
        vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
    }
}
