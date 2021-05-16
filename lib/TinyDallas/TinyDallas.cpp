#include "TinyDallas.h"

// DSROM FIELDS
#define DSROM_FAMILY 0 // the first ROM byte indicates which chip
#define DSROM_CRC 7    // the last ROM byte indicates hold the CRC checksum

// Model IDs
#define DS18S20MODEL 0x10 // or old DS1820
#define DS18B20MODEL 0x28
#define DS1822MODEL 0x22

// OneWire commands
#define STARTCONVO 0x44    // Tells device to take a temperature reading and put it on the scratchpad
#define COPYSCRATCH 0x48   // Copy scratchpad to EEPROM
#define READSCRATCH 0xBE   // Read from scratchpad
#define WRITESCRATCH 0x4E  // Write to scratchpad
#define RECALLSCRATCH 0xB8 // Recall from EEPROM to scratchpad

// Scratchpad locations
#define TEMP_LSB 0
#define TEMP_MSB 1
#define HIGH_ALARM_TEMP 2
#define LOW_ALARM_TEMP 3
#define CONFIGURATION 4
#define INTERNAL_BYTE 5
#define COUNT_REMAIN 6
#define COUNT_PER_C 7
#define SCRATCHPAD_CRC 8

TinyDallas::TinyDallas(OneWire *_oneWire)
{
    _wire = _oneWire;
}

// initialise the bus
void TinyDallas::begin(void)
{

    DeviceAddress deviceAddress;

    _wire->reset_search();
    devices = 0; // Reset the number of devices when we enumerate wire devices

    while (_wire->search(deviceAddress))
    {
        if (validAddress(deviceAddress) && validFamily(deviceAddress))
        {
            devices++;
        }
    }
}

// returns the number of devices found on the bus
uint8_t TinyDallas::getDeviceCount(void)
{
    return devices;
}

// returns true if address is valid
bool TinyDallas::validAddress(const uint8_t *deviceAddress)
{
    return (_wire->crc8(deviceAddress, 7) == deviceAddress[DSROM_CRC]);
}

bool TinyDallas::validFamily(const uint8_t *deviceAddress)
{
    switch (deviceAddress[DSROM_FAMILY])
    {
    case DS18S20MODEL:
    case DS18B20MODEL:
    case DS1822MODEL:
        return true;
    default:
        return false;
    }
}

// finds an address at a given index on the bus
// returns true if the device was found
bool TinyDallas::getAddress(uint8_t *deviceAddress, uint8_t index)
{

    uint8_t depth = 0;

    _wire->reset_search();

    while (depth <= index && _wire->search(deviceAddress))
    {
        if (depth == index && validAddress(deviceAddress))
            return true;
        depth++;
    }

    return false;
}

// returns temperature in degrees C or DEVICE_DISCONNECTED_C if the
// device's scratch pad cannot be read successfully.
// the numeric value of DEVICE_DISCONNECTED_C is defined in
// TinyDallas.h. It is a large negative number outside the
// operating range of the device
float TinyDallas::getTempC(const uint8_t *deviceAddress)
{
    int16_t raw = getTemp(deviceAddress);
    if (raw <= DEVICE_DISCONNECTED_RAW)
        return DEVICE_DISCONNECTED_C;
    // C = RAW/128
    return (float)raw / 16.0;
}

// returns temperature in degrees F or DEVICE_DISCONNECTED_F if the
// device's scratch pad cannot be read successfully.
// the numeric value of DEVICE_DISCONNECTED_F is defined in
// TinyDallas.h. It is a large negative number outside the
// operating range of the device
float TinyDallas::getTempF(const uint8_t *deviceAddress)
{
    float tempC = getTempC(deviceAddress);
    if (tempC == DEVICE_DISCONNECTED_C)
        return DEVICE_DISCONNECTED_F;
    // F = (C*1.8)+32
    return ((tempC * 1.8) + 32.0);
}

// sends command for all devices on the bus to perform a temperature conversion
void TinyDallas::requestTemperatures()
{

    _wire->reset();
    _wire->skip();
    _wire->write(STARTCONVO);

    delay(750); // maybe 750ms is enough, maybe not
}

// // reads scratchpad and returns fixed-point temperature, scaling factor 2^-7
// int16_t TinyDallas::calculateTemperature(const uint8_t *deviceAddress,
//                                          uint8_t *scratchPad)
// {

//     int16_t fpTemperature = (((int16_t)scratchPad[TEMP_MSB]) << 11) | (((int16_t)scratchPad[TEMP_LSB]) << 3);

//     /*
// 	 DS1820 and DS18S20 have a 9-bit temperature register.
// 	 Resolutions greater than 9-bit can be calculated using the scratchPad from
// 	 the temperature, and COUNT REMAIN and COUNT PER °C registers in the
// 	 scratchpad.  The resolution of the calculation depends on the model.
// 	 While the COUNT PER °C register is hard-wired to 16 (10h) in a
// 	 DS18S20, it changes with temperature in DS1820.
// 	 After reading the scratchpad, the TEMP_READ value is obtained by
// 	 truncating the 0.5°C bit (bit 0) from the temperature scratchPad. The
// 	 extended resolution temperature can then be calculated using the
// 	 following equation:
// 	                                  COUNT_PER_C - COUNT_REMAIN
// 	 TEMPERATURE = TEMP_READ - 0.25 + --------------------------
// 	                                         COUNT_PER_C
// 	 Hagai Shatz simplified this to integer arithmetic for a 12 bits
// 	 value for a DS18S20, and James Cameron added legacy DS1820 support.
// 	 See - http://myarduinotoy.blogspot.co.uk/2013/02/12bit-result-from-ds18s20.html
// 	 */

//     if ((deviceAddress[DSROM_FAMILY] == DS18S20MODEL) && (scratchPad[COUNT_PER_C] != 0))
//     {
//         fpTemperature = ((fpTemperature & 0xfff0) << 3) - 32 + (((scratchPad[COUNT_PER_C] - scratchPad[COUNT_REMAIN]) << 7) / scratchPad[COUNT_PER_C]);
//     }

//     return fpTemperature;
// }

bool TinyDallas::readScratchPad(const uint8_t *deviceAddress,
                                uint8_t *scratchPad)
{
    // send the reset command and fail fast
    int b = _wire->reset();
    if (b == 0)
        return false;

    _wire->select(deviceAddress);
    _wire->write(READSCRATCH);

    // Read all registers in a simple loop
    // byte 0: temperature LSB
    // byte 1: temperature MSB
    // byte 2: high alarm temp
    // byte 3: low alarm temp
    // byte 4: DS18S20: store for crc
    //         DS18B20 & DS1822: configuration register
    // byte 5: internal use & crc
    // byte 6: DS18S20: COUNT_REMAIN
    //         DS18B20 & DS1822: store for crc
    // byte 7: DS18S20: COUNT_PER_C
    //         DS18B20 & DS1822: store for crc
    // byte 8: SCRATCHPAD_CRC
    for (uint8_t i = 0; i < 9; i++)
    {
        scratchPad[i] = _wire->read();
    }

    b = _wire->reset();
    return (b == 1);
}

// returns temperature in 1/128 degrees C or DEVICE_DISCONNECTED_RAW if the
// device's scratch pad cannot be read successfully.
// the numeric value of DEVICE_DISCONNECTED_RAW is defined in
// TinyDallas.h. It is a large negative number outside the
// operating range of the device
int16_t TinyDallas::getTemp(const uint8_t *deviceAddress)
{
    ScratchPad scratchPad;
    bool b = readScratchPad(deviceAddress, scratchPad);

    // Check if readScratchPad was successfull
    if (!b || isAllZeros(scratchPad) || (_wire->crc8(scratchPad, 8) != scratchPad[SCRATCHPAD_CRC]))
    {
        return DEVICE_DISCONNECTED_RAW;
    }
    else
    {

        // See https://github.com/PaulStoffregen/OneWire/blob/master/examples/DS18x20_Temperature/DS18x20_Temperature.pde
        // Convert the scratchPad to actual temperature
        // because the result is a 16 bit signed integer, it should
        // be stored to an "int16_t" type, which is always 16 bits
        // even when compiled on a 32 bit processor.
        int16_t raw = (scratchPad[TEMP_MSB] << 8) | scratchPad[TEMP_LSB];
        if (deviceAddress[DSROM_FAMILY] == DS18S20MODEL)
        {
            raw = raw << 3; // 9 bit resolution default
            if (scratchPad[COUNT_PER_C] == 0x10)
            {
                // "count remain" gives full 12 bit resolution
                raw = (raw & 0xFFF0) + 12 - scratchPad[COUNT_REMAIN];
            }
        }
        else
        {
            byte cfg = (scratchPad[CONFIGURATION] & 0x60);
            // at lower res, the low bits are undefined, so let's zero them
            if (cfg == 0x00)      // TEMP_9_BIT
                raw = raw & ~7;   // 9 bit resolution, 93.75 ms
            else if (cfg == 0x20) // TEMP_10_BIT
                raw = raw & ~3;   // 10 bit res, 187.5 ms
            else if (cfg == 0x40) // TEMP_11_BIT
                raw = raw & ~1;   // 11 bit res, 375 ms
                                  //// default is 12 bit resolution, 750 ms conversion time
            // else TEMP_12_BIT
        }

        return raw;
    }
}

// Returns true if all bytes of scratchPad are '\0'
bool TinyDallas::isAllZeros(const uint8_t *const scratchPad, const size_t length)
{
    for (size_t i = 0; i < length; i++)
    {
        if (scratchPad[i] != 0)
        {
            return false;
        }
    }

    return true;
}