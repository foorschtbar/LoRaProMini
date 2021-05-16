#ifndef TinyDallas_h
#define TinyDallas_h

#include <OneWire.h>

// Error Codes
#define DEVICE_DISCONNECTED_C -127
#define DEVICE_DISCONNECTED_F -196.6
#define DEVICE_DISCONNECTED_RAW -7040

typedef uint8_t DeviceAddress[8];

class TinyDallas
{
public:
    TinyDallas(OneWire *);

    // initialise bus
    void begin();

    // returns the number of devices found on the bus
    uint8_t getDeviceCount();

    // returns the number of DS18xxx Family devices on bus
    uint8_t getDS18Count(void);

    // returns true if address is valid
    bool validAddress(const uint8_t *);

    // returns true if address is of the family of sensors the lib supports.
    bool validFamily(const uint8_t *deviceAddress);

    // finds an address at a given index on the bus
    bool getAddress(uint8_t *, uint8_t);

    // returns temperature in degrees C
    float getTempC(const uint8_t *);

    // returns temperature in degrees F
    float getTempF(const uint8_t *);

    // sends command for all devices on the bus to perform a temperature conversion
    void requestTemperatures(void);

    // returns temperature raw value (12 bit integer of 1/128 degrees C)
    int16_t getTemp(const uint8_t *);

    // read device's scratchpad
    bool readScratchPad(const uint8_t *, uint8_t *);

    // // convert from Celsius to Fahrenheit
    // static float toFahrenheit(float);

    // // convert from Fahrenheit to Celsius
    // static float toCelsius(float);

    // convert from raw to Celsius
    static float rawToCelsius(int16_t);

    // // convert from Celsius to raw
    // static int16_t celsiusToRaw(float);

    // convert from raw to Fahrenheit
    static float rawToFahrenheit(int16_t);

private:
    // Take a pointer to one wire instance
    OneWire *_wire;

    // count of DS18xxx Family devices on bus
    uint8_t devices;

    // Returns true if all bytes of scratchPad are '\0'
    bool isAllZeros(const uint8_t *const scratchPad, const size_t length = 9);

    // reads scratchpad and returns the raw temperature
    //int16_t calculateTemperature(const uint8_t *, uint8_t *);

    typedef uint8_t ScratchPad[9];
};
#endif