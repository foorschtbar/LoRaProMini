#ifndef TinyBME_h
#define TinyBME_h

#include <Arduino.h>
#include <Wire.h>

// I2C Adresses
#define BME280_ADDRESS (0x77)           // Primary I2C Address
#define BME280_ADDRESS_ALTERNATE (0x76) // Alternate Address

// Registers
enum
{
    BME280_REGISTER_DIG_T1 = 0x88,
    BME280_REGISTER_DIG_T2 = 0x8A,
    BME280_REGISTER_DIG_T3 = 0x8C,

    BME280_REGISTER_DIG_P1 = 0x8E,
    BME280_REGISTER_DIG_P2 = 0x90,
    BME280_REGISTER_DIG_P3 = 0x92,
    BME280_REGISTER_DIG_P4 = 0x94,
    BME280_REGISTER_DIG_P5 = 0x96,
    BME280_REGISTER_DIG_P6 = 0x98,
    BME280_REGISTER_DIG_P7 = 0x9A,
    BME280_REGISTER_DIG_P8 = 0x9C,
    BME280_REGISTER_DIG_P9 = 0x9E,

    BME280_REGISTER_DIG_H1 = 0xA1,
    BME280_REGISTER_DIG_H2 = 0xE1,
    BME280_REGISTER_DIG_H3 = 0xE3,
    BME280_REGISTER_DIG_H4 = 0xE4,
    BME280_REGISTER_DIG_H5 = 0xE5,
    BME280_REGISTER_DIG_H6 = 0xE7,

    BME280_REGISTER_CHIPID = 0xD0,
    BME280_REGISTER_VERSION = 0xD1,
    BME280_REGISTER_SOFTRESET = 0xE0,

    BME280_REGISTER_CAL26 = 0xE1, // R calibration stored in 0xE1-0xF0

    BME280_REGISTER_CONTROLHUMID = 0xF2,
    BME280_REGISTER_STATUS = 0XF3,
    BME280_REGISTER_CONTROL = 0xF4,
    BME280_REGISTER_CONFIG = 0xF5,
    BME280_REGISTER_PRESSUREDATA = 0xF7,
    BME280_REGISTER_TEMPDATA = 0xFA,
    BME280_REGISTER_HUMIDDATA = 0xFD,
};
// Sensor power modes
enum sensor_mode
{
    MODE_SLEEP = 0b00,
    MODE_FORCED = 0b01,
    MODE_NORMAL = 0b11
};

// Sampling rates
enum sensor_sampling
{
    SAMPLING_NONE = 0b000,
    SAMPLING_X1 = 0b001,
    SAMPLING_X2 = 0b010,
    SAMPLING_X4 = 0b011,
    SAMPLING_X8 = 0b100,
    SAMPLING_X16 = 0b101
};

// Filter values
enum sensor_filter
{
    FILTER_OFF = 0b000,
    FILTER_X2 = 0b001,
    FILTER_X4 = 0b010,
    FILTER_X8 = 0b011,
    FILTER_X16 = 0b100
};

// Standby duration in ms
enum standby_duration
{
    STANDBY_MS_0_5 = 0b000,
    STANDBY_MS_10 = 0b110,
    STANDBY_MS_20 = 0b111,
    STANDBY_MS_62_5 = 0b001,
    STANDBY_MS_125 = 0b010,
    STANDBY_MS_250 = 0b011,
    STANDBY_MS_500 = 0b100,
    STANDBY_MS_1000 = 0b101
};

// Calibration data
typedef struct
{
    uint16_t dig_T1;
    int16_t dig_T2;
    int16_t dig_T3;

    uint16_t dig_P1;
    int16_t dig_P2;
    int16_t dig_P3;
    int16_t dig_P4;
    int16_t dig_P5;
    int16_t dig_P6;
    int16_t dig_P7;
    int16_t dig_P8;
    int16_t dig_P9;

    uint8_t dig_H1;
    int16_t dig_H2;
    uint8_t dig_H3;
    int16_t dig_H4;
    int16_t dig_H5;
    int8_t dig_H6;
} bme280_calib_data;

//Temperature units
enum BME280_temp_t
{
    BME280_C,
    BME280_F
};

class TinyBME
{
public:
    TinyBME();

    // Initialise bus
    bool begin(uint8_t addr = BME280_ADDRESS);

    // Take a new measurement (only possible in forced mode)
    bool takeForcedMeasurement(void);

    //  Returns the temperature from the sensor
    float readTemperature(void);

    //  Returns the pressure from the sensor
    float readPressure(void);

    //  Returns the humidity from the sensor
    float readHumidity(void);

private:
    // Pointer to a TwoWire object
    TwoWire *_wire;

    // Writes an 8 bit value
    void write8(byte reg, byte value);

    // Reads an 8 bit value
    uint8_t read8(byte reg);

    // Reads an 16 bit value
    uint16_t read16(byte reg);

    // Reads an 24 bit value
    uint32_t read24(byte reg);

    // Reads a signed 16 bit little endian value
    uint16_t read16_LE(byte reg);

    // Reads a signed 16 bit value
    int16_t readS16(byte reg);

    // Reads a signed little endian 16 bit value
    int16_t readS16_LE(byte reg);

    // Reads the factory-set coefficients
    void readCoefficients(void);

    // I2C addr for the TwoWire interface
    uint8_t _i2caddr;

    // temperature with high resolution, stored as an attribute
    // as this is used for temperature compensation
    // reading humidity and pressure
    int32_t t_fine;

    // Stores calibration data
    bme280_calib_data _bme280_calib;
};
#endif