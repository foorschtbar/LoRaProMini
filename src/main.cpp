#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <LowPower.h>
#include "config.h"

Adafruit_BME280 bme;

static osjob_t sendjob;

const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RESET,
    .dio = {LORA_DIO0, LORA_DIO1, LORA_DIO2},
};

#ifdef ABP
// These callbacks are only used in over-the-air activation, so they are left empty here
void os_getArtEui(u1_t *buf) {}
void os_getDevEui(u1_t *buf) {}
void os_getDevKey(u1_t *buf) {}
#else
void os_getArtEui(u1_t *buf)
{
  memcpy_P(buf, APPEUI, 8);
}
void os_getDevEui(u1_t *buf)
{
  memcpy_P(buf, DEVEUI, 8);
}
void os_getDevKey(u1_t *buf)
{
  memcpy_P(buf, APPKEY, 16);
}
#endif

float readBat()
{
  uint16_t value = 0;
  int numReadings = 5;

  for (int i = 0; i < numReadings; i++)
  {
    value = value + analogRead(BAT_SENSE_PIN);

    // 1ms pause adds more stability between reads.
    delay(1);
  }

  int sensorValue = value / numReadings;

  float batteryV = sensorValue * BAT_SENSE_VBP;

#ifdef DEBUG
  Serial.print("Battery Voltage: ");
  Serial.print(batteryV);
  Serial.print(" V (");
  Serial.print(sensorValue);
  Serial.println(")");
#endif

  return batteryV;
}

void printHex2(unsigned c)
{
  c &= 0xff;
  if (c < 16)
    Serial.write(48); // 0
  Serial.print(c, HEX);
}

void do_send(osjob_t *j)
{
  // Check if there is not a current TX/RX job running
  if (LMIC.opmode & OP_TXRXPEND)
  {
    Serial.println(F("OP_TXRXPEND, not sending"));
  }
  else
  {
    // Signed 16 bits integer, -32767 up to +32767
    int16_t temp = 0;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t humi = 0;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t press = 0;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t bat = 0;

    bme.takeForcedMeasurement();

    // Read sensor values and multiply by 100 to effectively keep 2 decimals
    temp = bme.readTemperature() * 100;
    // t = t + 40; => t [-40..+85] => [0..125] => t = t * 10; => t [0..125] => [0..1250]
    humi = bme.readHumidity() * 100;
    press = bme.readPressure() / 100.0F; // p [300..1100]
    bat = readBat() * 100;

    byte buffer[10];
    buffer[0] = temp >> 8;
    buffer[1] = temp;
    buffer[2] = humi >> 8;
    buffer[3] = humi;
    buffer[4] = press >> 8;
    buffer[5] = press;
    buffer[6] = bat >> 8;
    buffer[7] = bat;
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);

#ifdef DEBUG
    Serial.print(F("> Temperature: "));
    Serial.println(temp);
    Serial.print(F("> Humidity: "));
    Serial.println(humi);
    Serial.print(F("> Pressure: "));
    Serial.println(press);
    Serial.print(F("> Battery: "));
    Serial.println(bat);
    Serial.print(F("> LoRa packet payload: "));
    for (size_t i = 0; i < sizeof(buffer); i++)
    {
      if (i != 0)
        Serial.write(32); // Space
      printHex2(buffer[i]);
    }
    Serial.println();
#endif

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);

    Serial.print(millis());
    Serial.print(" : ");
    Serial.println(F("Packet queued"));
  }
}

void lmicStartup()
{

  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

#ifdef ABP

  // Set static session parameters. Instead of dynamically establishing a session
  // by joining the network, precomputed session parameters are be provided.
  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];
  memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  LMIC_setSession(0x13, DEVADDR, nwkskey, appskey);

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

#else
  LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100); // fix OTAA joining for Arduino Pro Mini (https://github.com/matthijskooijman/arduino-lmic#problems-with-downlink-and-otaa)

#endif
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
  Serial.print(millis());
  Serial.print(" : ");
  Serial.print(F("Sleep for "));
  Serial.print(sleepTime);
  Serial.println(F(" seconds"));
  Serial.flush();
#endif

  // Power down LoRa Module
  LMIC.opmode = OP_SHUTDOWN;

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

  LMIC.opmode = OP_NONE;
}

void onEvent(ev_t ev)
{
  Serial.print(millis());
  Serial.print(" : ");
  switch (ev)
  {
    break;
  case EV_JOINING:
#ifdef DEBUG
    Serial.println(F("EV_JOINING"));
#endif
    break;
  case EV_JOINED:
#ifdef DEBUG
    Serial.println(F("EV_JOINED"));
#endif
#ifndef ABP
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
      for (size_t i = 0; i < sizeof(artKey); ++i)
      {
        if (i != 0)
          Serial.write(32); // Space
        printHex2(artKey[i]);
      }
      Serial.println("");
      Serial.print(F("> NwkSKey: "));
      for (size_t i = 0; i < sizeof(nwkKey); ++i)
      {
        if (i != 0)
          Serial.write(32); // Space
        printHex2(nwkKey[i]);
      }
      Serial.println();
    }

    // Disable link check validation (automatically enabled
    // during join, but not supported by TTN at this time).
    LMIC_setLinkCheckMode(0);

    // Ok send our first data in 10 ms
    os_setTimedCallback(&sendjob, os_getTime() + ms2osticks(10), do_send);
#endif
    break;
  case EV_JOIN_FAILED:
#ifdef DEBUG
    Serial.println(F("EV_JOIN_FAILED"));
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
    Serial.println(F("EV_TXCOMPLETE (includes waiting for RX windows)"));
    if (LMIC.txrxFlags & TXRX_ACK)
      Serial.println(F("> Received ack"));
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

    // Going to sleep
    do_sleep(SLEEPTIME);

    // Schedule next transmission
    os_setCallback(&sendjob, do_send);
    break;
  case EV_LOST_TSYNC:
#ifdef DEBUG
    Serial.println(F("EV_LOST_TSYNC"));
#endif
    break;
  case EV_RESET:
#ifdef DEBUG
    Serial.println(F("EV_RESET"));
#endif

    break;
  case EV_RXCOMPLETE:
#ifdef DEBUG
    // data received in ping slot
    Serial.println(F("EV_RXCOMPLETE"));
#endif
    break;
  case EV_LINK_DEAD:
#ifdef DEBUG
    Serial.println(F("EV_LINK_DEAD"));
#endif
    break;
  case EV_LINK_ALIVE:
#ifdef DEBUG
    Serial.println(F("EV_LINK_ALIVE"));
#endif
    break;
  case EV_TXSTART:
#ifdef DEBUG
    Serial.println(F("EV_TXSTART"));
#endif
    break;
  case EV_TXCANCELED:
#ifdef DEBUG
    Serial.println(F("EV_TXCANCELED"));
#endif
    break;
  case EV_RXSTART:
    /* do not print anything -- it wrecks timing */
    break;
  case EV_JOIN_TXCOMPLETE:
#ifdef DEBUG
    Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
#endif
    break;
  default:
#ifdef DEBUG
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
#endif
    break;
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
  Serial.begin(115200);
  delay(100); // per sample code on RF_95 test

  Serial.println(F("\n=== Starting LoRaProMini ==="));
#endif

  //BME280 forced mode, 1x temperature / 1x humidity / 1x pressure oversampling, filter off
  if (!bme.begin(I2C_ADR_BME, &Wire))
  {
#ifdef DEBUG
    Serial.print(millis());
    Serial.print(": ");
    Serial.println(F("Could not find BME280"));
#endif
  }
  else
  {
    bme.setSampling(Adafruit_BME280::MODE_FORCED,
                    Adafruit_BME280::SAMPLING_X1, // temperature
                    Adafruit_BME280::SAMPLING_X1, // pressure
                    Adafruit_BME280::SAMPLING_X1, // humidity
                    Adafruit_BME280::FILTER_OFF,
                    Adafruit_BME280::STANDBY_MS_1000);
  }

  // LMIC init
  os_init();

  // Reset the MAC state. Session and pending data transfers will be discarded.
  lmicStartup();

#ifdef DEBUG
  Serial.print(millis());
  Serial.print(": ");
  Serial.println(F("Setup complete. Begin with LoRa"));
#endif

#ifdef ABP
  // Start job in ABP Mode
  do_send(&sendjob);
#else
  // Start job (sending automatically starts OTAA too)
  // Join the network, sending will be started after the event "Joined"
  LMIC_startJoining();
#endif
}

void loop()
{
  os_runloop_once();
}