/*
 * TEMT6000 ambient light sensor, Wokwi custom chip
 *
 * Models the phototransistor as an analog output that rises from
 * near 0V in darkness toward VCC as the simulated light level (lux)
 * increases, following an exponential saturation curve similar to
 * the response shown in the TEMT6000 datasheet.
 *
 * Pins: VCC, GND, OUT
 * Attribute: lux (0 to 2000, default 500), adjustable from the
 * chip's control panel while the simulation is running.
 */

#include <math.h>
#include <stdlib.h>
#include "wokwi-api.h"

typedef struct
{
  pin_t pin_vcc;
  pin_t pin_out;
  uint32_t attr_lux;
} chip_state_t;

static void chip_timer_callback(void *user_data)
{
  chip_state_t *chip = (chip_state_t *)user_data;

  uint32_t lux = attr_read(chip->attr_lux);
  float vcc = pin_adc_read(chip->pin_vcc);

  /* Tune this constant to match the load resistor on the real
     breakout board you are simulating. A smaller value makes the
     output saturate toward VCC at lower light levels. */
  const float SATURATION_LUX = 500.0f;

  float fraction = 1.0f - expf(-(float)lux / SATURATION_LUX);
  float vout = vcc * fraction;

  pin_dac_write(chip->pin_out, vout);
}

void chip_init(void)
{
  chip_state_t *chip = malloc(sizeof(chip_state_t));

  chip->pin_vcc = pin_init("VCC", ANALOG);
  chip->pin_out = pin_init("OUT", ANALOG);

  chip->attr_lux = attr_init("lux", 500);

  const timer_config_t timer_config = {
      .callback = chip_timer_callback,
      .user_data = chip,
  };
  timer_t timer_id = timer_init(&timer_config);
  timer_start(timer_id, 50000, true);
}