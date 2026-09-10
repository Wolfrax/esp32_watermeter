// See pn5180_iso15693.h. Ported from PN5180ISO15693.cpp.

#include <string.h>

#include "pn5180_iso15693.h"
#include "globals.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static iso15693_error_t issue_command(pn5180_t *dev, const uint8_t *cmd, uint8_t cmd_len,
                                       uint8_t *result_buf, uint8_t result_buf_size, uint16_t *out_len)
{
    pn5180_send_data(dev, cmd, cmd_len, 0);
    vTaskDelay(pdMS_TO_TICKS(10));

    uint32_t irq = pn5180_get_irq_status(dev);
    if (0 == (irq & PN5180_IRQ_RX_SOF_DET_STAT)) {
        return ISO15693_EC_NO_CARD;
    }

    int64_t start = esp_timer_get_time() / 1000;
    while (!(irq & PN5180_IRQ_RX_STAT)) {
        irq = pn5180_get_irq_status(dev);
        vTaskDelay(pdMS_TO_TICKS(1));
        if ((esp_timer_get_time() / 1000) - start > dev->command_timeout_ms) {
            return ISO15693_EC_NO_CARD;
        }
    }

    uint32_t rx_status = 0;
    pn5180_read_register(dev, PN5180_REG_RX_STATUS, &rx_status);
    uint16_t len = rx_status & 0x000001ff;

    if (len == 0 || len > result_buf_size) {
        ESP_LOGW(TAG, "issue_command: unexpected response length %u", len);
        return ISO15693_EC_ERROR;
    }

    if (!pn5180_read_data(dev, len, result_buf)) {
        return ISO15693_EC_ERROR;
    }

    irq = pn5180_get_irq_status(dev);
    if (0 == (irq & PN5180_IRQ_RX_SOF_DET_STAT)) {
        pn5180_clear_irq_status(dev, PN5180_IRQ_TX_STAT | PN5180_IRQ_IDLE_STAT);
        return ISO15693_EC_NO_CARD;
    }

    uint8_t response_flags = result_buf[0];
    if (response_flags & (1 << 0)) { // error flag
        ESP_LOGW(TAG, "issue_command: tag reported error code 0x%02x", result_buf[1]);
        return ISO15693_EC_ERROR;
    }

    pn5180_clear_irq_status(dev, PN5180_IRQ_RX_SOF_DET_STAT | PN5180_IRQ_IDLE_STAT |
                                  PN5180_IRQ_TX_STAT | PN5180_IRQ_RX_STAT);
    *out_len = len;
    return ISO15693_EC_OK;
}

bool pn5180_iso15693_setup_rf(pn5180_t *dev)
{
    if (!pn5180_load_rf_config(dev, 0x0d, 0x8d)) { // ISO15693, per NXP AN12650
        return false;
    }
    if (!pn5180_set_rf_on(dev)) {
        return false;
    }
    pn5180_write_register_and_mask(dev, PN5180_REG_SYSTEM_CONFIG, 0xfffffff8); // Idle/StopCom
    pn5180_write_register_or_mask(dev, PN5180_REG_SYSTEM_CONFIG, 0x00000003);  // Transceive
    return true;
}

iso15693_error_t pn5180_iso15693_get_inventory(pn5180_t *dev, uint8_t uid[8])
{
    //                  Flags, CMD,  maskLen
    uint8_t cmd[3] = { 0x26, 0x01, 0x00 };
    uint8_t buf[16];
    uint16_t len = 0;

    memset(uid, 0, 8);

    iso15693_error_t rc = issue_command(dev, cmd, sizeof(cmd), buf, sizeof(buf), &len);
    if (rc != ISO15693_EC_OK) {
        return rc;
    }
    if (len < 10) { // flags(1) + DSFID(1) + UID(8)
        return ISO15693_EC_ERROR;
    }

    memcpy(uid, &buf[2], 8); // LSB first, as received over the air
    return ISO15693_EC_OK;
}

iso15693_error_t pn5180_iso15693_read_single_block(pn5180_t *dev, const uint8_t uid[8],
                                                     uint8_t block_no, uint8_t *block_data,
                                                     uint8_t block_size)
{
    //             flags, cmd,  uid (LSB first)      blockNo
    uint8_t cmd[11] = { 0x22, 0x20, 0,0,0,0,0,0,0,0, block_no };
    memcpy(&cmd[2], uid, 8);

    uint8_t buf[32];
    if (block_size > sizeof(buf) - 1) {
        return ISO15693_EC_ERROR;
    }
    uint16_t len = 0;

    iso15693_error_t rc = issue_command(dev, cmd, sizeof(cmd), buf, sizeof(buf), &len);
    if (rc != ISO15693_EC_OK) {
        return rc;
    }
    if (len < (uint16_t)(block_size + 1)) {
        return ISO15693_EC_ERROR;
    }

    memcpy(block_data, &buf[1], block_size);
    return ISO15693_EC_OK;
}
