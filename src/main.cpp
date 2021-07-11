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
#define LORA_DIO2 2
#define LORA_RESET 9
#define LORA_CS SS

// 1-Wire Bus
#define ONEWIREBUS 6

// Battery
#define BAT_SENSE_PIN A0 // Analoge Input Pin

#define OTAA 1
#define ABP 2

// BME I2C Adresses
#define I2C_ADR_BME 0x76

// Voltage calibration interval
#define VOL_DEBUG_INTERVAL 1000 // in ms

// Start address in EEPROM for structure 'cfg'
#define CFG_START 0

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
// VARS
//
// ++++++++++++++++++++++++++++++++++++++++

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
  uint8_t CONFIG_IS_VALID;   // 1 byte
  uint16_t SLEEPTIME;        // 2 byte - (Deep) Sleep time between data acquisition and transmission
  float BAT_SENSE_VBP;       // 4 byte - Volts per Bit. See documentation
  float BAT_MIN_VOLTAGE;     // 4 byte - Minimum voltage for operation, otherwise the node continues to sleep
  uint8_t ACTIVATION_METHOD; // 1 byte - 1 = OTAA, 2 = ABP
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

  float batteryV = value * cfg.BAT_SENSE_VBP;

#if defined(VERBOSE) || defined(CONFIG)
  Serial.print("Analoge voltage: ");
  Serial.print(((1.1 / 1024.0) * value), 2);
  Serial.print(" V | Analoge value: ");
  Serial.print(value);
  Serial.print(" (");
  Serial.print(((100.0 / 1023.0) * value), 1);
  Serial.print("% of Range) | Battery voltage: ");
  Serial.print(batteryV, 1);
  Serial.print(" V (");
  Serial.print(batteryV, 2);
  Serial.print(" V, VBP=");
  Serial.print(cfg.BAT_SENSE_VBP, 10);
  Serial.println(")");
#endif

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
  byte newcfg[sizeof(cfg) + 4];
  byte buffer[2];
  uint32_t crc_data;
  CRC32 crc;

  for (uint8_t i = 0; i < sizeof(newcfg); i++)
  {
    // if (Serial.available() > 0)
    // {
    Serial.readBytes(buffer, 2);
    newcfg[i] = strtol((char *)buffer, 0, 16);
    if (i < sizeof(newcfg) - 4)
    {
      crc.update(buffer);
    }
    else
    {
      crc_data = crc_data << 8;
      crc_data |= newcfg[i];
    }
    // }
    // else
    // {
    //   newcfg[i] = 0x00;
    // }

    newcfg[i] &= 0xff;
    if (newcfg[i] < 16)
    {
      Serial.write(48); // 0
    }
    Serial.print(newcfg[i], HEX);
    Serial.print(" ");

    // delay(10);
  }

  Serial.println();

  Serial.print("> CHECKSUM (Serial Input): 0x");
  Serial.print(crc_data, HEX);
  Serial.println();

  uint32_t crc_calculated = crc.finalize();
  Serial.print("> CHECKSUM (Calculated):   0x");
  Serial.println(crc_calculated, HEX);

  if (crc_data == crc_calculated)
  {

    Serial.println("> Checksum correct. Configuration saved!");

    for (uint8_t i = CFG_START; i < sizeof(cfg); i++)
    {
      EEPROM.write(i, (uint8_t)newcfg[i]);
    }

    readConfig();

  }
  else
  {
    Serial.println("> INVALID CHECKSUM! Process aborted! ");
  }
}

void showConfig(bool raw = false)
{
  Serial.print("> CONFIG_IS_VALID: ");
  if (cfg.CONFIG_IS_VALID == 1)
  {
    Serial.println("yes");
  }
  else
  {
    Serial.println("no");
  }
  Serial.print("> SLEEPTIME: ");
  Serial.println(cfg.SLEEPTIME, DEC);
  Serial.print("> BAT_SENSE_VBP: ");
  Serial.println(cfg.BAT_SENSE_VBP, DEC);
  Serial.print("> BAT_MIN_VOLTAGE: ");
  Serial.println(cfg.BAT_MIN_VOLTAGE, DEC);
  Serial.print("> ACTIVATION_METHOD: ");
  switch (cfg.ACTIVATION_METHOD)
  {
  case 1:
    Serial.println("OTAA");
    break;
  case 2:
    Serial.println("ABP");
    break;
  default:
    Serial.println("Unkown");
    break;
  }
  Serial.print("> NWKSKEY: ");
  printHex(cfg.NWKSKEY, sizeof(cfg.NWKSKEY));
  Serial.print("\n> APPSKEY: ");
  printHex(cfg.APPSKEY, sizeof(cfg.APPSKEY));
  Serial.print("\n> DEVADDR: 0x");
  Serial.print(cfg.DEVADDR, HEX);
  Serial.print("\n> APPEUI: ");
  printHex(cfg.APPEUI, sizeof(cfg.APPEUI));
  Serial.print("\n> DEVEUI: ");
  printHex(cfg.DEVEUI, sizeof(cfg.DEVEUI));
  Serial.print("\n> APPKEY: ");
  printHex(cfg.APPKEY, sizeof(cfg.APPKEY));
  Serial.println();

  if (raw)
  {
    Serial.print("> RAW (");
    Serial.print((uint32_t)sizeof(cfg), DEC);
    Serial.print(" bytes): ");
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

  Serial.println("\n\n== CONFIG MENU ==");
  Serial.println("[1] Show config");
  Serial.println("[2] Set config");
  Serial.println("[3] Erase config");
  Serial.println("[4] Voltage calibration");
  Serial.print("Select: ");
  serialWait();

  if (Serial.available() > 0)
  {

    switch (Serial.read())
    {
    case '1':
      Serial.println("\n\n== SHOW CONFIG ==");
      showConfig(true);
      break;

    case '2':
      Serial.println("\n\n== SET CONFIG ==");
      Serial.print("Past new config: ");
      serialWait();
      if (Serial.available() > 0)
      {
        setConfig();
      }
      break;

    case '3':
      Serial.println("\n\n== ERASE CONFIG ==");
      Serial.print("Are you sure you want to erase the EEPROM? [y]es or [n]o: ");
      serialWait();
      Serial.println();
      if (Serial.available() > 0)
      {
        if (Serial.read() == 'y')
        {
          eraseConfig();
          Serial.println("Successfull!");
        }
        else
        {
          Serial.println("Aborted!");
        }
      }
      break;

    case '4':
      Serial.println("\n\n== VOLTAGE CALIBRATION ==");
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
#ifdef VERBOSE
    Serial.println(F("OP_TXRXPEND, not sending"));
#endif
  }
  else
  {
    // Signed 16 bits integer, -32767 up to +32767
    int16_t temp1 = -127;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t humi1 = 0;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t press1 = 0;
    // Signed 16 bits integer, -32767 up to +32767
    int16_t temp2 = -127;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t bat = readBat() * 100;

    // BME280
    bme.takeForcedMeasurement();

    // Read sensor values and multiply by 100 to effectively keep 2 decimals
    temp1 = bme.readTemperature() * 100;
    // t = t + 40; => t [-40..+85] => [0..125] => t = t * 10; => t [0..125] => [0..1250]
    humi1 = bme.readHumidity() * 100;
    press1 = bme.readPressure() / 100.0F; // p [300..1100]

    // 1-Wire sensors
    ds.requestTemperatures(); // Send the command to get temperatures

    float tempC = ds.getTempC(dsSensor);
    if (tempC != DEVICE_DISCONNECTED_C)
    {
      temp2 = tempC * 100;
    }

    byte buffer[10];
    buffer[0] = temp1 >> 8;
    buffer[1] = temp1;
    buffer[2] = humi1 >> 8;
    buffer[3] = humi1;
    buffer[4] = press1 >> 8;
    buffer[5] = press1;
    buffer[6] = temp2 >> 8;
    buffer[7] = temp2;
    buffer[8] = bat >> 8;
    buffer[9] = bat;
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);

#ifdef DEBUG
    Serial.println(F("Prepare package for queue"));
    Serial.print(F("> BME Temp:   "));
    Serial.println(temp1);
    Serial.print(F("> BME Humi:   "));
    Serial.println(humi1);
    Serial.print(F("> BME Pres:   "));
    Serial.println(press1);
    Serial.print(F("> DS18x Temp: "));
    Serial.println(temp2);
    Serial.print(F("> Batt:       "));
    Serial.println(bat);
    Serial.print(F("> Payload: "));
    printHex(buffer, sizeof(buffer));
    Serial.println();
#endif

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);
#ifdef DEBUG
    Serial.println(F("LoRa packet queued"));
#endif
  }
}

void lmicStartup()
{
  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  // #ifdef ABP
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

extern volatile unsigned long timer0_millis;
void fixMillis(unsigned long offset)
{
  uint8_t oldSREG = SREG;
  cli();
  timer0_millis += offset;
  SREG = oldSREG;
  sei();
}

void do_sleep(uint16_t sleepTime)
{

  uint16_t sleepTimeLeft = sleepTime;

#ifdef DEBUG
  Serial.print(F("Sleep "));
  Serial.print(sleepTime);
  Serial.println(F("s\n"));
  Serial.flush();
#endif

  // sleep logic using LowPower library
  uint16_t delays[] = {8, 4, 2, 1};
  period_t sleep[] = {SLEEP_8S, SLEEP_4S, SLEEP_2S, SLEEP_1S};

  for (uint8_t i = 0; i <= 3; i++)
  {
    for (uint16_t x = sleepTimeLeft; x >= delays[i]; x -= delays[i])
    {
      // Serial.print("i: ");
      // Serial.print(i);
      // Serial.print(" TL: ");
      // Serial.print(sleepTimeLeft);
      // Serial.print(" S: ");
      // Serial.println(delays[i]);
      // Serial.flush();
      LowPower.powerDown(sleep[i], ADC_OFF, BOD_OFF);
      sleepTimeLeft -= delays[i];
    }
  }

  fixMillis(sleepTime * 1000);
}

void onEvent(ev_t ev)
{
  switch (ev)
  {
    #ifdef DEBUG
  case EV_JOINING:
Serial.println(F("LoRa joining..."));
    break;
#endif
  case EV_JOINED:
#ifdef DEBUG
    Serial.println(F("LoRa joined!"));
#endif
    // #ifndef ABP
    if (cfg.ACTIVATION_METHOD == OTAA)
    {
      {
        u4_t netid = 0;
        devaddr_t devaddr = 0;
        u1_t nwkKey[16];
        u1_t artKey[16];
        LMIC_getSessionKeys(&netid, &devaddr, nwkKey, artKey);
        Serial.print(F("> NetID: "));
        Serial.println(netid, DEC);
        Serial.print(F("> DevAddr: "));
        Serial.println(devaddr, HEX);
        Serial.print(F("> AppSKey: "));
        printHex(artKey, sizeof(artKey));
        Serial.println();
        Serial.print(F("> NwkSKey: "));
        printHex(nwkKey, sizeof(nwkKey));
        Serial.println();
      }

      // Disable link check validation (automatically enabled
      // during join, but not supported by TTN at this time).
      LMIC_setLinkCheckMode(0);

      // Ok send our first data in 10 ms
      os_setTimedCallback(&sendjob, os_getTime() + ms2osticks(10), do_send);
      // #endif
    }
    break;
  case EV_JOIN_FAILED:
#ifdef DEBUG
    Serial.println(F("LoRa join failed"));
#endif
    lmicStartup(); //Reset LMIC and retry
    break;
  case EV_REJOIN_FAILED:
#ifdef DEBUG
    Serial.println(F("EV_REJOIN_FAILED"));
#endif
    lmicStartup(); //Reset LMIC and retry
    break;
  case EV_TXCOMPLETE:
#ifdef DEBUG
    Serial.println(F("LoRa TX complete")); // (includes waiting for RX windows)
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("> Received ack"));
#endif
#ifdef VERBOSE
    if (LMIC.dataLen)
    {

      Serial.print(F("> Received "));
      Serial.print(LMIC.dataLen);
      Serial.print(F(" bytes of payload: "));
      for (int i = 0; i < LMIC.dataLen; i++)
      {
        if (LMIC.frame[LMIC.dataBeg + i] < 0x10)
        {
          Serial.print(F("0"));
        }
        Serial.print(LMIC.frame[LMIC.dataBeg + i], HEX);
        Serial.print(" ");
      }
      Serial.println();
    }
#endif

    // Power down LoRa Module
    LMIC.opmode = OP_SHUTDOWN;

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
#ifdef DEBUG
        Serial.println(F("Battery voltage to low!"));
#endif
      }
    }

    // Power up LoRa Module
    LMIC.opmode = OP_NONE;

    // Schedule next transmission
    os_setCallback(&sendjob, do_send);
    break;
#ifdef DEBUG
  case EV_JOIN_TXCOMPLETE:
    Serial.println(F("LoRa NO JoinAccept"));
    break;
#endif
#ifdef DEBUG
  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
#endif
  }
}

void setup()
{
  // use the 1.1 V internal reference
  analogReference(INTERNAL);

  //pinMode(LED_BUILTIN, OUTPUT);
#ifdef DEBUG
  while (!Serial)
    ; // wait for Serial to be initialized
  Serial.begin(9600);
  delay(100); // per sample code on RF_95 test

  Serial.println(F("\n=== Starting LoRaProMini ==="));
#endif

  readConfig();

#ifdef CONFIG
  Serial.println(F("CONFIG MODE ENABLED!"));
  Serial.println(F("LORA DISABLED!"));
#else
  if (!cfg.CONFIG_IS_VALID)
  {
#ifdef DEBUG
    Serial.println(F("INVALID CONFIG!"));
#endif
    while (true)
    {
    }
  }
#endif

#ifdef DEBUG
  Serial.print("Search DS18x...");
#endif

  ds.begin();
  ds.requestTemperatures();

#ifdef DEBUG
  Serial.print(ds.getDeviceCount(), DEC);
  Serial.println(" found");
#endif

  for (uint8_t i = 0; i < ds.getDeviceCount(); i++)
  {
#ifdef DEBUG
    Serial.print(F("> #"));
    Serial.print(i);
    Serial.print(F(": "));
#endif
    DeviceAddress deviceAddress;
    if (ds.getAddress(deviceAddress, i))
    {
      // Save first sensor as measurement sensor for later
      if (i == 0)
      {
        memcpy(dsSensor, deviceAddress, sizeof(deviceAddress) / sizeof(*deviceAddress));
      }

#ifdef DEBUG
      printHex(deviceAddress, sizeof(deviceAddress));
#endif

#ifdef VERBOSE
      printHex(deviceAddress, sizeof(deviceAddress));

      Serial.print(" --> ");
      uint8_t scratchPad[9];
      ds.readScratchPad(deviceAddress, scratchPad);
      printHex(scratchPad, sizeof(scratchPad));
      Serial.print(" --> ");
      Serial.print(ds.getTempC(deviceAddress));
      Serial.print(" °C");
#endif
      Serial.println();
    }
  }

  // BME280 forced mode, 1x temperature / 1x humidity / 1x pressure oversampling, filter off
#ifdef DEBUG
  Serial.print(F("Search BME280..."));
#endif
  if (!bme.begin(I2C_ADR_BME))
  {
#ifdef DEBUG
    Serial.println(F("not found"));
#endif
  }
  else
  {
#ifdef DEBUG
    Serial.println(F("1 found"));
#endif
  }

#ifndef CONFIG
  // LMIC init
  os_init();

  // Reset the MAC state. Session and pending data transfers will be discarded.
  lmicStartup();
#endif

#ifdef VERBOSE
  Serial.println(F("Setup complete. Begin with LoRa"));
#endif

#ifndef CONFIG

  Serial.print(F("Join via "));

  // ABP Mode
  if (cfg.ACTIVATION_METHOD == ABP)
  {
    Serial.println(F("ABP..."));
    // Start job in ABP Mode
    do_send(&sendjob);
  }

  // OTAA Mode
  else if (cfg.ACTIVATION_METHOD == OTAA)
  {
    Serial.println(F("OTAA..."));
    // Start job (sending automatically starts OTAA too)
    // Join the network, sending will be started after the event "Joined"
    LMIC_startJoining();
  }
  else
  {
    Serial.println(F("?"));
  }

#endif
}

void loop()
{
#ifdef CONFIG

  serialMenu();
  // readBat();
  // delay(1000);
#else
  os_runloop_once();
#endif
}