// Minimal ESP-IDF port of the PN5180 base driver (register/EEPROM/RF
// commands, SPI framing with the BUSY-line handshake), ported from
// wilson-elechouse/PN5180_ELECHOUSE's PN5180.cpp (Arduino) — see
// ../../bringup/PN5180_bringup and ../../docs/wiring.md. Only what
// this project needs is ported (no MIFARE, no LPCD).
#ifndef PN5180_H
#define PN5180_H

#include <stdbool.h>
#include <stdint.h>
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// PN5180 EEPROM addresses used for the version-check bring-up test.
#define PN5180_EEPROM_PRODUCT_VERSION  0x10
#define PN5180_EEPROM_FIRMWARE_VERSION 0x12
#define PN5180_EEPROM_EEPROM_VERSION   0x14

// PN5180 registers used here.
#define PN5180_REG_SYSTEM_CONFIG  0x00
#define PN5180_REG_IRQ_ENABLE     0x01
#define PN5180_REG_IRQ_STATUS     0x02
#define PN5180_REG_IRQ_CLEAR      0x03
#define PN5180_REG_RX_STATUS      0x13
#define PN5180_REG_TX_CONFIG      0x18

// IRQ_STATUS bits used here.
#define PN5180_IRQ_RX_STAT         (1u << 0)
#define PN5180_IRQ_TX_STAT         (1u << 1)
#define PN5180_IRQ_IDLE_STAT       (1u << 2)
#define PN5180_IRQ_RX_SOF_DET_STAT (1u << 14)
#define PN5180_IRQ_TX_RFON_STAT    (1u << 9)
#define PN5180_IRQ_TX_RFOFF_STAT   (1u << 8)

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t pin_nss;
    gpio_num_t pin_busy;
    gpio_num_t pin_rst;
    gpio_num_t pin_ce;   // PD/CE, hard-reboot control. Set to GPIO_NUM_NC if unused.
    uint32_t command_timeout_ms;
} pn5180_t;

// Bring up SPI + GPIOs. Does not reset or power on the chip.
esp_err_t pn5180_init(pn5180_t *dev, spi_host_device_t host,
                       gpio_num_t sck, gpio_num_t miso, gpio_num_t mosi,
                       gpio_num_t nss, gpio_num_t busy, gpio_num_t rst,
                       gpio_num_t ce);

// Pulse PD/CE low then high — physically power-cycles the chip's
// logic core. Recommended before pn5180_reset() for reliability, per
// the module's own manual. No-op if pin_ce is GPIO_NUM_NC.
void pn5180_hard_reboot(pn5180_t *dev);

// Soft reset via the RST line; waits for the IDLE IRQ.
void pn5180_reset(pn5180_t *dev);

bool pn5180_write_register(pn5180_t *dev, uint8_t reg, uint32_t value);
bool pn5180_write_register_or_mask(pn5180_t *dev, uint8_t reg, uint32_t mask);
bool pn5180_write_register_and_mask(pn5180_t *dev, uint8_t reg, uint32_t mask);
bool pn5180_read_register(pn5180_t *dev, uint8_t reg, uint32_t *value);

bool pn5180_read_eeprom(pn5180_t *dev, uint8_t addr, uint8_t *buffer, uint8_t len);

// Writes data to the RF transmission buffer and starts transmission.
bool pn5180_send_data(pn5180_t *dev, const uint8_t *data, uint8_t len, uint8_t valid_bits);

// Reads `len` bytes from the RF reception buffer into buffer.
bool pn5180_read_data(pn5180_t *dev, uint16_t len, uint8_t *buffer);

bool pn5180_load_rf_config(pn5180_t *dev, uint8_t tx_conf, uint8_t rx_conf);
bool pn5180_set_rf_on(pn5180_t *dev);
bool pn5180_set_rf_off(pn5180_t *dev);

uint32_t pn5180_get_irq_status(pn5180_t *dev);
bool pn5180_clear_irq_status(pn5180_t *dev, uint32_t mask);

// Low-level SPI command exchange implementing the NSS/BUSY handshake
// from the PN5180 datasheet (11.4.3). recv_buffer/recv_len may be
// NULL/0 for write-only commands.
bool pn5180_transceive_command(pn5180_t *dev,
                                const uint8_t *send_buffer, size_t send_len,
                                uint8_t *recv_buffer, size_t recv_len);

#ifdef __cplusplus
}
#endif

#endif // PN5180_H
