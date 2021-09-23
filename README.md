# LoRaProMini

:warning: This project is a WIP!

A LoRaWAN sensor node for The Things Network, based on an ATMEGA328P (Arduino Pro Mini) and RFM95W/SX1276 LoRa transiver module. 

![PCB Front Assembled](.github/pcb_front_assembled.png)

The module can be used to:
- Collect various climate values with the climate sensors
- It can tell if the letter carrier has put letters in the mailbox (with two reed switches)
- It can be used as a wirless button
- It can notify if someone rang the doorbell
- ...

## Features
- Deep sleep processor and sensors between data transmissions
- Two interrupt inputs could use to wake up the processor from deep sleep
- Confirmend und unconfirmend data up messages
- Extremely low power consumption
- Power input
    - Battery (Li-Ion with 3.7 works fine)
    - Battery with solar charger
    - Up to 6V
- Sensor support
    - Bosch BME280 (humidity, barometric pressure and ambient temperature)
    -  Maxim DS18B20(+)/DS18S20(+)/DS1822 1-Wire sensor to measure temperature 

## Example Applications (TBD)
- Mailbox Monitor
- Doorbell Monitor
- Weather Monitor

## PCB

<!--- ![PCB Front](.github/pcb_front.png) --->
![PCB Back](.github/pcb_back.png)
<!--- ![PCB Back](.github/pcb_back.png) --->
![PCB KiCad](.github/pcb_kicad.png)

## The Things Stack configuration

- LoRaWAN version `MAC V1.0.3`
- TBD

## How to use

1. Flash config firmware (See [How to flash](#how-to-flash))
1. Start voltage calibration from menu
1. Set a voltage, measure the voltage with a multimeter and note the analog value. The range is optimized up to 6V
1. Use volts-per-bit calculator to get VBP factor for config
1. Create configuration with [Configuration Builder](https://foorschtbar.github.io/LoRaProMini/)
1. Write configuration to EEPROM using configuration menu
1. Check written configuration via configuration menu
1. Flash debug or release firmware (See [How to flash](#how-to-flash))
1. Finish

## How to flash

```
avrdude-F -v -c arduino -p atmega328p -P <COM PORT> -b 57600 -D -U flash:w:<FIRMWARE FILE>:i
```

Example:
```
avrdude -F -v -c arduino -p atmega328p -P COM4 -b 57600 -D -U flash:w:firmware_1.0_config.hex:i
```

## ToDo

- [ ] Go to sleep immediately when voltage is too low
- [x] Add wake up trough interrupt pins
- [x] Move Major- and Minorversion byte to single byte. 4 bits for major and 4 bits for minor.
- [x] Add option for Confirmed Uplink to config
- [x] Add CI/CD pipeline to build firmware
- [x] Rewirte VBP calculator in Configuration Builder
- [x] Move config to EEPROM
- [x] Added special firmware to change configs
- [x] Build HTML/JS Interface to build configs
- [x] Add CRC32 check
- [x] Test ABP
- [x] Test OTAA
- [x] Deploy config tool via GitHub Pages
- [x] Fix problem when checksum in pastend config had zeros O.o
- [x] Add random EUI generator button to config tool
- [x] Parse config string to GUI fields

 
## TTN Payload decoder
```javascript
function decodeUplink(input) {
    var bytes = input.bytes;
    
    var itrTrigger = (bytes[0] & 1) !== 0; // Message was triggerd from interrupt
    var itr0 = (bytes[0] & 2) !== 0; // Interrupt 0
    var itr1 = (bytes[0] & 4) !== 0; // Interrupt 1
    var bat = bytes[1] << 8 | bytes[2]; // Battery
    var fwversion = (bytes[3] >> 4) + "." + (bytes[3] & 0xf); // Firmware version
    var temp1 = (bytes[4] & 0x80 ? 0xFFFF << 16 : 0) | bytes[4] << 8 | bytes[5]; // BME Temperature
    var humi1 = bytes[6] << 8 | bytes[7]; // BME Humidity
    var press1 = bytes[8] << 8 | bytes[9]; // BME Pressure
    var temp2 = (bytes[10] & 0x80 ? 0xFFFF << 16 : 0) | bytes[10] << 8 | bytes[11]; // DS18x Temperature
    
    var mbStatus = "UNKOWN";
    if (itr0) {
      mbStatus = "FULL";
    } else if (itr1) {
      mbStatus = "EMPTY";
    }

    return {
        data: {
            interrupts: {
                itr0: itr0,
                itr1: itr1,
                itrTrigger: itrTrigger
            },
            extra: {
                mbStatus: mbStatus,
                mbChanged: itrTrigger
            },
            fwversion: fwversion,
            bme: {
                temperature: temp1 / 100,
                humidity: humi1 / 100,
                pressure: press1
            },
            ds18x: {
                temperature: temp2 / 100
            },
            battery: bat / 100
        },
        warnings: [],
        errors: []
    }
}
```
