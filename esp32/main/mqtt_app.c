#include <string.h>
#include <time.h>

#include "mqtt_app.h"
#include "globals.h"
#include "cJSON.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define MQTT_CONNECTED_BIT BIT0

// Fixed (not per-hostname) topics: there's only ever one physical water
// meter reader, matching the esp32_watertank project's convention
// (e.g. "watertank/heater/power", not tied to a MAC-derived hostname)
// — see homeassistant-config/mqtt.yaml for the matching sensor entry.
#define STATE_TOPIC        "watermeter/state"
#define AVAILABILITY_TOPIC "watermeter/availability"

static esp_mqtt_client_handle_t s_client = NULL;
static EventGroupHandle_t s_mqtt_event_group;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            esp_mqtt_client_publish(s_client, AVAILABILITY_TOPIC, "online", 0, 1, true);
            xEventGroupSetBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            xEventGroupClearBits(s_mqtt_event_group, MQTT_CONNECTED_BIT);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGW(TAG, "MQTT error event");
            break;
        default:
            break;
    }
}

// One-time cleanup: an earlier firmware version published a retained
// MQTT-discovery config message per-hostname. This project doesn't
// use discovery (see homeassistant-config/mqtt.yaml — every sensor is
// hand-declared there instead), so clear that retained message rather
// than leave a stray untracked entity around. Empty retained payload
// deletes it per the MQTT spec. Safe to call every boot (no-op once
// already cleared).
static void clear_stale_discovery_config(void)
{
    char config_topic[128];
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s_volume/config", hostname);
    esp_mqtt_client_publish(s_client, config_topic, "", 0, 1, true);
}

esp_err_t mqtt_app_start(void)
{
    s_mqtt_event_group = xEventGroupCreate();

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .session.last_will.topic = AVAILABILITY_TOPIC,
        .session.last_will.msg = "offline",
        .session.last_will.qos = 1,
        .session.last_will.retain = true,
    };
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(s_client);
    if (err != ESP_OK) {
        return err;
    }

    err = mqtt_app_wait_connected(10000);
    if (err == ESP_OK) {
        clear_stale_discovery_config();
    }
    return err;
}

esp_err_t mqtt_app_wait_connected(uint32_t timeout_ms)
{
    EventBits_t bits = xEventGroupWaitBits(
        s_mqtt_event_group, MQTT_CONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & MQTT_CONNECTED_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

void mqtt_app_publish_reading(const watermeter_reading_t *reading, bool changed_since_last)
{
    if (s_client == NULL) {
        return;
    }

    time_t now;
    time(&now);
    struct tm tinfo;
    gmtime_r(&now, &tinfo);
    char ts[32];
    strftime(ts, sizeof(ts), "%Y-%m-%dT%H:%M:%SZ", &tinfo);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "ts", ts);
    cJSON_AddBoolToObject(root, "tag_present", reading->tag_present);
    cJSON_AddBoolToObject(root, "uid_match", reading->uid_match);
    cJSON_AddBoolToObject(root, "decode_valid", reading->decode_valid);
    cJSON_AddBoolToObject(root, "changed_since_last", changed_since_last);

    if (reading->decode_valid) {
        cJSON_AddNumberToObject(root, "volume_m3", reading->volume_m3);
        char date[11];
        snprintf(date, sizeof(date), "%04d-%02d-%02d", reading->year, reading->month, reading->day);
        cJSON_AddStringToObject(root, "tag_date", date);
    }

    char uid_str[24];
    snprintf(uid_str, sizeof(uid_str), "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
             reading->uid[0], reading->uid[1], reading->uid[2], reading->uid[3],
             reading->uid[4], reading->uid[5], reading->uid[6], reading->uid[7]);
    cJSON_AddStringToObject(root, "uid", uid_str);

    char *json = cJSON_PrintUnformatted(root);
    if (json) {
        esp_mqtt_client_publish(s_client, STATE_TOPIC, json, 0, 1, true);
        if (changed_since_last) {
            ESP_LOGI(TAG, "Tag value changed: %s", json);
        }
        free(json);
    }
    cJSON_Delete(root);
}
