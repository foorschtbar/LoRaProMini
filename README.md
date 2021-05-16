# LoRaProMini

**This project is a WIP!**

A LoRaWAN node for The Things Network, based on a Arduino Pro Mini and RFM95W/SX1276 LoRa module. The PCB is installed in a solar lamp and supplied with power from it. Between the data transmissions, the Arduino, LoRa module and all sensors go into (deep) sleep mode to save power.

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
