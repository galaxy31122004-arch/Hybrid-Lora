#pragma once

#include <Arduino.h>
#include <SPI.h>

// LoRa PHY settings shared by every radio. All are written explicitly in
// begin() so the link never depends on SX1278 reset defaults.
namespace lora_phy {
// v0.4.0 is tuned for maximum range: SF12 at 125 kHz (sensitivity about
// -137 dBm vs -123 dBm at SF7). Symbol time is 32.8 ms, so Low Data Rate
// Optimize must be on.
constexpr uint8_t kSpreadingFactor = 12;  // SF12
constexpr uint8_t kBandwidthCode = 0x07;  // 125 kHz (RegModemConfig1 Bw)
constexpr uint8_t kCodingRateCode = 0x01; // 4/5 (RegModemConfig1 CodingRate)
constexpr bool kLowDataRateOptimize = true;
constexpr uint16_t kPreambleSymbols = 8;
constexpr uint8_t kSyncWord = 0x12;       // private LoRa network
// RA-02 routes only PA_BOOST to the antenna. 2..17 dBm, or 20 dBm with the
// high-power PA DAC. 20 dBm (100 mW) is above the ~10 mW usually allowed in
// the 433.05-434.79 MHz band: the user chose it and owns the legal check.
constexpr int8_t kTxPowerDbm = 20;
}  // namespace lora_phy

class Sx1278Radio {
 public:
  Sx1278Radio(SPIClass& spi, int cs, int reset, int dio0)
      : spi_(spi), cs_(cs), reset_(reset), dio0_(dio0) {}

  // rx_hz: channel this radio listens on; tx_hz: channel it transmits on.
  bool begin(long rx_hz, long tx_hz, bool reset = true) {
    rx_hz_ = rx_hz;
    tx_hz_ = tx_hz;
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    pinMode(reset_, OUTPUT);
    digitalWrite(reset_, HIGH);
    pinMode(dio0_, INPUT);
    if (reset) reset_chip();
    if (read_reg(kRegVersion) != 0x12) return false;  // SX1276/77/78 silicon

    write_reg(kRegOpMode, kModeLoRa | kModeSleep);  // LoRa bit only changes in sleep
    write_reg(kRegOpMode, kModeLoRa | kModeStandby);
    set_frequency(rx_hz_);
    write_reg(0x0E, 0x80);  // FIFO TX base
    write_reg(0x0F, 0x00);  // FIFO RX base
    write_reg(0x1D, (lora_phy::kBandwidthCode << 4) |
                        (lora_phy::kCodingRateCode << 1));  // explicit header
    write_reg(0x1E, (lora_phy::kSpreadingFactor << 4) | 0x04);  // RX CRC on
    write_reg(0x26, (lora_phy::kLowDataRateOptimize ? 0x08 : 0x00) | 0x04);  // LDRO, AGC on
    write_reg(0x20, static_cast<uint8_t>(lora_phy::kPreambleSymbols >> 8));
    write_reg(0x21, static_cast<uint8_t>(lora_phy::kPreambleSymbols));
    write_reg(0x39, lora_phy::kSyncWord);
    set_tx_power(lora_phy::kTxPowerDbm);
    write_reg(0x0C, 0x20);  // LNA max gain (LF port: HF boost bits stay off)
    write_reg(kRegIrqFlags, 0xFF);
    write_reg(0x11, 0x00);  // unmask IRQs
    start_receive();
    return true;
  }

  bool send(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > 255 || transmitting_) return false;
    write_reg(kRegOpMode, kModeLoRa | kModeStandby);  // stop RX before using the FIFO
    set_frequency(tx_hz_);
    write_reg(0x0D, 0x80);
    for (size_t i = 0; i < size; ++i) write_reg(0x00, data[i]);
    write_reg(0x22, static_cast<uint8_t>(size));
    write_reg(kRegIrqFlags, 0xFF);
    write_reg(kRegOpMode, kModeLoRa | kModeTx);
    transmitting_ = true;
    return true;
  }

  bool packet_available() {
    if (transmitting_) {
      if ((read_reg(kRegIrqFlags) & kIrqTxDone) == 0) return false;
      write_reg(kRegIrqFlags, kIrqTxDone);
      start_receive();
      return false;
    }
    return (read_reg(kRegIrqFlags) & kIrqRxDone) != 0;
  }

  size_t receive(uint8_t* out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    const uint8_t irq = read_reg(kRegIrqFlags);
    // Drop packets without RxDone, with a bad CRC, or sent without a CRC.
    if ((irq & kIrqRxDone) == 0 || (irq & kIrqCrcError) != 0 ||
        (read_reg(0x1C) & 0x40) == 0) {  // RegHopChannel CrcOnPayload
      write_reg(kRegIrqFlags, 0xFF);
      return 0;
    }
    const uint8_t length = read_reg(0x13);
    const size_t count = min(capacity, static_cast<size_t>(length));
    write_reg(0x0D, read_reg(0x10));
    for (size_t i = 0; i < count; ++i) out[i] = read_reg(0x00);
    write_reg(kRegIrqFlags, 0xFF);  // RX continuous keeps listening
    return count;
  }

 private:
  static constexpr uint8_t kRegOpMode = 0x01;
  static constexpr uint8_t kRegIrqFlags = 0x12;
  static constexpr uint8_t kRegVersion = 0x42;
  static constexpr uint8_t kModeLoRa = 0x80;
  static constexpr uint8_t kModeSleep = 0x00;
  static constexpr uint8_t kModeStandby = 0x01;
  static constexpr uint8_t kModeTx = 0x03;
  static constexpr uint8_t kModeRxContinuous = 0x05;
  static constexpr uint8_t kIrqCrcError = 0x20;
  static constexpr uint8_t kIrqRxDone = 0x40;
  static constexpr uint8_t kIrqTxDone = 0x08;

  void reset_chip() {
    digitalWrite(reset_, LOW);
    delay(1);
    digitalWrite(reset_, HIGH);
    delay(5);
  }

  // PA_BOOST output. 2..17 dBm: Pout = 17 - (15 - OutputPower).
  // 20 dBm: high-power PA DAC, Pout = 20 - (15 - OutputPower), OCP raised.
  void set_tx_power(int8_t dbm) {
    if (dbm > 17) {
      if (dbm > 20) dbm = 20;
      write_reg(0x4D, 0x87);                                 // PaDac +20 dBm
      write_reg(0x0B, 0x20 | 0x11);                          // OCP 140 mA
      write_reg(0x09, 0x80 | static_cast<uint8_t>(dbm - 5)); // PA_BOOST
    } else {
      if (dbm < 2) dbm = 2;
      write_reg(0x4D, 0x84);                                 // PaDac default
      write_reg(0x0B, 0x20 | 0x0B);                          // OCP 100 mA
      write_reg(0x09, 0x80 | static_cast<uint8_t>(dbm - 2)); // PA_BOOST
    }
  }

  // Must be called in sleep or standby.
  void set_frequency(long hz) {
    const uint32_t frf = (static_cast<uint64_t>(hz) << 19) / 32000000UL;
    write_reg(0x06, static_cast<uint8_t>(frf >> 16));
    write_reg(0x07, static_cast<uint8_t>(frf >> 8));
    write_reg(0x08, static_cast<uint8_t>(frf));
  }

  void start_receive() {
    write_reg(kRegOpMode, kModeLoRa | kModeStandby);
    set_frequency(rx_hz_);
    write_reg(kRegOpMode, kModeLoRa | kModeRxContinuous);
    transmitting_ = false;
  }

  uint8_t read_reg(uint8_t address) const {
    spi_.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    spi_.transfer(address & 0x7F);
    const uint8_t value = spi_.transfer(0x00);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
    return value;
  }

  void write_reg(uint8_t address, uint8_t value) {
    spi_.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
    digitalWrite(cs_, LOW);
    spi_.transfer(address | 0x80);
    spi_.transfer(value);
    digitalWrite(cs_, HIGH);
    spi_.endTransaction();
  }

  SPIClass& spi_;
  int cs_;
  int reset_;
  int dio0_;
  long rx_hz_{0};
  long tx_hz_{0};
  bool transmitting_{false};
};
