/*
  xdrv_15_pca9685.ino - Support for I2C PCA9685 12bit 16 pin hardware PWM driver on Tasmota

  Copyright (C) 2019  Andre Thomas and Theo Arends

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef USE_I2C
#ifdef USE_PCA9685
/*********************************************************************************************\
 * PCA9685 - 16-channel 12-bit pwm driver
 *
 * I2C Address: 0x40 .. 0x47
\*********************************************************************************************/

#define XDRV_15                     15
#define XI2C_01                     1  // See I2CDEVICES.md

#define PCA9685_REG_MODE1           0x00
#define PCA9685_REG_LED0_ON_L       0x06
#define PCA9685_REG_PRE_SCALE       0xFE

#ifndef USE_PCA9685_ADDR
  #define USE_PCA9685_ADDR          0x40
#endif
#ifndef USE_PCA9685_FREQ
  #define USE_PCA9685_FREQ          50
#endif

uint8_t pca9685_detected = 0;
uint16_t pca9685_freq = USE_PCA9685_FREQ;
uint16_t pca9685_pin_pwm_value[16];

void PCA9685_Detect(void)
{
  if (pca9685_detected) { return; }
  if (I2cActive(USE_PCA9685_ADDR)) { return; }

  uint8_t buffer;
  if (I2cValidRead8(&buffer, USE_PCA9685_ADDR, PCA9685_REG_MODE1)) {
    I2cWrite8(USE_PCA9685_ADDR, PCA9685_REG_MODE1, 0x20);
    if (I2cValidRead8(&buffer, USE_PCA9685_ADDR, PCA9685_REG_MODE1)) {
      if (0x20 == buffer) {
        I2cSetActive(USE_PCA9685_ADDR);
        pca9685_detected = 1;
        AddLog_P2(LOG_LEVEL_INFO, S_LOG_I2C_FOUND_AT, "PCA9685", USE_PCA9685_ADDR);
        PCA9685_Reset(); // Reset the controller
      }
    }
  }
}

void PCA9685_Reset(void)
{
  I2cWrite8(USE_PCA9685_ADDR, PCA9685_REG_MODE1, 0x80);
  PCA9685_SetPWMfreq(USE_PCA9685_FREQ);
  for (uint32_t pin=0;pin<16;pin++) {
    PCA9685_SetPWM(pin,0,false);
    pca9685_pin_pwm_value[pin] = 0;
  }
  Response_P(PSTR("{\"PCA9685\":{\"RESET\":\"OK\"}}"));
}

void PCA9685_SetPWMfreq(double freq) {
/*
 7.3.5 from datasheet
 prescale value = round(25000000/(4096*freq))-1;
 */
  if (freq > 23 && freq < 1527) {
   pca9685_freq=freq;
  } else {
   pca9685_freq=50;
  }
  uint8_t pre_scale_osc = round(25000000/(4096*pca9685_freq))-1;
  if (1526 == pca9685_freq) pre_scale_osc=0xFF; // force setting for 24hz because rounding causes 1526 to be 254
  uint8_t current_mode1 = I2cRead8(USE_PCA9685_ADDR, PCA9685_REG_MODE1); // read current value of MODE1 register
  uint8_t sleep_mode1 = (current_mode1&0x7F) | 0x10; // Determine register value to put PCA to sleep
  I2cWrite8(USE_PCA9685_ADDR, PCA9685_REG_MODE1, sleep_mode1); // Let's sleep a little
  I2cWrite8(USE_PCA9685_ADDR, PCA9685_REG_PRE_SCALE, pre_scale_osc); // Set the pre-scaler
  I2cWrite8(USE_PCA9685_ADDR, PCA9685_REG_MODE1, current_mode1 | 0xA0); // Reset MODE1 register to original state and enable auto increment
}

void PCA9685_SetPWM_Reg(uint8_t pin, uint16_t on, uint16_t off) {
  uint8_t led_reg = PCA9685_REG_LED0_ON_L + 4 * pin;
  uint32_t led_data = 0;
  I2cWrite8(USE_PCA9685_ADDR, led_reg, on);
  I2cWrite8(USE_PCA9685_ADDR, led_reg+1, (on >> 8));
  I2cWrite8(USE_PCA9685_ADDR, led_reg+2, off);
  I2cWrite8(USE_PCA9685_ADDR, led_reg+3, (off >> 8));
}

void PCA9685_SetPWM(uint8_t pin, uint16_t pwm, bool inverted) {
  if (4096 == pwm) {
    PCA9685_SetPWM_Reg(pin, 4096, 0); // Special use additional bit causes channel to turn on completely without PWM
  } else {
    PCA9685_SetPWM_Reg(pin, 0, pwm);
  }
  pca9685_pin_pwm_value[pin] = pwm;
}

bool PCA9685_Command(void)
{
  bool serviced = true;
  bool validpin = false;
  uint8_t paramcount = 0;
  if (XdrvMailbox.data_len > 0) {
    paramcount=1;
  } else {
    serviced = false;
    return serviced;
  }
  char sub_string[XdrvMailbox.data_len];
  for (uint32_t ca=0;ca<XdrvMailbox.data_len;ca++) {
    if ((' ' == XdrvMailbox.data[ca]) || ('=' == XdrvMailbox.data[ca])) { XdrvMailbox.data[ca] = ','; }
    if (',' == XdrvMailbox.data[ca]) { paramcount++; }
  }
  UpperCase(XdrvMailbox.data,XdrvMailbox.data);

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"RESET"))  {  PCA9685_Reset(); return serviced; }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"STATUS"))  { PCA9685_OutputTelemetry(false); return serviced; }

  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"PWMF")) {
    if (paramcount > 1) {
      uint16_t new_freq = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if ((new_freq >= 24) && (new_freq <= 1526)) {
        PCA9685_SetPWMfreq(new_freq);
        Response_P(PSTR("{\"PCA9685\":{\"PWMF\":%i, \"Result\":\"OK\"}}"),new_freq);
        return serviced;
      }
    } else { // No parameter was given for setfreq, so we return current setting
      Response_P(PSTR("{\"PCA9685\":{\"PWMF\":%i}}"),pca9685_freq);
      return serviced;
    }
  }
  if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 1),"PWM")) {
    if (paramcount > 1) {
      uint8_t pin = atoi(subStr(sub_string, XdrvMailbox.data, ",", 2));
      if (paramcount > 2) {
        if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 3), "ON")) {
          PCA9685_SetPWM(pin, 4096, false);
          Response_P(PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,4096);
          serviced = true;
          return serviced;
        }
        if (!strcmp(subStr(sub_string, XdrvMailbox.data, ",", 3), "OFF")) {
          PCA9685_SetPWM(pin, 0, false);
          Response_P(PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,0);
          serviced = true;
          return serviced;
        }
        uint16_t pwm = atoi(subStr(sub_string, XdrvMailbox.data, ",", 3));
        if ((pin >= 0 && pin <= 15) && (pwm >= 0 && pwm <= 4096)) {
          PCA9685_SetPWM(pin, pwm, false);
          Response_P(PSTR("{\"PCA9685\":{\"PIN\":%i,\"PWM\":%i}}"),pin,pwm);
          serviced = true;
          return serviced;
        }
      }
    }
  }
  return serviced;
}

void PCA9685_OutputTelemetry(bool telemetry) {
  if (0 == pca9685_detected) { return; }  // We do not do this if the PCA9685 has not been detected
  ResponseTime_P(PSTR(",\"PCA9685\":{\"PWM_FREQ\":%i,"),pca9685_freq);
  for (uint32_t pin=0;pin<16;pin++) {
    ResponseAppend_P(PSTR("\"PWM%i\":%i,"),pin,pca9685_pin_pwm_value[pin]);
  }
  ResponseAppend_P(PSTR("\"END\":1}}"));
  if (telemetry) {
    MqttPublishPrefixTopic_P(TELE, PSTR(D_RSLT_SENSOR), Settings.flag.mqtt_sensor_retain);  // CMND_SENSORRETAIN
  }
}

bool Xdrv15(uint8_t function)
{
  if (!I2cEnabled(XI2C_01)) { return false; }

  bool result = false;

  switch (function) {
    case FUNC_EVERY_SECOND:
      PCA9685_Detect();
      if (tele_period == 0) {
        PCA9685_OutputTelemetry(true);
      }
      break;
    case FUNC_COMMAND_DRIVER:
      if (XDRV_15 == XdrvMailbox.index) {
        result = PCA9685_Command();
      }
      break;
  }
  return result;
}

#endif // USE_PCA9685
#endif // USE_IC2
