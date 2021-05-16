#include "TinyBME.h"

#define BME280_REGISTER_CHIPID 0xD0
#define BME280_REGISTER_SOFTRESET 0xE0

TinyBME::TinyBME()
{
}

// initialise the bus
bool TinyBME::begin(uint8_t addr)
{
    bool status = false;
    _i2caddr = addr;
    _wire = &Wire;

    // check if sensor, i.e. the chip ID is correct
    if (read8(BME280_REGISTER_CHIPID) != 0x60)
    {
        return false;
    }

    // Reset the device using soft-reset
    // This makes sure the IIR is off, etc.
    write8(BME280_REGISTER_SOFTRESET, 0xB6);

    // Wait for chip to wake up.
    delay(10);

    // Read trimming parameters, see DS 4.2.2
    readCoefficients();

    write8(BME280_REGISTER_CONTROL, MODE_SLEEP);
    write8(BME280_REGISTER_CONTROLHUMID, SAMPLING_X1);                                        // DS 5.4.3 - Register 0xF2 “ctrl_hum” - Set before CONTROL!
    write8(BME280_REGISTER_CONFIG, ((STANDBY_MS_0_5 << 5) | (FILTER_OFF << 2)));              // DS 5.4.6 - Register 0xF5 “config” (7-5 standby time, 4-2 filter settings, 1-0 unused)
    write8(BME280_REGISTER_CONTROL, ((SAMPLING_X1 << 5) | (SAMPLING_X1 << 2) | MODE_FORCED)); // DS 5.4.5 - Register 0xF4 “ctrl_meas” (7-5 temperature oversampling, 4-2 pressure oversampling, 1-0 device mode)

    delay(100);

    return true;
}

// Writes an 8 bit value
void TinyBME::write8(byte reg, byte value)
{

    _wire->beginTransmission((uint8_t)_i2caddr);
    _wire->write((uint8_t)reg);
    _wire->write((uint8_t)value);
    _wire->endTransmission();
}

// Reads an 8 bit value
uint8_t TinyBME::read8(byte reg)
{
    uint8_t value;

    _wire->beginTransmission((uint8_t)_i2caddr);
    _wire->write((uint8_t)reg);
    _wire->endTransmission();
    _wire->requestFrom((uint8_t)_i2caddr, (byte)1);
    value = _wire->read();
    _wire->endTransmission();

    return value;
}

// Reads an 16 bit value
uint16_t TinyBME::read16(byte reg)
{
    uint16_t value;

    _wire->beginTransmission((uint8_t)_i2caddr);
    _wire->write((uint8_t)reg);
    _wire->endTransmission();
    _wire->requestFrom((uint8_t)_i2caddr, (byte)2);
    value = (Wire.read() << 8) | Wire.read();
    _wire->endTransmission();

    return value;
}

// Reads an 24 bit value
uint32_t TinyBME::read24(byte reg)
{
    uint32_t value;

    _wire->beginTransmission((uint8_t)_i2caddr);
    _wire->write((uint8_t)reg);
    _wire->endTransmission();
    _wire->requestFrom((uint8_t)_i2caddr, (byte)3);
    value = _wire->read();
    value <<= 8;
    value |= _wire->read();
    value <<= 8;
    value |= _wire->read();
    _wire->endTransmission();

    return value;
}

// Reads a signed 16 bit little endian value
uint16_t TinyBME::read16_LE(byte reg)
{
    uint16_t temp = read16(reg);

    return (temp >> 8) | (temp << 8);
};

// Reads a signed 16 bit value
int16_t TinyBME::readS16(byte reg)
{
    return (int16_t)read16(reg);
};

// Reads a signed little endian 16 bit value
int16_t TinyBME::readS16_LE(byte reg)
{
    return (int16_t)read16_LE(reg);
};

// Reads the factory-set coefficients
void TinyBME::readCoefficients(void)
{
    _bme280_calib.dig_T1 = read16_LE(BME280_REGISTER_DIG_T1);
    _bme280_calib.dig_T2 = readS16_LE(BME280_REGISTER_DIG_T2);
    _bme280_calib.dig_T3 = readS16_LE(BME280_REGISTER_DIG_T3);

    _bme280_calib.dig_P1 = read16_LE(BME280_REGISTER_DIG_P1);
    _bme280_calib.dig_P2 = readS16_LE(BME280_REGISTER_DIG_P2);
    _bme280_calib.dig_P3 = readS16_LE(BME280_REGISTER_DIG_P3);
    _bme280_calib.dig_P4 = readS16_LE(BME280_REGISTER_DIG_P4);
    _bme280_calib.dig_P5 = readS16_LE(BME280_REGISTER_DIG_P5);
    _bme280_calib.dig_P6 = readS16_LE(BME280_REGISTER_DIG_P6);
    _bme280_calib.dig_P7 = readS16_LE(BME280_REGISTER_DIG_P7);
    _bme280_calib.dig_P8 = readS16_LE(BME280_REGISTER_DIG_P8);
    _bme280_calib.dig_P9 = readS16_LE(BME280_REGISTER_DIG_P9);

    _bme280_calib.dig_H1 = read8(BME280_REGISTER_DIG_H1);
    _bme280_calib.dig_H2 = readS16_LE(BME280_REGISTER_DIG_H2);
    _bme280_calib.dig_H3 = read8(BME280_REGISTER_DIG_H3);
    _bme280_calib.dig_H4 = ((int8_t)read8(BME280_REGISTER_DIG_H4) << 4) |
                           (read8(BME280_REGISTER_DIG_H4 + 1) & 0xF);
    _bme280_calib.dig_H5 = ((int8_t)read8(BME280_REGISTER_DIG_H5 + 1) << 4) |
                           (read8(BME280_REGISTER_DIG_H5) >> 4);
    _bme280_calib.dig_H6 = (int8_t)read8(BME280_REGISTER_DIG_H6);
}

// Take a new measurement (only possible in forced mode)
bool TinyBME::takeForcedMeasurement(void)
{
    bool return_value = false;
    // If we are in forced mode, the BME sensor goes back to sleep after each
    // measurement and we need to set it to forced mode once at this point, so
    // it will take the next measurement and then return to sleep again.
    // In normal mode simply does new measurements periodically.

    return_value = true;
    // set to forced mode, i.e. "take next measurement"
    write8(BME280_REGISTER_CONTROL, ((SAMPLING_X1 << 5) | (SAMPLING_X1 << 2) | MODE_FORCED)); // DS 5.4.5 - Register 0xF4 “ctrl_meas” (7-5 temperature oversampling, 4-2 pressure oversampling, 1-0 device mode)

    // Store current time to measure the timeout
    uint32_t timeout_start = millis();
    // wait until measurement has been completed, otherwise we would read the
    // the values from the last measurement or the timeout occurred after 2 sec.
    while (read8(BME280_REGISTER_STATUS) & 0x08)
    {
        // In case of a timeout, stop the while loop
        if ((millis() - timeout_start) > 2000)
        {
            return_value = false;
            break;
        }
        delay(1);
    }

    return true;
}

// Returns the temperature from the sensor
float TinyBME::readTemperature(void)
{

    int32_t adc_T = read24(BME280_REGISTER_TEMPDATA);
    if (adc_T == 0x800000) // value in case temp measurement was disabled
        return NAN;
    adc_T >>= 4;

    int32_t var1 = ((((adc_T >> 3) - ((int32_t)_bme280_calib.dig_T1 << 1))) *
                    ((int32_t)_bme280_calib.dig_T2)) >>
                   11;

    int32_t var2 = (((((adc_T >> 4) - ((int32_t)_bme280_calib.dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)_bme280_calib.dig_T1))) >>
                     12) *
                    ((int32_t)_bme280_calib.dig_T3)) >>
                   14;

    t_fine = var1 + var2;

    float T = (t_fine * 5 + 128) >> 8;
    return T / 100;
}

//  Returns the pressure from the sensor
float TinyBME::readPressure(void)
{
    int64_t var1, var2, p;

    readTemperature(); // must be done first to get t_fine

    int32_t adc_P = read24(BME280_REGISTER_PRESSUREDATA);
    if (adc_P == 0x800000) // value in case pressure measurement was disabled
        return NAN;
    adc_P >>= 4;

    var1 = ((int64_t)t_fine) - 128000;
    var2 = var1 * var1 * (int64_t)_bme280_calib.dig_P6;
    var2 = var2 + ((var1 * (int64_t)_bme280_calib.dig_P5) << 17);
    var2 = var2 + (((int64_t)_bme280_calib.dig_P4) << 35);
    var1 = ((var1 * var1 * (int64_t)_bme280_calib.dig_P3) >> 8) +
           ((var1 * (int64_t)_bme280_calib.dig_P2) << 12);
    var1 =
        (((((int64_t)1) << 47) + var1)) * ((int64_t)_bme280_calib.dig_P1) >> 33;

    if (var1 == 0)
    {
        return 0; // avoid exception caused by division by zero
    }
    p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)_bme280_calib.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)_bme280_calib.dig_P8) * p) >> 19;

    p = ((p + var1 + var2) >> 8) + (((int64_t)_bme280_calib.dig_P7) << 4);
    return (float)p / 256;
}

// Returns the humidity from the sensor
float TinyBME::readHumidity(void)
{
    readTemperature(); // must be done first to get t_fine

    int32_t adc_H = read16(BME280_REGISTER_HUMIDDATA);
    if (adc_H == 0x8000) // value in case humidity measurement was disabled
        return NAN;

    int32_t v_x1_u32r;

    v_x1_u32r = (t_fine - ((int32_t)76800));

    v_x1_u32r = (((((adc_H << 14) - (((int32_t)_bme280_calib.dig_H4) << 20) -
                    (((int32_t)_bme280_calib.dig_H5) * v_x1_u32r)) +
                   ((int32_t)16384)) >>
                  15) *
                 (((((((v_x1_u32r * ((int32_t)_bme280_calib.dig_H6)) >> 10) *
                      (((v_x1_u32r * ((int32_t)_bme280_calib.dig_H3)) >> 11) +
                       ((int32_t)32768))) >>
                     10) +
                    ((int32_t)2097152)) *
                       ((int32_t)_bme280_calib.dig_H2) +
                   8192) >>
                  14));

    v_x1_u32r = (v_x1_u32r - (((((v_x1_u32r >> 15) * (v_x1_u32r >> 15)) >> 7) *
                               ((int32_t)_bme280_calib.dig_H1)) >>
                              4));

    v_x1_u32r = (v_x1_u32r < 0) ? 0 : v_x1_u32r;
    v_x1_u32r = (v_x1_u32r > 419430400) ? 419430400 : v_x1_u32r;
    float h = (v_x1_u32r >> 12);
    return h / 1024.0;
}