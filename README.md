# LoRaProMini

:warning: This project is a WIP!

A LoRaWAN sensor node for The Things Network, based on an Arduino Pro Mini and RFM95W/SX1276 LoRa module. Uses a Bosch BME280 (humidity, barometric pressure and ambient temperature) sensor to measure inside the enclosure and a Maxim DS18B20(+)/DS18s20(+)/DS1822 1-Wire sensor to measure temperature outside the enclosure. The PCB is installed in a solar lamp and supplied with power from it. Between the data transmissions, the Arduino, LoRa module and all sensors go into deep sleep mode to save power.

![PCB Front Assembled](.github/pcb_front_assembled.png)
<!--- ![PCB Front](.github/pcb_front.png) --->
![PCB Back Assembled](.github/pcb_back_assembled.png)
<!--- ![PCB Back](.github/pcb_back.png) --->
![PCB KiCad](.github/pcb_kicad.png)

## The Things Stack configuration

- LoRaWAN version `MAC V1.0.3`
- ...

## ToDo

- [x] Move config to EEPROM
- [x] Added special firmware to change configs
- [x] Build HTML/JS Interface to build configs
- [x] Add CRC32 check
- [x] Test ABP
- [x] Test OTAA
- [ ] Add CI/CD pipeline to build firmware
- [ ] Deploy config tool via GitHub Pages
- [ ] Go to sleep immediately when voltage is too low
- [ ] Fix problem when checksum in pastend config had zeros O.o
- [ ] Add random EUI generator button to config tool
 
## TTN Payload decoder
```javascript
function Decoder(bytes, port) {
  var temp1 = (bytes[0] & 0x80 ? 0xFFFF<<16 : 0) | bytes[0]<<8 | bytes[1];
  var humi1 = bytes[2]<<8 | bytes[3];
  var press1 = bytes[4]<<8 | bytes[5];
  var temp2 = (bytes[6] & 0x80 ? 0xFFFF<<16 : 0) | bytes[6]<<8 | bytes[7];
  var bat = bytes[8]<<8 | bytes[9];
  return { 
    bme : {
      temperature: temp1 / 100, 
      humidity: humi1 / 100, 
      pressure: press1
    },
    ds18x : {
      temperature: temp2 / 100
      
    },
    battery: bat / 100} 
} 
```
