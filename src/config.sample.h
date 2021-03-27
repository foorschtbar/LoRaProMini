#ifndef config_h
#define config_h

#include <Arduino.h>
#include <lmic.h>

// Enable debug prints to serial monitor
#define DEBUG

// LoRa module pins
#define LORA_DIO0 2
#define LORA_DIO1 7
#define LORA_DIO2 8
#define LORA_RESET 9
#define LORA_CS SS

// BME I2C Adresses
#define I2C_ADR_BME 0x76

// (Deep) Sleep time between data acquisition and transmission
#define SLEEPTIME 60

// Activation Method: ABP mode. Comment out line or change to OTAA for OTAA mode
#define ABP

// ABP/OTAA deive settings
#ifdef ABP
// ***********************
// ABP device settings
// ***********************

// LoRaWAN NwkSKey, network session key in big-endian format (aka msb).
static const PROGMEM u1_t NWKSKEY[16] = {}; // CHANGE ME!

// LoRaWAN AppSKey, application session key in big-endian format (aka msb).
static const u1_t PROGMEM APPSKEY[16] = {}; // CHANGE ME!

// LoRaWAN end-device address (DevAddr) in big-endian format (aka msb)
// See http://thethingsnetwork.org/wiki/AddressSpace
static const u4_t DEVADDR = 0x1234567; // CHANGE ME!

#else

// ***********************
// OTAA device settings
// ***********************

// The EUIs must be in little-endian format, so least-significant-byte (aka lsb)
static const u1_t PROGMEM APPEUI[8] = {}; // CHANGE ME!

static const u1_t PROGMEM DEVEUI[8] = {}; // CHANGE ME!

// This key should be in big-endian format (aka msb)
static const u1_t PROGMEM APPKEY[16] = {}; // CHANGE ME!

#endif

#endif