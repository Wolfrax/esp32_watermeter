// PN5180_bringup.ino
//
// Throwaway hardware bring-up test for the PN5180 <-> ESP32-C6-DevKitC-1
// wiring in ../../docs/wiring.md. Not the production firmware (that will
// be plain ESP-IDF, see docs/wiring.md's "Open decision" section) — this
// is just to prove the SPI link works and the reader can see the actual
// water meter's tag before investing in that.
//
// Library: "PN5180 Library" by Andreas Trappmann (Arduino Library
// Manager: search "PN5180"), or the wilson-elechouse/PN5180_ELECHOUSE
// fork from the module's own manual — same API either way.
// Board: "ESP32C6 Dev Module" (arduino-esp32 core with C6 support).
//
// What it does:
//   1. Hard-reboots the PN5180 via PD/CE, then reads back product/
//      firmware/EEPROM version over SPI. A sane (non-0xFF) product
//      version proves SPI + BUSY wiring is correct, independent of any
//      tag being present.
//   2. Polls for an ISO15693 tag and, when found, compares its UID
//      against the meter's already-known tag (E0:02:24:69:31:38:49:E3,
//      see ../../docs/findings.md) to prove the antenna sees the real
//      target, not just some other nearby tag.
//   3. Dumps every block it can read — a chance to finally capture the
//      full 512 bytes (past dumps via phone only got ~396B, see
//      ../../docs/findings.md "Not yet decoded").

#include <PN5180.h>
#include <PN5180ISO15693.h>

// Pins per docs/wiring.md
#define PN5180_NSS  1
#define PN5180_SCK  2
#define PN5180_MISO 3
#define PN5180_MOSI 6
#define PN5180_BUSY 7
#define PN5180_RST  0
#define PN5180_CE   10  // PD/CE, optional hard-reboot control

PN5180ISO15693 nfc(PN5180_NSS, PN5180_BUSY, PN5180_RST);

// Known UID of the water meter's ST25DV04K-I tag, in the display order
// phones show it (MSB first) — see docs/findings.md.
const uint8_t METER_UID[8] = {0xE0, 0x02, 0x24, 0x69, 0x31, 0x38, 0x49, 0xE3};

void setup() {
  Serial.begin(115200);
  delay(2000); // give the serial monitor time to attach
  Serial.println(F("=================================="));
  Serial.println(F("PN5180 bring-up test"));

  Serial.println(F("Hard-rebooting PN5180 via PD/CE..."));
  pinMode(PN5180_CE, OUTPUT);
  digitalWrite(PN5180_CE, LOW);
  delay(50);
  digitalWrite(PN5180_CE, HIGH);
  delay(50);

  nfc.begin(PN5180_SCK, PN5180_MISO, PN5180_MOSI, PN5180_NSS);
  nfc.reset();

  uint8_t productVersion[2];
  nfc.readEEprom(PRODUCT_VERSION, productVersion, sizeof(productVersion));
  Serial.print(F("Product version: "));
  Serial.print(productVersion[1]);
  Serial.print(".");
  Serial.println(productVersion[0]);

  if (0xff == productVersion[1]) {
    Serial.println(F("FAILED: product version reads 0xFF — check SPI/BUSY wiring."));
    Serial.println(F("Halting. Reset the board to retry."));
    Serial.flush();
    while (true) delay(1000);
  }

  uint8_t firmwareVersion[2];
  nfc.readEEprom(FIRMWARE_VERSION, firmwareVersion, sizeof(firmwareVersion));
  Serial.print(F("Firmware version: "));
  Serial.print(firmwareVersion[1]);
  Serial.print(".");
  Serial.println(firmwareVersion[0]);

  uint8_t eepromVersion[2];
  nfc.readEEprom(EEPROM_VERSION, eepromVersion, sizeof(eepromVersion));
  Serial.print(F("EEPROM version: "));
  Serial.print(eepromVersion[1]);
  Serial.print(".");
  Serial.println(eepromVersion[0]);

  Serial.println(F("SPI link OK. Enabling RF field..."));
  nfc.setupRF();

  Serial.println(F("Hold the antenna near the meter's NFC area..."));
}

void loop() {
  Serial.println(F("----------------------------------"));

  uint8_t uid[8];
  ISO15693ErrorCode rc = nfc.getInventory(uid);
  if (rc != ISO15693_EC_OK) {
    Serial.print(F("No tag: "));
    Serial.println(nfc.strerror(rc));
    nfc.reset();
    nfc.setupRF();
    delay(1000);
    return;
  }

  // getInventory returns the UID LSB-first; reverse it to the usual
  // MSB-first display order to compare against METER_UID.
  uint8_t displayUid[8];
  bool isMeter = true;
  Serial.print(F("Tag found, UID="));
  for (int i = 0; i < 8; i++) {
    displayUid[i] = uid[7 - i];
    if (displayUid[i] < 0x10) Serial.print("0");
    Serial.print(displayUid[i], HEX);
    if (i < 7) Serial.print(":");
    if (displayUid[i] != METER_UID[i]) isMeter = false;
  }
  Serial.println();

  if (isMeter) {
    Serial.println(F(">>> MATCH: this is the water meter's tag <<<"));
  } else {
    Serial.println(F("(different tag — not the water meter)"));
  }

  uint8_t blockSize, numBlocks;
  rc = nfc.getSystemInfo(uid, &blockSize, &numBlocks);
  if (rc != ISO15693_EC_OK) {
    Serial.print(F("getSystemInfo failed: "));
    Serial.println(nfc.strerror(rc));
    delay(2000);
    return;
  }
  Serial.print(F("blockSize="));
  Serial.print(blockSize);
  Serial.print(F(" numBlocks="));
  Serial.println(numBlocks);

  uint8_t block[32]; // blockSize is expected to be 4, oversized for safety
  for (int no = 0; no < numBlocks; no++) {
    rc = nfc.readSingleBlock(uid, no, block, blockSize);
    if (rc != ISO15693_EC_OK) {
      Serial.print(F("Block #"));
      Serial.print(no);
      Serial.print(F(" read failed: "));
      Serial.println(nfc.strerror(rc));
      continue;
    }
    Serial.print(F("Block #"));
    if (no < 10) Serial.print(" ");
    Serial.print(no);
    Serial.print(F(": "));
    for (int i = 0; i < blockSize; i++) {
      if (block[i] < 0x10) Serial.print("0");
      Serial.print(block[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
  }

  delay(3000);
}
