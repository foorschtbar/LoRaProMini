#include <Arduino.h>
#include <lmic.h>
#include <hal/hal.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <LowPower.h>

#include "config.h"

//#define LMIC_DEBUG_LEVEL 2

// Pins
#define LORA_DIO0 2
#define LORA_DIO1 7
#define LORA_DIO2 8
#define LORA_RESET 9
#define LORA_CS SS

// I2C Adresses
#define I2C_ADR_BME 0x76

// Enable debug prints to serial monitor
#define DEBUG_ON

Adafruit_BME280 bme;

static osjob_t sendjob;

const lmic_pinmap lmic_pins = {
    .nss = LORA_CS,
    .rxtx = LMIC_UNUSED_PIN,
    .rst = LORA_RESET,
    .dio = {LORA_DIO0, LORA_DIO1, LORA_DIO2},
};

float readBat()
{
  //   int sensorValue = analogRead(BATTERY_SENSE_PIN);
  // #ifdef DEBUG_ON
  //   Serial.println(sensorValue);
  // #endif
  //   // 1M, 470K divider across battery and using internal ADC ref of 1.1V
  //   // Sense point is bypassed with 0.1 uF cap to reduce noise at that point
  //   // ((1e6+470e3)/470e3)*1.1 = Vmax = 3.44 Volts
  //   // 3.44/1023 = Volts per bit = 0.003363075

  //   // 2M, 470K 1.1V ref => 5,78V Max!
  //   // 5,78/1023 = 0.00565
  //   float batteryV = sensorValue * 0.00565;

  // #ifdef DEBUG_ON
  //   Serial.print("Battery Voltage: ");
  //   Serial.print(batteryV);
  //   Serial.println(" V");
  // #endif
  //   return batteryV;
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
    bme.takeForcedMeasurement();

    // Read sensor values and multiply by 100 to effectively keep 2 decimals
    // Signed 16 bits integer, -32767 up to +32767
    int16_t temp = bme.readTemperature() * 100;
    // t = t + 40; => t [-40..+85] => [0..125] => t = t * 10; => t [0..125] => [0..1250]
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t humi = bme.readHumidity() * 100;
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t press = bme.readPressure() / 100.0F; // p [300..1100]
    // Unsigned 16 bits integer, 0 up to 65535
    uint16_t bat = 0; //readBat() * 100;

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

#ifdef DEBUG_ON
    Serial.print(F("> Temperature: "));
    Serial.println(temp);
    Serial.print(F("> Humidity: "));
    Serial.println(humi);
    Serial.print(F("> Pressure: "));
    Serial.println(press);
    Serial.print(F("> Battery: "));
    Serial.println(bat);
    Serial.print(F("> LoRa packet payload: "));

    char lorapacket[24];
    sprintf(lorapacket, "%02X %02X %02X %02X %02X %02X %02X %02X", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7]);
    Serial.println(lorapacket);

    // for (size_t i = 0; i < sizeof(buffer); i++)
    // {
    //   if (buffer[i] < 16)
    //     Serial.write('0');
    //   Serial.print(buffer[i], HEX);
    //   Serial.print(" ");
    // }
#endif

    // Prepare upstream data transmission at the next possible time.
    LMIC_setTxData2(1, buffer, sizeof(buffer), 0);

    Serial.print(os_getTime());
    Serial.print(": ");
    Serial.println(F("Packet queued"));
  }
}

void lmicStartup()
{

  // Reset the MAC state. Session and pending data transfers will be discarded.
  LMIC_reset();

  //   // Set static session parameters. Instead of dynamically establishing a session
  //   // by joining the network, precomputed session parameters are be provided.
  //   uint8_t appskey[sizeof(APPSKEY)];
  //   uint8_t nwkskey[sizeof(NWKSKEY)];
  //   memcpy_P(appskey, APPSKEY, sizeof(APPSKEY));
  //   memcpy_P(nwkskey, NWKSKEY, sizeof(NWKSKEY));
  //   LMIC_setSession(0x13, DEVADDR, nwkskey, appskey);

  // #if defined(CFG_eu868)
  //   // Set up the channels used by the Things Network, which corresponds
  //   // to the defaults of most gateways. Without this, only three base
  //   // channels from the LoRaWAN specification are used, which certainly
  //   // works, so it is good for debugging, but can overload those
  //   // frequencies, so be sure to configure the full frequency range of
  //   // your network here (unless your network autoconfigures them).
  //   // Setting up channels should happen after LMIC_setSession, as that
  //   // configures the minimal channel set. The LMIC doesn't let you change
  //   // the three basic settings, but we show them here.
  //   LMIC_setupChannel(0, 868100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(1, 868300000, DR_RANGE_MAP(DR_SF12, DR_SF7B), BAND_CENTI); // g-band
  //   LMIC_setupChannel(2, 868500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(3, 867100000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(4, 867300000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(5, 867500000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(6, 867700000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(7, 867900000, DR_RANGE_MAP(DR_SF12, DR_SF7), BAND_CENTI);  // g-band
  //   LMIC_setupChannel(8, 868800000, DR_RANGE_MAP(DR_FSK, DR_FSK), BAND_MILLI);   // g2-band
  // // TTN defines an additional channel at 869.525Mhz using SF9 for class B
  // // devices' ping slots. LMIC does not have an easy way to define set this
  // // frequency and support for class B is spotty and untested, so this
  // // frequency is not configured here.
  // #elif defined(CFG_us915) || defined(CFG_au915)
  //   // NA-US and AU channels 0-71 are configured automatically
  //   // but only one group of 8 should (a subband) should be active
  //   // TTN recommends the second sub band, 1 in a zero based count.
  //   // https://github.com/TheThingsNetwork/gateway-conf/blob/master/US-global_conf.json
  //   LMIC_selectSubBand(1);
  // #elif defined(CFG_as923)
  // // Set up the channels used in your country. Only two are defined by default,
  // // and they cannot be changed.  Use BAND_CENTI to indicate 1% duty cycle.
  // // LMIC_setupChannel(0, 923200000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);
  // // LMIC_setupChannel(1, 923400000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_CENTI);

  // // ... extra definitions for channels 2..n here
  // #elif defined(CFG_kr920)
  // // Set up the channels used in your country. Three are defined by default,
  // // and they cannot be changed. Duty cycle doesn't matter, but is conventionally
  // // BAND_MILLI.
  // // LMIC_setupChannel(0, 922100000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
  // // LMIC_setupChannel(1, 922300000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
  // // LMIC_setupChannel(2, 922500000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

  // // ... extra definitions for channels 3..n here.
  // #elif defined(CFG_in866)
  // // Set up the channels used in your country. Three are defined by default,
  // // and they cannot be changed. Duty cycle doesn't matter, but is conventionally
  // // BAND_MILLI.
  // // LMIC_setupChannel(0, 865062500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
  // // LMIC_setupChannel(1, 865402500, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);
  // // LMIC_setupChannel(2, 865985000, DR_RANGE_MAP(DR_SF12, DR_SF7),  BAND_MILLI);

  // // ... extra definitions for channels 3..n here.
  // #else
  // #error Region not supported
  // #endif

  //   // Disable link check validation
  //   LMIC_setLinkCheckMode(0);

  //   // TTN uses SF9 for its RX2 window.
  //   LMIC.dn2Dr = DR_SF9;

  //   // Set data rate and transmit power for uplink
  //   LMIC_setDrTxpow(DR_SF7, 14);

  LMIC_setClockError(MAX_CLOCK_ERROR * 5 / 100); // fix OTAA joining for Arduino Pro Mini (https://github.com/matthijskooijman/arduino-lmic#problems-with-downlink-and-otaa)

  //LMIC_setLinkCheckMode(1);
  //LMIC_setAdrMode(1);

  // Start job (sending automatically starts OTAA too)
  // Join the network, sending will be
  // started after the event "Joined"
  LMIC_startJoining();
}

extern volatile unsigned long timer0_millis;
void advanceMillis(unsigned long offset)
{
  uint8_t oldSREG = SREG;
  cli();
  timer0_millis += offset;
  SREG = oldSREG;
  sei();
}

void do_sleep(uint16_t sleepyTime)
{
  uint16_t eights = sleepyTime / 8;
  uint16_t fours = (sleepyTime % 8) / 4;
  uint16_t twos = ((sleepyTime % 8) % 4) / 2;
  uint16_t ones = ((sleepyTime % 8) % 4) % 2;

#ifdef DEBUG_ON
  Serial.print(os_getTime());
  Serial.print(": ");
  Serial.print(F("Sleeping for "));
  Serial.print(sleepyTime);
  Serial.print(F(" seconds ("));
  Serial.print(eights);
  Serial.print(F("x8 + "));
  Serial.print(fours);
  Serial.print(F("x4 + "));
  Serial.print(twos);
  Serial.print(F("x2 + "));
  Serial.print(ones);
  Serial.println(F(")"));
  delay(100); //Wait for serial to complete
#endif

  // Power down LoRa Module
  LMIC.opmode = OP_SHUTDOWN;

  for (uint16_t i = 0; i < eights; i++)
  {
    // put the processor to sleep for 8 seconds
    LowPower.powerDown(SLEEP_8S, ADC_OFF, BOD_OFF);
  }
  for (uint16_t i = 0; i < fours; i++)
  {
    // put the processor to sleep for 4 seconds
    LowPower.powerDown(SLEEP_4S, ADC_OFF, BOD_OFF);
  }
  for (uint16_t i = 0; i < twos; i++)
  {
    // put the processor to sleep for 2 seconds
    LowPower.powerDown(SLEEP_2S, ADC_OFF, BOD_OFF);
  }
  for (uint16_t i = 0; i < ones; i++)
  {
    // put the processor to sleep for 1 seconds
    LowPower.powerDown(SLEEP_1S, ADC_OFF, BOD_OFF);
  }

  advanceMillis(sleepyTime * 1000);

  LMIC.opmode = OP_NONE;
}

void onEvent(ev_t ev)
{
  Serial.print(os_getTime());
  Serial.print(": ");
  switch (ev)
  {
  case EV_SCAN_TIMEOUT:
    Serial.println(F("EV_SCAN_TIMEOUT"));
    break;
  case EV_BEACON_FOUND:
    Serial.println(F("EV_BEACON_FOUND"));
    break;
  case EV_BEACON_MISSED:
    Serial.println(F("EV_BEACON_MISSED"));
    break;
  case EV_BEACON_TRACKED:
    Serial.println(F("EV_BEACON_TRACKED"));
    break;
  case EV_JOINING:
    Serial.println(F("EV_JOINING"));
    break;
  case EV_JOINED:
    Serial.println(F("EV_JOINED"));

    // Disable link check validation (automatically enabled
    // during join, but not supported by TTN at this time).
    LMIC_setLinkCheckMode(0);

    // Ok send our first data in 10 ms
    os_setTimedCallback(&sendjob, os_getTime() + ms2osticks(10), do_send);
    break;
  case EV_JOIN_FAILED:
    Serial.println(F("EV_JOIN_FAILED"));
    lmicStartup(); //Reset LMIC and retry
    break;
  case EV_REJOIN_FAILED:
    Serial.println(F("EV_REJOIN_FAILED"));
    lmicStartup(); //Reset LMIC and retry
    break;
  case EV_TXCOMPLETE:
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

    // Schedule next transmission
    do_sleep(TX_INTERVAL);
    os_setCallback(&sendjob, do_send);
    break;
  case EV_LOST_TSYNC:
    Serial.println(F("EV_LOST_TSYNC"));
    break;
  case EV_RESET:
    Serial.println(F("EV_RESET"));
    break;
  case EV_RXCOMPLETE:
    // data received in ping slot
    Serial.println(F("EV_RXCOMPLETE"));
    break;
  case EV_LINK_DEAD:
    Serial.println(F("EV_LINK_DEAD"));
    break;
  case EV_LINK_ALIVE:
    Serial.println(F("EV_LINK_ALIVE"));
    break;
  /*
        || This event is defined but not used in the code. No
        || point in wasting codespace on it.
        ||
        || case EV_SCAN_FOUND:
        ||    Serial.println(F("EV_SCAN_FOUND"));
        ||    break;
        */
  case EV_TXSTART:
    Serial.println(F("EV_TXSTART"));
    break;
  case EV_TXCANCELED:
    Serial.println(F("EV_TXCANCELED"));
    break;
  case EV_RXSTART:
    /* do not print anything -- it wrecks timing */
    break;
  case EV_JOIN_TXCOMPLETE:
    Serial.println(F("EV_JOIN_TXCOMPLETE: no JoinAccept"));
    break;
  default:
    Serial.print(F("Unknown event: "));
    Serial.println((unsigned)ev);
    break;
  }
}

void setup()
{
  //pinMode(LED_BUILTIN, OUTPUT);
  while (!Serial)
    ; // wait for Serial to be initialized
  Serial.begin(115200);
  delay(100); // per sample code on RF_95 test

  Serial.println(F("\n=== Starting LoRaProMini ==="));

  // BME280 forced mode, 1x temperature / 1x humidity / 1x pressure oversampling, filter off
  if (!bme.begin(I2C_ADR_BME, &Wire))
  {
    Serial.print(os_getTime());
    Serial.print(": ");
    Serial.println(F("Could not find BME280"));
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

  Serial.print(os_getTime());
  Serial.print(": ");
  Serial.println(F("Setup complete"));
}

void loop()
{
  os_runloop_once();

  // // Start job
  // do_send(&sendjob);

  // while (LMIC_transmitted != 1)
  // {
  //   os_runloop_once();
  //   // Add timeout counter when nothing happens:
  //   LMIC_event_Timeout++;
  //   delay(100);
  //   if (LMIC_event_Timeout >= 600)
  //   {
  //     // Timeout when there's no "EV_TXCOMPLETE" event after 60 seconds
  //     Serial.print(F("\tETimeout, msg not tx\n"));
  //     break;
  //   }
  // }

  // Serial.println(LMIC_event_Timeout);

  // LMIC_transmitted = 0;
  // LMIC_event_Timeout = 0;

  // Serial.print(os_getTime());
  // Serial.println(F("Going to sleep."));
  // delay(100); // allow serial to send.

  // for (int i = 0; i < TX_INTERVAL / 8; i++)
  // {
  //   // Enter power down state for 8 s with ADC and BOD module disabled
  //   LowPower.idle(SLEEP_8S, ADC_OFF, TIMER2_OFF, TIMER1_OFF, TIMER0_OFF,
  //                 SPI_OFF, USART0_OFF, TWI_OFF);
  // }
}