// See pn5180.h. Ported from wilson-elechouse/PN5180_ELECHOUSE's
// PN5180.cpp — comments trimmed, logic kept equivalent. The NSS/BUSY
// handshake is done by hand (SPI device configured with no automatic
// CS) because the protocol needs NSS held across a variable-length
// exchange with BUSY polling in between, which doesn't fit the
// hardware auto-CS model.

#include <string.h>

#include "pn5180.h"
#include "globals.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define CMD_WRITE_REGISTER          0x00
#define CMD_WRITE_REGISTER_OR_MASK  0x01
#define CMD_WRITE_REGISTER_AND_MASK 0x02
#define CMD_READ_REGISTER           0x04
#define CMD_WRITE_EEPROM            0x06
#define CMD_READ_EEPROM             0x07
#define CMD_SEND_DATA               0x09
#define CMD_READ_DATA               0x0A
#define CMD_LOAD_RF_CONFIG          0x11
#define CMD_RF_ON                   0x16
#define CMD_RF_OFF                  0x17

#define DEFAULT_COMMAND_TIMEOUT_MS  500

static bool wait_busy(pn5180_t *dev, int level, uint32_t timeout_ms)
{
    int64_t start = esp_timer_get_time() / 1000;
    while (gpio_get_level(dev->pin_busy) != level) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if ((esp_timer_get_time() / 1000) - start > timeout_ms) {
            return false;
        }
    }
    return true;
}

esp_err_t pn5180_init(pn5180_t *dev, spi_host_device_t host,
                       gpio_num_t sck, gpio_num_t miso, gpio_num_t mosi,
                       gpio_num_t nss, gpio_num_t busy, gpio_num_t rst,
                       gpio_num_t ce)
{
    memset(dev, 0, sizeof(*dev));
    dev->pin_nss = nss;
    dev->pin_busy = busy;
    dev->pin_rst = rst;
    dev->pin_ce = ce;
    dev->command_timeout_ms = DEFAULT_COMMAND_TIMEOUT_MS;

    gpio_config_t nss_cfg = {
        .pin_bit_mask = 1ULL << nss,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&nss_cfg));
    gpio_set_level(nss, 1); // disabled

    gpio_config_t busy_cfg = {
        .pin_bit_mask = 1ULL << busy,
        .mode = GPIO_MODE_INPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_cfg));

    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << rst,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(rst, 1); // no reset

    if (ce != GPIO_NUM_NC) {
        gpio_config_t ce_cfg = {
            .pin_bit_mask = 1ULL << ce,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&ce_cfg));
        gpio_set_level(ce, 1); // enabled
    }

    spi_bus_config_t bus_cfg = {
        .sclk_io_num = sck,
        .mosi_io_num = mosi,
        .miso_io_num = miso,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 512,
    };
    esp_err_t err = spi_bus_initialize(host, &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        return err;
    }

    // No automatic CS (spics_io_num = -1): NSS is bit-banged by hand
    // around the BUSY handshake, see pn5180_transceive_command().
    spi_device_interface_config_t dev_cfg = {
        .clock_speed_hz = 7 * 1000 * 1000, // matches PN5180 library default (7 Mbps, MODE0)
        .mode = 0,
        .spics_io_num = -1,
        .queue_size = 1,
    };
    return spi_bus_add_device(host, &dev_cfg, &dev->spi);
}

void pn5180_hard_reboot(pn5180_t *dev)
{
    if (dev->pin_ce == GPIO_NUM_NC) {
        return;
    }
    gpio_set_level(dev->pin_ce, 0);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(dev->pin_ce, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

void pn5180_reset(pn5180_t *dev)
{
    gpio_set_level(dev->pin_rst, 0); // at least 10us required
    vTaskDelay(pdMS_TO_TICKS(1));
    gpio_set_level(dev->pin_rst, 1); // 2ms to ramp up required
    vTaskDelay(pdMS_TO_TICKS(5));

    int64_t start = esp_timer_get_time() / 1000;
    while (0 == (pn5180_get_irq_status(dev) & PN5180_IRQ_IDLE_STAT)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if ((esp_timer_get_time() / 1000) - start > dev->command_timeout_ms) {
            ESP_LOGW(TAG, "pn5180_reset: timeout waiting for IDLE IRQ, retrying with longer wait");
            gpio_set_level(dev->pin_rst, 0);
            vTaskDelay(pdMS_TO_TICKS(10));
            gpio_set_level(dev->pin_rst, 1);
            vTaskDelay(pdMS_TO_TICKS(50));
            return;
        }
    }
}

bool pn5180_transceive_command(pn5180_t *dev,
                                const uint8_t *send_buffer, size_t send_len,
                                uint8_t *recv_buffer, size_t recv_len)
{
    // 0. wait until busy is low
    if (!wait_busy(dev, 0, dev->command_timeout_ms)) {
        ESP_LOGW(TAG, "transceive: timeout (pre-send busy)");
        return false;
    }

    // 1-2. assert NSS, clock out the command
    gpio_set_level(dev->pin_nss, 0);
    vTaskDelay(pdMS_TO_TICKS(1));

    spi_transaction_t t = {
        .length = send_len * 8,
        .tx_buffer = send_buffer,
    };
    if (spi_device_polling_transmit(dev->spi, &t) != ESP_OK) {
        gpio_set_level(dev->pin_nss, 1);
        return false;
    }

    // 3-5. wait busy high, deassert NSS, wait busy low
    if (!wait_busy(dev, 1, dev->command_timeout_ms)) {
        ESP_LOGW(TAG, "transceive: timeout (send busy-high)");
        gpio_set_level(dev->pin_nss, 1);
        return false;
    }
    gpio_set_level(dev->pin_nss, 1);
    vTaskDelay(pdMS_TO_TICKS(1));
    if (!wait_busy(dev, 0, dev->command_timeout_ms)) {
        ESP_LOGW(TAG, "transceive: timeout (send busy-low)");
        return false;
    }

    if (recv_buffer == NULL || recv_len == 0) {
        return true;
    }

    // Second SPI frame to clock in the response.
    gpio_set_level(dev->pin_nss, 0);
    memset(recv_buffer, 0xFF, recv_len);
    spi_transaction_t rt = {
        .length = recv_len * 8,
        .rx_buffer = recv_buffer,
    };
    if (spi_device_polling_transmit(dev->spi, &rt) != ESP_OK) {
        gpio_set_level(dev->pin_nss, 1);
        return false;
    }

    if (!wait_busy(dev, 1, dev->command_timeout_ms)) {
        ESP_LOGW(TAG, "transceive: timeout (recv busy-high)");
        gpio_set_level(dev->pin_nss, 1);
        return false;
    }
    gpio_set_level(dev->pin_nss, 1);
    if (!wait_busy(dev, 0, dev->command_timeout_ms)) {
        ESP_LOGW(TAG, "transceive: timeout (recv busy-low)");
        return false;
    }

    return true;
}

bool pn5180_write_register(pn5180_t *dev, uint8_t reg, uint32_t value)
{
    uint8_t cmd[6] = { CMD_WRITE_REGISTER, reg };
    memcpy(&cmd[2], &value, 4); // little-endian, matches host byte order on ESP32
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);
}

bool pn5180_write_register_or_mask(pn5180_t *dev, uint8_t reg, uint32_t mask)
{
    uint8_t cmd[6] = { CMD_WRITE_REGISTER_OR_MASK, reg };
    memcpy(&cmd[2], &mask, 4);
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);
}

bool pn5180_write_register_and_mask(pn5180_t *dev, uint8_t reg, uint32_t mask)
{
    uint8_t cmd[6] = { CMD_WRITE_REGISTER_AND_MASK, reg };
    memcpy(&cmd[2], &mask, 4);
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);
}

bool pn5180_read_register(pn5180_t *dev, uint8_t reg, uint32_t *value)
{
    uint8_t cmd[2] = { CMD_READ_REGISTER, reg };
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), (uint8_t *)value, 4);
}

bool pn5180_read_eeprom(pn5180_t *dev, uint8_t addr, uint8_t *buffer, uint8_t len)
{
    if (addr > 254 || (addr + len) > 254) {
        return false;
    }
    uint8_t cmd[3] = { CMD_READ_EEPROM, addr, len };
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), buffer, len);
}

bool pn5180_send_data(pn5180_t *dev, const uint8_t *data, uint8_t len, uint8_t valid_bits)
{
    // len is uint8_t (max 255), always within the PN5180's 260-byte
    // SEND_DATA limit, so no range check needed here.
    uint8_t buffer[262];
    buffer[0] = CMD_SEND_DATA;
    buffer[1] = valid_bits;
    memcpy(&buffer[2], data, len);

    pn5180_write_register_and_mask(dev, PN5180_REG_SYSTEM_CONFIG, 0xfffffff8); // Idle/StopCom
    pn5180_write_register_or_mask(dev, PN5180_REG_SYSTEM_CONFIG, 0x00000003);  // Transceive

    return pn5180_transceive_command(dev, buffer, len + 2, NULL, 0);
}

bool pn5180_read_data(pn5180_t *dev, uint16_t len, uint8_t *buffer)
{
    if (len > 508) {
        return false;
    }
    uint8_t cmd[2] = { CMD_READ_DATA, 0x00 };
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), buffer, len);
}

bool pn5180_load_rf_config(pn5180_t *dev, uint8_t tx_conf, uint8_t rx_conf)
{
    uint8_t cmd[3] = { CMD_LOAD_RF_CONFIG, tx_conf, rx_conf };
    return pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);
}

bool pn5180_set_rf_on(pn5180_t *dev)
{
    uint8_t cmd[2] = { CMD_RF_ON, 0x00 };
    pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);

    int64_t start = esp_timer_get_time() / 1000;
    while (0 == (pn5180_get_irq_status(dev) & PN5180_IRQ_TX_RFON_STAT)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if ((esp_timer_get_time() / 1000) - start > 500) {
            ESP_LOGW(TAG, "set_rf_on: timeout");
            return false;
        }
    }
    pn5180_clear_irq_status(dev, PN5180_IRQ_TX_RFON_STAT);
    return true;
}

bool pn5180_set_rf_off(pn5180_t *dev)
{
    uint8_t cmd[2] = { CMD_RF_OFF, 0x00 };
    pn5180_transceive_command(dev, cmd, sizeof(cmd), NULL, 0);

    int64_t start = esp_timer_get_time() / 1000;
    while (0 == (pn5180_get_irq_status(dev) & PN5180_IRQ_TX_RFOFF_STAT)) {
        vTaskDelay(pdMS_TO_TICKS(1));
        if ((esp_timer_get_time() / 1000) - start > 500) {
            ESP_LOGW(TAG, "set_rf_off: timeout");
            return false;
        }
    }
    pn5180_clear_irq_status(dev, PN5180_IRQ_TX_RFOFF_STAT);
    return true;
}

uint32_t pn5180_get_irq_status(pn5180_t *dev)
{
    uint32_t status = 0;
    pn5180_read_register(dev, PN5180_REG_IRQ_STATUS, &status);
    return status;
}

bool pn5180_clear_irq_status(pn5180_t *dev, uint32_t mask)
{
    return pn5180_write_register(dev, PN5180_REG_IRQ_CLEAR, mask);
}
