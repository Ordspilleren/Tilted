# Tilted

Work in progress hydrometer solution using ESP8266. 

Tilted is very similar to iSpindel, the most popular open-source hydrometer solution, but this project aims to be a simpler, smaller, and more compact version.

The first key difference is in architecture. Instead of connecting the sensor (Tilt) device itself to WiFi, the sensor device instead sends data to a gateway device, which in turns connects to WiFi. This enables two key advantages: First, you can place the gateway close to the sensor itself, which can be advantageous if you have a metal fermentation vessel that hinders the wireless signal. Second, the battery life is greatly improved since the sensor device does not have to worry about anything WiFi related.

Due to the above approach, a conservative estimate of the battery life, assuming most of the uptime is spent in normal mode, would be somewhere around 1 year!

## Usage
### Calibration mode
Calibration mode can be entered by placing the sensor device with the lid facing down. This will enable an update interval of 30 seconds for 30 minutes, allowing the user to calibrate the device.

The above procedure can only be done within 30 seconds from inserting the battery.

## Hardware

Unlike the iSpindel project, Tilted uses a bare ESP-12 module for the sensor device. This has some disadvantages, one of them being that the wiring and initial flashing procedure will be harder since there is no access to USB. The bare module is a hard requirement however since a regular Wemos D1 module is simply too big for the desired footprint. A huge advantage of the bare ESP-12 module is that the battery life of the final product is *greatly* increased.

For the battery, Tilted utilizes the amazing LiFePO4 battery technology. Since LiFePO4 batteries output a nominal voltage of ~3.3V, we can power everything directly from it without using any voltage converters.

### Parts list

For the sensor device itself, the following parts are needed:
* ESP-12F or ESP-12S
  * The S variant might be preferred since it has the required resistors for running built in.
* MPU-6050 Gyro/Accelerometer
* 14500 LiFePO4 battery
  * I personally use Soshine 14500 LiFePO4 batteries, and I highly recommend them.
* 113mm (inner length) x 21mm (inner diameter) PET preform
  * Any type can be used as long as the size is about right. I recommend this: https://www.ebay.com/itm/142622693372.
* 3D printed sled

For the gateway device, you will need these parts:
* Wemos D1
* 3D printed case

### Wiring the ESP-12

For flashing, the following diagram is sufficient:
![](https://www.allaboutcircuits.com/uploads/articles/20170323-lee-wifieye-circuit-pgm-1.jpg)

For running:

VCC --> EN, MPU VCC, Battery +

GND --> IO15 (pull-down resistor preferred), MPU GND, Battery -

IO16 --> RST

IO4 --> MPU SDA

IO5 --> MPU SCL

## Credits
[weeSpindel](https://github.com/c-/weeSpindel): I took heavy inspiration from the code, but also the approach as a whole.