// Ported from esp32_watertank/esp32/main/ota.c — same manifest-based
// OTA convention (see OTA_MANIFEST_URL in globals.h), unchanged.

#include <string.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_app_desc.h"
#include "esp_system.h"
#include "globals.h"
#include "ota.h"

#define MANIFEST_BUF_LEN 256

bool ota_is_pending_verify(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;

    return esp_ota_get_state_partition(running, &state) == ESP_OK &&
           state == ESP_OTA_IMG_PENDING_VERIFY;
}

void ota_mark_valid_if_pending(void)
{
    if (ota_is_pending_verify()) {
        ESP_LOGI(TAG, "New OTA image confirmed healthy (WiFi connected), cancelling rollback");
        esp_ota_mark_app_valid_cancel_rollback();
    }
}

static esp_err_t fetch_manifest(char *buf, size_t buf_len)
{
    esp_http_client_config_t config = {
        .url = OTA_MANIFEST_URL,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }

    esp_http_client_fetch_headers(client);

    int total = 0;
    int r;
    while (total < (int)buf_len - 1 &&
           (r = esp_http_client_read(client, buf + total, buf_len - 1 - total)) > 0) {
        total += r;
    }
    buf[total] = '\0';

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    return total > 0 ? ESP_OK : ESP_FAIL;
}

void ota_check_and_update(void)
{
    char manifest[MANIFEST_BUF_LEN];

    if (fetch_manifest(manifest, sizeof(manifest)) != ESP_OK) {
        ESP_LOGW(TAG, "OTA manifest fetch failed (%s unreachable?), skipping update check", OTA_MANIFEST_URL);
        return;
    }

    char version[32] = {0};
    char url[192] = {0};

    if (sscanf(manifest, "%31s %191s", version, url) != 2) {
        ESP_LOGW(TAG, "OTA manifest malformed, skipping");
        return;
    }

    const esp_app_desc_t *running_desc = esp_app_get_description();

    if (strcmp(version, running_desc->version) == 0) {
        ESP_LOGI(TAG, "Already running latest version (%s)", running_desc->version);
        return;
    }

    ESP_LOGI(TAG, "New firmware available: %s -> %s (%s)", running_desc->version, version, url);

    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = 15000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_err_t err = esp_https_ota(&ota_config);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA update applied, rebooting into new image");
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA update failed: %s", esp_err_to_name(err));
    }
}
