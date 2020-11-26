# Tilted

Work in progress hydrometer solution using ESP8266. 

## Usage
### Calibration mode
Calibration mode can be entered by placing the sensor device with the lid facing down. This will enable an update interval of 30 seconds for 30 minutes, allowing the user to calibrate the device.

The above procedure can only be done within 30 seconds from inserting the battery.

## Wiring the ESP-12

For flashing, the following diagram is sufficient:
![](https://www.allaboutcircuits.com/uploads/articles/20170323-lee-wifieye-circuit-pgm-1.jpg)

For running:

VCC --> EN, MPU VCC, Battery +

GND --> IO15 (pull-down resistaor preferred), MPU GND, Battery -

IO16 --> RST

IO4 --> MPU SDA

IO5 --> MPU SCL

## Credits
[weeSpindel](https://github.com/c-/weeSpindel): I took heavy inspiration from the code, but also the approach as a whole.