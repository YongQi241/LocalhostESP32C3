#include "wokwi-api.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTER_COUNT 0x1200
#define DEFAULT_I2C_ADDRESS 0x29

// Registers used by the Pololu VL53L1X Arduino library.
enum {
  REG_SOFT_RESET = 0x0000,
  REG_I2C_ADDRESS = 0x0001,
  REG_FAST_OSC_FREQUENCY = 0x0006,
  REG_GPIO_STATUS = 0x0031,
  REG_VCSEL_PERIOD_A = 0x0060,
  REG_VCSEL_PERIOD_B = 0x0063,
  REG_RANGE_STATUS = 0x0089,
  REG_OSC_CALIBRATE = 0x00DE,
  REG_FIRMWARE_STATUS = 0x00E5,
  REG_MODEL_ID = 0x010F,
};

typedef struct {
  uint8_t registers[REGISTER_COUNT];
  uint16_t register_pointer;
  uint8_t write_byte_index;
  uint8_t i2c_address;
  uint8_t stream_count;

  uint32_t distance_attr;
  pin_t xshut_pin;
  pin_t gpio1_pin;
} chip_state_t;

static void write_u16_be(chip_state_t *chip, uint16_t reg, uint16_t value) {
  if ((uint32_t)reg + 1 >= REGISTER_COUNT) {
    return;
  }
  chip->registers[reg] = (uint8_t)(value >> 8);
  chip->registers[reg + 1] = (uint8_t)value;
}

static void initialize_registers(chip_state_t *chip) {
  memset(chip->registers, 0, sizeof(chip->registers));

  // Pololu sensor.init() expects 0xEACC at 0x010F.
  chip->registers[REG_MODEL_ID] = 0xEA;
  chip->registers[REG_MODEL_ID + 1] = 0xCC;

  // Firmware boot complete.
  chip->registers[REG_FIRMWARE_STATUS] = 0x01;

  // Nonzero oscillator values prevent divisions by zero in timing calculations.
  write_u16_be(chip, REG_FAST_OSC_FREQUENCY, 0x8000);
  write_u16_be(chip, REG_OSC_CALIBRATE, 0x0100);

  // Plausible long-distance defaults.
  chip->registers[REG_VCSEL_PERIOD_A] = 0x0F;
  chip->registers[REG_VCSEL_PERIOD_B] = 0x0D;

  // Active-low ready signal: bit zero clear means data ready.
  chip->registers[REG_GPIO_STATUS] = 0x00;
}

static void refresh_measurement(chip_state_t *chip) {
  uint32_t requested_mm = attr_read(chip->distance_attr);
  if (requested_mm < 40) requested_mm = 40;
  if (requested_mm > 4000) requested_mm = 4000;

  // The Pololu library applies a 2011/2048 gain correction. Compensate here so
  // the value printed by sensor.read() is approximately the slider value.
  uint32_t raw_mm = (requested_mm * 2048u + 1005u) / 2011u;
  if (raw_mm > 65535u) raw_mm = 65535u;

  chip->stream_count++;
  if (chip->stream_count == 0) chip->stream_count = 1;

  // 17-byte result block starting at 0x0089.
  chip->registers[0x0089] = 9;                   // RANGECOMPLETE
  chip->registers[0x008A] = 0;                   // report status
  chip->registers[0x008B] = chip->stream_count;
  write_u16_be(chip, 0x008C, 0x0100);            // effective SPADs
  write_u16_be(chip, 0x008E, 0x0400);            // peak signal, unused
  write_u16_be(chip, 0x0090, 0x0080);            // ambient rate
  write_u16_be(chip, 0x0092, 0x0010);            // sigma, unused
  write_u16_be(chip, 0x0094, 0x0000);            // phase, unused
  write_u16_be(chip, 0x0096, (uint16_t)raw_mm);  // distance
  write_u16_be(chip, 0x0098, 0x0400);            // corrected peak rate

  chip->registers[REG_GPIO_STATUS] &= (uint8_t)~0x01;
  pin_write(chip->gpio1_pin, LOW);
}

static bool on_i2c_connect(void *user_data, uint32_t address, bool is_read) {
  chip_state_t *chip = (chip_state_t *)user_data;

  // XSHUT low disables the real device. Leave it unconnected or drive it high.
  if (pin_read(chip->xshut_pin) == LOW) {
    return false;
  }

  // address=0 in i2c_config means the callback sees every bus address.
  if (address != chip->i2c_address) {
    return false;
  }

  if (!is_read) {
    chip->write_byte_index = 0;
  }
  return true;
}

static bool on_i2c_write(void *user_data, uint8_t data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  if (chip->write_byte_index == 0) {
    chip->register_pointer = (uint16_t)data << 8;
  } else if (chip->write_byte_index == 1) {
    chip->register_pointer |= data;
  } else {
    uint16_t reg = chip->register_pointer;
    if (reg < REGISTER_COUNT) {
      chip->registers[reg] = data;
    }

    if (reg == REG_I2C_ADDRESS) {
      chip->i2c_address = data & 0x7F;
    }

    // A full silicon reset is intentionally not modeled. Keep the required
    // identity and boot registers alive when the library toggles SOFT_RESET.
    if (reg == REG_SOFT_RESET && data == 0x01) {
      chip->registers[REG_FIRMWARE_STATUS] = 0x01;
      chip->registers[REG_MODEL_ID] = 0xEA;
      chip->registers[REG_MODEL_ID + 1] = 0xCC;
    }

    chip->register_pointer++;
  }

  chip->write_byte_index++;
  return true;
}

static uint8_t on_i2c_read(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;

  // readResults() starts its 17-byte read at this register.
  if (chip->register_pointer == REG_RANGE_STATUS) {
    refresh_measurement(chip);
  }

  uint8_t value = 0;
  if (chip->register_pointer < REGISTER_COUNT) {
    value = chip->registers[chip->register_pointer];
  }
  chip->register_pointer++;
  return value;
}

static void on_i2c_disconnect(void *user_data) {
  chip_state_t *chip = (chip_state_t *)user_data;
  chip->write_byte_index = 0;
}

void chip_init(void) {
  chip_state_t *chip = calloc(1, sizeof(chip_state_t));
  if (!chip) {
    printf("VL53L1X model: allocation failed\n");
    return;
  }

  chip->i2c_address = DEFAULT_I2C_ADDRESS;
  chip->distance_attr = attr_init("distanceMm", 500);
  chip->xshut_pin = pin_init("XSHUT", INPUT_PULLUP);
  chip->gpio1_pin = pin_init("GPIO1", OUTPUT_LOW);
  initialize_registers(chip);

  const i2c_config_t i2c_config = {
    .user_data = chip,
    .address = 0, // Listen to all, then ACK only chip->i2c_address.
    .scl = pin_init("SCL", INPUT_PULLUP),
    .sda = pin_init("SDA", INPUT_PULLUP),
    .connect = on_i2c_connect,
    .read = on_i2c_read,
    .write = on_i2c_write,
    .disconnect = on_i2c_disconnect,
  };
  i2c_init(&i2c_config);

  printf("VL53L1X minimal model ready at I2C address 0x29\n");
}
