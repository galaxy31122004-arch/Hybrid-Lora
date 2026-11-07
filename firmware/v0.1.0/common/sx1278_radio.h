#pragma once

#include <Arduino.h>
#include <SPI.h>

class Sx1278Radio {
 public:
  Sx1278Radio(SPIClass& spi, int cs, int reset, int dio0)
      : spi_(spi), cs_(cs), reset_(reset), dio0_(dio0) {}

  bool begin(long frequency_hz, bool reset = true) {
    pinMode(cs_, OUTPUT);
    digitalWrite(cs_, HIGH);
    pinMode(reset_, OUTPUT);
    digitalWrite(reset_, HIGH);
    pinMode(dio0_, INPUT);
    if (reset) reset_chip();
    write_reg(0x01, 0x80);  // sleep + LoRa mode
    const uint32_t frf = (static_cast<uint64_t>(frequency_hz) << 19) / 32000000UL;
    write_reg(0x06, static_cast<uint8_t>(frf >> 16));
    write_reg(0x07, static_cast<uint8_t>(frf >> 8));
    write_reg(0x08, static_cast<uint8_t>(frf));
    write_reg(0x0E, 0x80);  // FIFO TX base
    write_reg(0x0F, 0x00);  // FIFO RX base
    write_reg(0x12, 0xFF);  // clear IRQs
    write_reg(0x11, 0x00);  // unmask IRQs
    write_reg(0x01, 0x86);  // continuous RX + LoRa mode
    return (read_reg(0x42) != 0);
  }

  bool send(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0 || size > 255 || transmitting_) return false;
    write_reg(0x01, 0x85);
    write_reg(0x0D, 0x80);
    for (size_t i = 0; i < size; ++i) write_reg(0x00, data[i]);
    write_reg(0x22, static_cast<uint8_t>(size));
    write_reg(0x12, 0xFF);
    write_reg(0x01, 0x83);  // TX
    transmitting_ = true;
    return true;
  }

  bool packet_available() {
    if (transmitting_) {
      if ((read_reg(0x12) & 0x08) == 0) return false;  // TxDone
      write_reg(0x12, 0x08);
      start_receive();
      return false;
    }
    return (read_reg(0x12) & 0x40) != 0;  // RxDone
  }

  size_t receive(uint8_t* out, size_t capacity) {
    if (out == nullptr || capacity == 0) return 0;
    const uint8_t irq = read_reg(0x12);
    if ((irq & 0x40) == 0 || (irq & 0x20) != 0) {
      write_reg(0x12, 0xFF);
      return 0;
    }
    const uint8_t length = read_reg(0x13);
    const size_t count = min(capacity, static_cast<size_t>(length));
    write_reg(0x0D, read_reg(0x10));
    for (size_t i = 0; i < count; ++i) out[i] = read_reg(0x00);
    write_reg(0x12, 0xFF);
    start_receive();
    return count;
  }

 private:
  void reset_chip() {
    digitalWrite(reset_, LOW);
    delay(1);
    digitalWrite(reset_, HIGH);
    delay(5);
  }

  void start_receive() {
    write_reg(0x01, 0x86);  // continuous RX + LoRa mode
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
  bool transmitting_{false};
};
