#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <LowPower.h>
#include <OneWire.h>
#include <TinyDallas.h>
#include <TinyBME.h>
#include <EEPROM.h>
#include <CRC32.h>

// ++++++++++++++++++++++++++++++++++++++++
//
// CONSTANTS
//
// ++++++++++++++++++++++++++++++++++++++++

// LoRa module pins
#define LORA_DIO0 8
#define LORA_DIO1 7
#define LORA_DIO2 LMIC_UNUSED_PIN
#define LORA_RESET 9
#define LORA_CS SS

// 1-Wire Bus
#define ONEWIREBUS 6

// External interrupt pins
#define INTERRUPT_PIN0 2
#define INTERRUPT_PIN1 3

// Battery
#define BAT_SENSE_PIN A0 // Analoge Input Pin

// BME I2C Adresses
#define I2C_ADR_BME 0x76

// Voltage calibration interval
#define VOL_DEBUG_INTERVAL 1000 // in ms

// Start address in EEPROM for structure 'cfg'
#define CFG_START 0

// Config size
#define CFG_SIZE 82
#define CFG_SIZE_WITH_CHECKSUM 86

// LORA MAX RANDOM SEND DELAY
#define LORA_MAX_RANDOM_SEND_DELAY 20

// ++++++++++++++++++++++++++++++++++++++++
//
// LOGGING
//
// ++++++++++++++++++++++++++++++++++++++++

#ifdef LOG_DEBUG
#define log_d(s, ...) Serial.print(s, ##__VA_ARGS__);
#define log_d_ln(s, ...) Serial.println(s, ##__VA_ARGS__);
#define logHex_d(b, c) printHex(b, c);
const boolean LOG_DEBUG_ENABLED = true;
#else
#define log_d(s, ...)
#define log_d_ln(s, ...)
#define logHex_d(b, c)
const boolean LOG_DEBUG_ENABLED = false;
#endif

// ++++++++++++++++++++++++++++++++++++++++
//
// LIBS
//
// ++++++++++++++++++++++++++++++++++++++++

OneWire oneWire(ONEWIREBUS);
TinyDallas ds(&oneWire);
TinyBME bme;

// Dallas temp sensor(s)
DeviceAddress dsSensor; // Holds later first sensor found at boot.

// ++++++++++++++++++++++++++++++++++++++++
//
// ENUMS
//
// ++++++++++++++++++++++++++++++++++++++++

enum _activationMethode
{
  OTAA = 1,
  ABP = 2
};

enum _StateByte
{
  STATE_ITR_TRIGGER = 0b0001,
  STATE_ITR0 = 0b0010,
  STATE_ITR1 = 0b0100,
};

// ++++++++++++++++++++++++++++++++++++++++
//
// VARS
//
// ++++++++++++++++++++++++++++++++++++++++

#ifdef CONFIG_MODE
const boolean CONFIG_MODE_ENABLED = true;
#else
const boolean CONFIG_MODE_ENABLED = false;
#endif

// LoRa LMIC
static osjob_t sendjob;
const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RESET,
    .dio = {LORA_DIO0, LORA_DIO1, LORA_DIO2},
};

typedef struct
{
  uint8_t CONFIG_IS_VALID;          // 1 byte
  uint16_t SLEEPTIME;               // 2 byte - (Deep) Sleep time between data acquisition and transmission
  float BAT_SENSE_VPB;              // 4 byte - Volts per Bit. See documentation
  float BAT_MIN_VOLTAGE;            // 4 byte - Minimum voltage for operation, otherwise the node continues to sleep
  uint8_t WAKEUP_BY_INTERRUPT_PINS; // 1 byte - 0 = Disabled, 1 = Enabled
  uint8_t CONFIRMED_DATA_UP;        // 1 byte - 0 = Unconfirmed Data Up, 1 = Confirmed Data Up
  uint8_t ACTIVATION_METHOD;        // 1 byte - 1 = OTAA, 2 = ABP

  // ABP
  u1_t NWKSKEY[16]; // 16 byte - NwkSKey, network session key in big-endian format (aka msb).
  u1_t APPSKEY[16]; // 16 byte - AppSKey, application session key in big-endian format (aka msb).
  u4_t DEVADDR;     //  4 byte - DevAddr, end-device address in big-endian format (aka msb)
  // OTAA
  u1_t APPEUI[8];  //  8 byte - EUIs must be little-endian format, so least-significant-byte (aka lsb)
  u1_t DEVEUI[8];  //  8 byte - EUIs must be in little-endian format, so least-significant-byte (aka lsb)
  u1_t APPKEY[16]; // 16 byte - AppSKey, application session key in big-endian format (aka msb).

} configData_t;
configData_t cfg; // Instance 'cfg' is a global variable with 'configData_t' structure now

volatile boolean wakedFromISR0 = false;
volatile boolean wakedFromISR1 = false;
unsigned long lastPrintTime = 0;
// unsigned long prepareCount = 0;
boolean TXCompleted = false;
boolean foundBME = false; // BME Sensor found. To skip reading if no sensor is attached
boolean foundDS = false;  // DS19x Sensor found. To skip reading if no sensor is attached
byte pinState = 0x0;
boolean doSend = false;

// These callbacks are used in over-the-air activation
void os_getArtEui(u1_t *buf)
{
  memcpy(buf, cfg.APPEUI, 8);
}
void os_getDevEui(u1_t *buf)
{
  memcpy(buf, cfg.DEVEUI, 8);
}
void os_getDevKey(u1_t *buf)
{
  memcpy(buf, cfg.APPKEY, 16);
}

// ++++++++++++++++++++++++++++++++++++++++
//
// MAIN CODE
//
// ++++++++++++++++++++++++++++++++++++++++

void clearSerialBuffer()
{
  while (Serial.available())
  {
    Serial.read();
  }
}

void wakeUp0()
{
  wakedFromISR0 = true;
}

void wakeUp1()
{
  wakedFromISR1 = true;
}

float readBat()
{
  uint16_t value = 0;
  uint8_t numReadings = 5;

  for (uint8_t i = 0; i < numReadings; i++)
  {
    value = value + analogRead(BAT_SENSE_PIN);

    // 1ms pause adds more stability between reads.
    delay(1);
  }

  value = value / numReadings;

  float batteryV = value * cfg.BAT_SENSE_VPB;
  if (CONFIG_MODE_ENABLED)
  {
    Serial.print(F("Analoge voltage: "));
    Serial.print(((1.1 / 1024.0) * value), 2);
    Serial.print(F(" V | Analoge value: "));
    Serial.print(value);
    Serial.print(F(" ("));
    Serial.print(((100.0 / 1023.0) * value), 1);
    Serial.print(F("% of Range) | Battery voltage: "));
    Serial.print(batteryV, 1);
    Serial.print(F(" V ("));
    Serial.print(batteryV, 2);
    Serial.print(F(" V, VPB="));
    Serial.print(cfg.BAT_SENSE_VPB, 10);
    Serial.println(F(")"));
  }

  return batteryV;
}

void printHex(byte buffer[], size_t arraySize)
{
  unsigned c;
  for (size_t i = 0; i < arraySize; i++)
  {
    c = buffer[i];

    if (i != 0)
      Serial.write(32); // Space

    c &= 0xff;
    if (c < 16)
      Serial.write(48); // 0
    Serial.print(c, HEX);
  }
}

void readConfig()
{
  EEPROM.get(CFG_START, cfg);
}

void setConfig()
{
  byte cfgbuffer[CFG_SIZE_WITH_CHECKSUM];
  char buffer[3];
  char *pEnd;
  uint32_t crc_data = 0;
  CRC32 crc;

  for (uint8_t i = 0; i < CFG_SIZE_WITH_CHECKSUM; i++)
  {
    if (!Serial.readBytes(buffer, 2))
    {
      break;
    }

    // added null-terminator for strtol
    buffer[3] = '\0';

    // convert null-terminated char buffer with hex values to int
    cfgbuffer[i] = (byte)strtol(buffer, &pEnd, 16);

    // Add bytes to crc calculation
    if (i < CFG_SIZE)
    {
      // Add hex chars from buffer without null-terminator
      crc.update(buffer, 2);
    }
    else
    {
      // Store checksum from serial input
      // later compare
      crc_data <<= 8;
      crc_data |= cfgbuffer[i];
    }

    // Pad to 8 bit
    // Add zero prefix to serial output
    cfgbuffer[i] &= 0xff;
    if (cfgbuffer[i] < 16)
      Serial.write(48); // 0

    Serial.print(cfgbuffer[i], HEX);
    Serial.print(" ");
  }

  Serial.println();

  Serial.print(F("> CHECKSUM (Serial Input): 0x"));
  Serial.print(crc_data, HEX);
  Serial.println();

  uint32_t crc_calculated = crc.finalize();
  Serial.print(F("> CHECKSUM (Calculated):   0x"));
  Serial.println(crc_calculated, HEX);

  if (crc_data == crc_calculated)
  {

    Serial.println(F("> Checksum correct. Configuration saved!"));

    for (uint8_t i = CFG_START; i < CFG_SIZE; i++)
    {
      EEPROM.write(i, (uint8_t)cfgbuffer[i]);
    }

    readConfig();
  }
  else
  {
    Serial.println(F("> INVALID CHECKSUM! Process aborted! "));
  }
}

void showConfig(bool raw = false)
{
  Serial.print(F("> CONFIG_IS_VALID: "));
  if (cfg.CONFIG_IS_VALID == 1)
  {
    Serial.println(F("yes"));
  }
  else
  {
    Serial.println(F("no"));
  }
  Serial.print(F("> SLEEPTIME: "));
  Serial.println(cfg.SLEEPTIME, DEC);
  Serial.print(F("> BAT_SENSE_VPB: "));
  Serial.println(cfg.BAT_SENSE_VPB, DEC);
  Serial.print(F("> BAT_MIN_VOLTAGE: "));
  Serial.println(cfg.BAT_MIN_VOLTAGE, DEC);
  Serial.print(F("> WAKEUP_BY_INTERRUPT_PINS: "));
  switch (cfg.WAKEUP_BY_INTERRUPT_PINS)
  {
  case 0:
    Serial.println(F("Disabled"));
    break;
  case 1:
    Serial.println(F("Enabled"));
    break;
  default:
    Serial.println(F("Unkown"));
    break;
  }
  Serial.print(F("> CONFIRMED_DATA_UP: "));
  switch (cfg.CONFIRMED_DATA_UP)
  {
  case 0:
    Serial.println(F("Unconfirmed Data Up"));
    break;
  case 1:
    Serial.println(F("Confirmed Data Up"));
    break;
  default:
    Serial.println(F("Unkown"));
    break;
  }
  Serial.print(F("> ACTIVATION_METHOD: "));
  switch (cfg.ACTIVATION_METHOD)
  {
  case 1:
    Serial.println(F("OTAA"));
    break;
  case 2:
    Serial.println(F("ABP"));
    break;
  default:
    Serial.println(F("Unkown"));
    break;
  }
  Serial.print(F("> NWKSKEY (MSB): "));
  printHex(cfg.NWKSKEY, sizeof(cfg.NWKSKEY));
  Serial.print(F("\n> APPSKEY (MSB): "));
  printHex(cfg.APPSKEY, sizeof(cfg.APPSKEY));
  Serial.print(F("\n> DEVADDR (MSB): "));
  Serial.print(cfg.DEVADDR, HEX);
  Serial.print(F("\n> APPEUI (LSB): "));
  printHex(cfg.APPEUI, sizeof(cfg.APPEUI));
  Serial.print(F("\n> DEVEUI (LSB): "));
  printHex(cfg.DEVEUI, sizeof(cfg.DEVEUI));
  Serial.print(F("\n> APPKEY (MSB): "));
  printHex(cfg.APPKEY, sizeof(cfg.APPKEY));
  Serial.println();

  if (raw)
  {
    Serial.print(F("> RAW ("));
    Serial.print((uint32_t)sizeof(cfg), DEC);
    Serial.print(F(" bytes): "));
    byte c;
    for (uint8_t i = CFG_START; i < sizeof(cfg); i++)
    {
      c = EEPROM.read(i);
      c &= 0xff;
      if (c < 16)
        Serial.write(48); // 0
      Serial.print(c, HEX);
      Serial.print(" ");
    }
    Serial.println();
  }
}

void eraseConfig()
{
  for (uint8_t i = CFG_START; i < sizeof(cfg); i++)
  {
    EEPROM.write(i, 0);
  }
}

void serialWait()
{
  while (!Serial.available())
  {
    yield();
    delay(50);
  }
}

void serialMenu()
{
  unsigned long timer = millis();

  Serial.println(F("\n== CONFIG MENU =="));
  Serial.println(F("[1] Show current config"));
  Serial.println(F("[2] Set new config"));
  Serial.println(F("[3] Erase current config"));
  Serial.println(F("[4] Voltage calibration"));
  Serial.print(F("Select: "));
  serialWait();

  if (Serial.available() > 0)
  {
    switch (Serial.read())
    {
    case '1':
      Serial.println(F("\n\n== SHOW CONFIG =="));
      showConfig(true);
      break;

    case '2':
      Serial.println(F("\n\n== SET CONFIG =="));
      Serial.print(F("Past new config: "));
      serialWait();
      if (Serial.available() > 0)
      {
        setConfig();
      }
      break;

    case '3':
      Serial.println(F("\n\n== ERASE CONFIG =="));
      Serial.print(F("Are you sure you want to erase the EEPROM? [y]es or [n]o: "));
      serialWait();
      Serial.println();
      if (Serial.available() > 0)
      {
        if (Serial.read() == 'y')
        {
          eraseConfig();
          Serial.println(F("Successfull!"));
          readConfig();
        }
        else
        {
          Serial.println(F("Aborted!"));
        }
      }
      break;

    case '4':
      Serial.println(F("\n\n== VOLTAGE CALIBRATION =="));
      while (!Serial.available())
      {
        if (millis() - timer > VOL_DEBUG_INTERVAL)
        {
          readBat();
          timer = millis();
        }
      }

      break;
    }
  }
  clearSerialBuffer();
}

void do_send(osjob_t *j)
{
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND)
  {
    // Serial.println(F("OP_TXRXPEND, not sending"));
  }
  else
  {
    // Unsigned 16 bits integer, 0 up to 65,535
    uint16_t bat = 0;

    // Battery
    bat = readBat() * 100;

    // Signed 16 bits integer, -32,768 up to +32,767
    int16_t temp1 = -127 * 100;
    int16_t temp2 = -127 * 100;

    // Unsigned 16 bits integer, 0 up to 65,535
    uint16_t humi1 = 0;
    uint16_t press1 = 0;

    // Read sensor values von BME280
    // and multiply by 100 to effectively keep 2 decimals
    if (foundBME)
    {
      bme.takeForcedMeasurement();
      temp1 = bme.readTemperature() * 100;
      humi1 = bme.readHumidity() * 100;
      press1 = bme.readPressure() / 100.0F; // p [300..1100]
    }

    // Read sensor value form 1-Wire sensor
    // and multiply by 100 to effectively keep 2 decimals
    if (foundDS)
    {
      ds.requestTemperatures(); // Send the command to get temperatures
      temp2 = ds.getTempC(dsSensor) * 100;
    }

    byte buffer[12];
    buffer[0] = pinState;
    buffer[1] = bat >> 8;
    buffer[2] = bat;
    buffer[3] = (VERSION_MAJOR << 4) | (VERSION_MINOR & 0xf);
    buffer[4] = temp1 >> 8;
    buffer[5] = temp1;
    buffer[6] = humi1 >> 8;
    buffer[7] = humi1;
    buffer[8] = press1 >> 8;
    buffer[9] = press1;
    buffer[10] = temp2 >> 8;
    buffer[11] = temp2;

    log_d_ln("Prepare pck");
    // log_d_ln(++prepareCount);
    // log_d(F("> FW: v"));
    // log_d(VERSION_MAJOR);
    // log_d(F("."));
    // log_d(VERSION_MINOR);
    // log_d(F("> Batt: "));
    // log_d_ln(bat);
    // log_d(F("> Pins: "));
    // log_d_ln(buffer[0], BIN);
    // log_d(F("> BME Temp: "));
    // log_d_ln(temp1);
    // log_d(F("> BME Humi: "));
    // log_d_ln(humi1);
    // log_d(F("> BME Pres: "));
    // log_d_ln(press1);
    // log_d(F("> DS18x Temp: "));
    // log_d_ln(temp2);
    log_d(F("> Payload: "));
    logHex_d(buffer, sizeof(buffer));
    log_d_ln();

    // Print first debug messages in loop immediately
    lastPrintTime = 0;

    TXCompleted = false;

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, buffer, sizeof(buffer), cfg.CONFIRMED_DATA_UP);
    log_d_ln(F("Pck queued"));
  }
}

void lmicStartup()
{
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  if (cfg.ACTIVATION_METHOD == ABP)
  {
    // Set static session parameters. Instead of dynamically establishing a session
    // by joining the network, precomputed session parameters are be provided.
    LMIC_setSession(0x13, cfg.DEVADDR, cfg.NWKSKEY, cfg.APPSKEY);

#if defined(CFG_eu868)
    // Set up the channels used by the Things Network, which corresponds
    // to the defaults of most gateways. Without this, only three base
    // channels from the LoRaWAN specification are used, which certainly
    // works, so it is good for debugging, but can overload those
    // frequencies, so be sure to configure the full frequency range of
    // your network here (unless your network autoconfigures them).
    // Setting up channels should happen after LMIC_setSession, as that
    // configures the minimal channel set. The LMIC doesn't let you change
    // the three basic settings, but we show them here.
    LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI); // g-band
    LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
    LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK, DR_FSK), BAND_MILLI);   // g2-band
// TTN defines an additional channel at 869.525Mhz using SF9 for class B
// devices' ping slots. LMIC does not have an easy way to define set this
// frequency and support for class B is spotty and untested, so this
// frequency is not configured here.
#elif defined(CFG_us915) || defined(CFG_au915)
    // NA-US and AU channels 0-71 are configured automatically
    // but only one group of 8 should (a subband) should be active
    // TTN recommends the second sub band, 1 in a zero based count.
    // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
    LMIC_selectSubBand(1);
#elif defined(CFG_as923)
// Set up the channels used in your country. Only two are defined by default,
// and they cannot be changed.  Use BAND_CENTI to indicate 1% duty cycle.
// LMIC_setupChannel(0, 923200000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
// LMIC_setupChannel(1, 923400000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);

// ... extra definitions for channels 2..n here
#elif defined(CFG_kr920)
// Set up the channels used in your country. Three are defined by default,
// and they cannot be changed. Duty cycle doesn't matter, but is conventionally
// BAND_MILLI.
// LMIC_setupChannel(0, 922100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
// LMIC_setupChannel(1, 922300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
// LMIC_setupChannel(2, 922500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

// ... extra definitions for channels 3..n here.
#elif defined(CFG_in866)
// Set up the channels used in your country. Three are defined by default,
// and they cannot be changed. Duty cycle doesn't matter, but is conventionally
// BAND_MILLI.
// LMIC_setupChannel(0, 865062500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
// LMIC_setupChannel(1, 865402500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
// LMIC_setupChannel(2, 865985000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

// ... extra definitions for channels 3..n here.
#else
#error Region not supported
#endif

    // Disable link check validation
    LMIC_setLinkCheckMode(0);
    // Disable ADR
    LMIC_setAdrMode(0);

    // TTN uses SF9 for its RX2 window.
    LMIC.dn2Dr = DR_SF9;

    // Set data rate and transmit power for uplink (note: txpow seems to be ignored by the library)
    LMIC_setDrTxpow(DR_SF7, 14);

    // #else
  }
  else
  {
    LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100); // fix OTAA joining for Arduino Pro Mini (https://github.com/matthijskooijman/arduino-lmic#problems-with-downlink-and-otaa)
  }
  // #endif
}

void do_sleep(uint16_t sleepTime)
{
  boolean breaksleep = false;

  if (LOG_DEBUG_ENABLED)
  {
    Serial.print(F("Sleep "));
    if (sleepTime <= 0)
    {
      Serial.println(F("FOREVER\n"));
    }
    else
    {
      Serial.print(sleepTime);
      Serial.println(F("s\n"));
    }
    Serial.flush();
  }

  // sleep logic using LowPower library
  if (sleepTime <= 0)
  {
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
  }
  else
  {

    // Add LORA_MAX_RANDOM_SEND_DELAY of randomness to avoid overlapping of different
    // nodes with exactly the same sende interval
    sleepTime += (rand() % LORA_MAX_RANDOM_SEND_DELAY);

    // Measurements show that the timer is off by about 12 percent,
    // so the sleep time is shortened by this value.
    sleepTime *= 0.88;

    // sleep logic using LowPower library
    uint16_t delays[] = {8, 4, 2, 1};
    period_t sleeptimes[] = {SLEEP_8S, SLEEP_4S, SLEEP_2S, SLEEP_1S};
    breaksleep = false;

    for (uint8_t i = 0; (i <= 3 && !breaksleep); i++)
    {
      for (uint16_t x = sleepTime; (x >= delays[i] && !breaksleep); x -= delays[i])
      {
        // Serial.print("i: ");
        // Serial.print(i);
        // Serial.print(" TL: ");
        // Serial.print(sleepTime);
        // Serial.print(" S: ");
        // Serial.println(delays[i]);
        // Serial.flush();
        LowPower.powerDown(sleeptimes[i], ADC_OFF, BOD_OFF);
        if (wakedFromISR0 || wakedFromISR1)
        {
          breaksleep = true;
        }
        else
        {
          sleepTime -= delays[i];
        }
      }
    }
  }

  // LMIC does not get that the MCU is sleeping and the
  // duty cycle limitation then provides a delay.
  // Reset duty cycle limitation to fix that.
  LMIC.bands[BAND_MILLI].avail =
      LMIC.bands[BAND_CENTI].avail =
          LMIC.bands[BAND_DECI].avail = os_getTime();

  // ++++++++++++++++++++++++++++++++++++++++++++++++++ //
  // If that still not work, here a some other things to checkout:
  //
  // https://www.thethingsnetwork.org/forum/t/adafruit-feather-32u4-lora-long-transmission-time-after-deep-sleep/11678/11
  // extern volatile unsigned long timer0_overflow_count;
  // cli();
  // timer0_overflow_count += 8 * 64 * clockCyclesPerMicrosecond();
  // sei();
  //
  // https://www.thethingsnetwork.org/forum/t/lmic-sleeping-and-duty-cycle/15471/2
  // https://github.com/matthijskooijman/arduino-lmic/issues/109
  // https://github.com/matthijskooijman/arduino-lmic/issues/121
  //
  // https://www.thethingsnetwork.org/forum/t/modified-lmic-sleep-and-other-parameter/17027/3
  //
  // https://github.com/mcci-catena/arduino-lmic/issues/777
  // LMIC.txend = 0;
  // for (int i = 0; i < MAX_BANDS; i++)
  // {
  //   LMIC.bands[i].avail = 0;
  // }
  // LMIC.bands[BAND_MILLI].avail = 0;
  // LMIC.bands[BAND_CENTI].avail = 0;
  // LMIC.bands[BAND_DECI].avail = 0;
  //
  // https://github.com/matthijskooijman/arduino-lmic/issues/293
  //
  // os_radio(OP_TXDATA);
  // LMIC.opmode = OP_TXDATA;
  // LMIC_setTxData2(1, mydata, sizeof(mydata) - 1, 0);
  //
  // https://www.thethingsnetwork.org/forum/t/feather-m0-with-lmic-takes-more-than-2-minutes-to-transmit/41803/20?page=2
  // https://gist.github.com/HeadBoffin/95eac7764d94ccde83af2503c1e24eb8
  //
  /// We don't just send here, give the os_runloop_once() a chance to run
  // and we keep the State machine pure
  //
  // https://forum.arduino.cc/t/manipulation-of-millis-value/42855/4
  //
  // https://www.thethingsnetwork.org/forum/t/arduino-sx1276-with-lmic-library-on-arduino-ide-sleep-mode-problem/54692
  // void PowerDownUpdateMicros()
  // {
  //   extern volatile unsigned long timer0_overflow_count;
  //   PowerDown();
  //   cli();
  //   // LMIC uses micros() to keep track of the duty cycle, so
  //   // hack timer0_overflow for a rude adjustment:
  //   timer0_overflow_count += 8 * 64 * clockCyclesPerMicrosecond();
  //   sei();
  // }
  // LMIC uses micros() to keep track of the duty cycle, so
  // hack timer0_overflow for a rude adjustment:
  // cli();
  // timer0_overflow_count += 8 * 64 * clockCyclesPerMicrosecond();
  // sei();
}

void onEvent(ev_t ev)
{
  switch (ev)
  {
  case EV_JOINING:
    log_d_ln(F("Joining..."));
    break;
  case EV_JOINED:
    log_d_ln(F("Joined!"));

    if (cfg.ACTIVATION_METHOD == OTAA)
    {
      //   u4_t netid = 0;
      //   devaddr_t devaddr = 0;
      //   u1_t nwkKey[16];
      //   u1_t artKey[16];
      //   LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
      //   log_d(F("> NetID: "));
      //   log_d_ln(netid, DEC);
      //   log_d(F("> DevAddr: ")); // (MSB)
      //   log_d_ln(devaddr, HEX);
      //   log_d(F("> AppSKey: ")); // (MSB)
      //   logHex_d(artKey, sizeof(artKey));
      //   log_d_ln();
      //   log_d(F("> NwkSKey: ")); // (MSB)
      //   logHex_d(nwkKey, sizeof(nwkKey));
      //   log_d_ln();
      log_d_ln();

      // Enable ADR explicit (default is alreay enabled)
      LMIC_setAdrMode(1);
      // enable link check validation
      LMIC_setLinkCheckMode(1);

      // Ok send our first data in 10 ms
      os_setTimedCallback(&sendjob, os_getTime() + ms2osticks(10), do_send);
    }
    break;
  case EV_JOIN_FAILED:
    log_d_ln(F("Join failed"));
    lmicStartup(); // Reset LMIC and retry
    break;
  case EV_REJOIN_FAILED:
    log_d_ln(F("Rejoin failed"));
    lmicStartup(); // Reset LMIC and retry
    break;

  case EV_TXSTART:
    // log_d_ln(F("EV_TXSTART"));
    break;
  case EV_TXCOMPLETE:
    log_d(F("TX done #")); // (includes waiting for RX windows)
    log_d_ln(LMIC.seqnoUp);
    if (LMIC.txrxFlags & TXRX_ACK)
      log_d_ln(F("> Got ack"));
    if (LMIC.txrxFlags & TXRX_NACK)
      log_d_ln(F("> Got NO ack"));

    // if (LMIC.dataLen)
    // {

    //   log_d(F("> Received "));
    //   log_d(LMIC.dataLen);
    //   log_d(F(" bytes of payload: "));
    //   for (int i = 0; i < LMIC.dataLen; i++)
    //   {
    //     if (LMIC.frame[LMIC.dataBeg + i] < 0x10)
    //     {
    //       log_d(F("0"));
    //     }
    //     log_d(LMIC.frame[LMIC.dataBeg + i], HEX);
    //     log_d(" ");
    //   }
    //   log_d_ln();
    // }

    TXCompleted = true;
    break;

  case EV_JOIN_TXCOMPLETE:
    log_d_ln(F("NO JoinAccept"));
    break;

  case EV_TXCANCELED:
    log_d_ln(F("TX canceled!"));
    break;
  case EV_BEACON_FOUND:
  case EV_BEACON_MISSED:
  case EV_BEACON_TRACKED:
  case EV_RFU1:
  case EV_LOST_TSYNC:
  case EV_RESET:
  case EV_RXCOMPLETE:
  case EV_LINK_DEAD:
  case EV_LINK_ALIVE:
  case EV_SCAN_FOUND:

  case EV_RXSTART:
  default:
    log_d(F("Unknown Evt: "));
    log_d_ln((unsigned)ev);
    break;
  }
}

void handleISR()
{
  if (wakedFromISR0 || wakedFromISR1)
  {
    log_d(F("Waked from ItrPin "));
    if (wakedFromISR0)
    {
      log_d_ln(F("0!"));
      pinState |= STATE_ITR0;    // set bit in pinState byte to 1
      pinState &= ~(STATE_ITR1); // set bit in pinState byte to 0
    }

    if (wakedFromISR1)
    {
      Serial.println(F("1!"));
      pinState |= STATE_ITR1;    // set bit in pinState byte to 1
      pinState &= ~(STATE_ITR0); // set bit in pinState byte to 0
    }

    // set STATE_ITR_EVT bit in pinState byte to 1
    // this means this pin was set now
    pinState |= STATE_ITR_TRIGGER;

    // Remove data previously prepared for upstream transmission.
    // If transmit messages are pending, the event EV_TXCOMPLETE will be reported.
    if (!TXCompleted)
    {
      LMIC_clrTxData();
    }

    // send new data;
    doSend = true;

    wakedFromISR0 = false;
    wakedFromISR1 = false;
  }
  else
  {
    // set STATE_ITR_EVT bit in pinState byte to 0
    // this means that the pin states was set previously
    pinState &= ~(STATE_ITR_TRIGGER);
  }
}

void setup()
{
  // use the 1.1 V internal reference
  analogReference(INTERNAL);

  if (LOG_DEBUG_ENABLED)
  {
    while (!Serial)
    {
      ; // wait for Serial to be initialized
    }
    Serial.begin(9600);
    delay(100); // per sample code on RF_95 test
  }

  log_d(F("\n= Starting LoRaProMini v"));
  log_d(VERSION_MAJOR);
  log_d(F("."));
  log_d(VERSION_MINOR);
  log_d_ln(F(" ="));

  readConfig();

  if (CONFIG_MODE_ENABLED)
  {
    Serial.println(F("CONFIG MODE ENABLED!"));
    Serial.println(F("LORA DISABLED!"));
  }
  else
  {
    if (!cfg.CONFIG_IS_VALID)
    {
      log_d_ln(F("INVALID CONFIG!"));
      while (true)
      {
      }
    }
  }

  log_d(F("Srch DS18x..."));

  ds.begin();
  ds.requestTemperatures();

  log_d(ds.getDeviceCount(), DEC);
  log_d_ln(F(" fnd"));

  for (uint8_t i = 0; i < ds.getDeviceCount(); i++)
  {
    if (CONFIG_MODE_ENABLED)
    {
      Serial.print(F("> #"));
      Serial.print(i);
      Serial.print(F(": "));
    }
    DeviceAddress deviceAddress;
    if (ds.getAddress(deviceAddress, i))
    {
      foundDS = true;

      // Save first sensor as measurement sensor for later
      if (i == 0)
      {
        memcpy(dsSensor, deviceAddress, sizeof(deviceAddress) / sizeof(*deviceAddress));
      }

      if (CONFIG_MODE_ENABLED)
      {
        printHex(deviceAddress, sizeof(deviceAddress));
        Serial.print(" --> ");
        uint8_t scratchPad[9];
        ds.readScratchPad(deviceAddress, scratchPad);
        printHex(scratchPad, sizeof(scratchPad));
        Serial.print(" --> ");
        Serial.print(ds.getTempC(deviceAddress));
        Serial.print(" °C");
        Serial.println();
      }
    }
  }

  // BME280 forced mode, 1x temperature / 1x humidity / 1x pressure oversampling, filter off
  log_d(F("Srch BME..."));
  if (bme.begin(I2C_ADR_BME))
  {
    foundBME = true;
    log_d_ln(F("1 fnd"));
    if (CONFIG_MODE_ENABLED)
    {
      bme.takeForcedMeasurement();
      Serial.print(F("> Temperatur: "));
      Serial.print(bme.readTemperature());
      Serial.println(" °C");
      Serial.print(F("> Humidity: "));
      Serial.print(bme.readHumidity());
      Serial.println(" %RH");
      Serial.print(F("> Pressure: "));
      Serial.print(bme.readPressure() / 100.0F);
      Serial.println(" hPa");
    }
  }
  else
  {
    log_d_ln(F("0 fnd"));
  }

  // Allow wake up pin to trigger interrupt on low.
  // https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
  if (cfg.WAKEUP_BY_INTERRUPT_PINS == 1)
  {
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN0), wakeUp0, RISING);
    attachInterrupt(digitalPinToInterrupt(INTERRUPT_PIN1), wakeUp1, RISING);
  }
  wakedFromISR0 = false;
  wakedFromISR1 = false;

  // Start LoRa stuff if not in config mode
  if (!CONFIG_MODE_ENABLED)
  {
    // LMIC init
    os_init();

    // Reset the MAC state. Session and pending data transfers will be discarded.
    lmicStartup();

    Serial.print(F("Join mode "));

    // ABP Mode
    if (cfg.ACTIVATION_METHOD == ABP)
    {
      Serial.println(F("ABP"));
      // Start job in ABP Mode
      do_send(&sendjob);
    }

    // OTAA Mode
    else if (cfg.ACTIVATION_METHOD == OTAA)
    {
      Serial.println(F("OTAA"));
      // Start job (sending automatically starts OTAA too)
      // Join the network, sending will be started after the event "Joined"
      LMIC_startJoining();
    }
  }
}

void loop()
{

  if (CONFIG_MODE_ENABLED)
  {
    serialMenu();
  }
  else
  {

    os_runloop_once();

    // Previous TX is complete and also no critical jobs pending in LMIC
    if (TXCompleted)
    {
      // Going to sleep
      boolean sleep = true;
      while (sleep)
      {
        do_sleep(cfg.SLEEPTIME);
        if (readBat() >= cfg.BAT_MIN_VOLTAGE)
        {
          sleep = false;
        }
        else
        {
          log_d_ln(F("Bat to low!"));
        }
      }

      handleISR();

      // sleep ended. do next transmission
      doSend = true;
    }
    if (lastPrintTime == 0 || lastPrintTime + 1000 < millis())
    {
      log_d_ln(F("> Cant sleep"));
      lastPrintTime = millis();
    }

    handleISR();

    if (doSend)
    {
      doSend = false;
      do_send(&sendjob);
    }
  }
}
