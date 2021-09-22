# LoRaProMini

:warning: This project is a WIP!

A LoRaWAN sensor node for The Things Network, based on an Arduino Pro Mini and RFM95W/SX1276 LoRa module. Uses a Bosch BME280 (humidity, barometric pressure and ambient temperature) sensor to measure inside the enclosure and a Maxim DS18B20(+)/DS18S20(+)/DS1822 1-Wire sensor to measure temperature outside the enclosure. The PCB is installed in a solar lamp and supplied with power from it. Between the data transmissions, the Arduino, LoRa module and all sensors go into deep sleep mode to save power.

![PCB Front Assembled](.github/pcb_front_assembled.png)
<!--- ![PCB Front](.github/pcb_front.png) --->
![PCB Back Assembled](.github/pcb_back_assembled.png)
<!--- ![PCB Back](.github/pcb_back.png) --->
![PCB KiCad](.github/pcb_kicad.png)

## The Things Stack configuration

- LoRaWAN version `MAC V1.0.3`
- ...

## How to use

1. Flash config firmware (See [How to flash](#how-to-flash))
1. Start voltage calibration from menu
1. Set a voltage, measure the voltage with a multimeter and note the analog value. The range is optimized up to 5V
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

- [ ] Timeout to go to sleep after
- [x] Added
- [ ] Major + Minor Byte to single byte 4bits for major and 4 bits for minor
- [ ] Add option for Confirmed Uplink to config?
- [ ] Timeout for Wire (I2C) Libary
- [ ] Go to sleep immediately when voltage is too low
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
    
    var pin0 = (bytes[0] & 8) !== 0;
    var pin1 = (bytes[0] & 4) !== 0;
    var pin0last = (bytes[0] & 2) !== 0;
    var pin1last = (bytes[0] & 1) !== 0;
    var bat = bytes[1] << 8 | bytes[2];
    var fwversion = (bytes[3] >> 4) + "." + (bytes[3] & 0xf);
    var temp1 = (bytes[4] & 0x80 ? 0xFFFF << 16 : 0) | bytes[4] << 8 | bytes[5];
    var humi1 = bytes[6] << 8 | bytes[7];
    var press1 = bytes[8] << 8 | bytes[9];
    var temp2 = (bytes[10] & 0x80 ? 0xFFFF << 16 : 0) | bytes[10] << 8 | bytes[11];
    
    var mbStatus = "UNKOWN";
    if(pin0 || pin0last) {
      mbStatus = "FULL";
    } else if(pin1 || pin1last) {
      mbStatus = "EMPTY";
    }
    var interrupt = pin0 || pin1;

    return {
        data: {
            pin0: pin0,
            pin1: pin1,
            pin0last: pin0last,
            pin1last: pin1last,
            interrupt:interrupt,
            fwversion: fwversion,
            mbStatus: mbStatus,
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
