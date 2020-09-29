#include <ESP8266WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#include <CREDENTIALS.h>

// Set ADC mode for voltage reading.
ADC_MODE(ADC_VCC);

// Set up static IP and WiFi details
IPAddress ip(10, 3, 3, 6);
IPAddress gateway(10, 3, 3, 1);
IPAddress mask(255, 255, 255, 0);
const char* ssid = WIFI_SSID;
const char* password = WIFI_PASS;

// MQTT config
const char* mqtt_server = MQTT_SERVER;
WiFiClient espClient;
PubSubClient client(espClient);

#define MQTT_TOPIC "tilted/data"

// Maximum time to be awake, in ms. This is needed in case the MPU sensor
// fails to return any samples.
#define WAKE_TIMEOUT 10000

// I2C pins
#define SDA_PIN 4
#define SCL_PIN 5

// pull low to run in calibration mode
#define CALIBRATE_PIN 14

// number of tilt samples to average
#define MAX_SAMPLES 5

// Normal interval should be long enough to stretch out battery life. Since
// we're using the MPU temp sensor, we're probably going to see slower
// response times so longer intervals aren't a terrible idea.
#define NORMAL_INTERVAL 1800

// In calibration mode, we need more frequent updates.
#define CALIBRATION_INTERVAL 30

// When the battery cell (default assumes NiMH) gets this low,
// the ESP switches to every
// LOW_VOLTAGE_MULTIPLIER*SLEEP_UPDATE_INTERVAL second updates. Hopefully
// someone is monitoring that stuff and can swap batteries before it really dies.
#define LOW_VOLTAGE_THRESHOLD 3000
#define LOW_VOLTAGE_MULTIPLIER 4

// when we booted
static unsigned long bootTime, wifiTime, mqttTime, sent = 0;

RF_PRE_INIT() {
	bootTime = millis();
}

// Disable RF calibration after deep-sleep wake up.
// This is probably not needed.
//RF_MODE(RF_NO_CAL);

//------------------------------------------------------------
static MPU6050 mpu;
static long sleep_interval = NORMAL_INTERVAL;

//------------------------------------------------------------
static const int led = LED_BUILTIN;

static inline void ledOn()
{
	// reduce the brightness a whole bunch
	analogWrite(led, PWMRANGE - 20);
}
static inline void ledOff()
{
	analogWrite(led, 0);
	digitalWrite(led, HIGH);
}

//-----------------------------------------------------------------
static void putMpuToSleep()
{
	static int slept = 0;
	if (slept == 0)
	{
		mpu.setSleepEnabled(true);
	}
	slept = 1;
}

static void actuallySleep()
{
	// If we haven't already done this...
	putMpuToSleep();

	double uptime = (millis() - bootTime) / 1000.;

	long willsleep = sleep_interval - uptime;
	if (willsleep <= sleep_interval / 2)
	{
		// If we somehow ended up awake longer than half a sleep interval,
		// sleep longer. This shouldn't happen in practice.
		willsleep = sleep_interval;
	}
	Serial.printf("bootTime: %ld WifiTime: %ld mqttTime: %ld\n", bootTime, wifiTime, mqttTime);
	Serial.printf("Deep sleeping %ld seconds after %.3g awake\n", willsleep, uptime);

	ESP.deepSleepInstant(willsleep * 1000000, WAKE_NO_RFCAL);
}

//-----------------------------------------------------------------
static int voltage = 0;

static inline double readVoltage() {
  return (voltage = ESP.getVcc());
}

//--------------------------------------------------------------
static unsigned int nsamples = 0;
static float samples[MAX_SAMPLES];
static float temperature = 0.0;

float round1(float value)
{
	return (int)(value * 10 + 0.5) / 10.0;
}

static void sendSensorData()
{
	Serial.println("Sending data...");

	// calculate average. Median might
	// throw away initial "bad" readings.
	double sum = 0;
	for (unsigned int i = 0; i < nsamples; i++)
	{
		sum += samples[i];
	}

	// Serialize data as JSON before sending.
	StaticJsonDocument<200> doc;
	doc["tilt"] = round1(sum / nsamples);
	doc["temp"] = round1(temperature);
	doc["volt"] = voltage;
	doc["interval"] = sleep_interval;

	char jsonString[200];
	serializeJson(doc, jsonString);

	// Connect to WiFi and send with MQTT.
	WiFi.mode(WIFI_STA);
	WiFi.config(ip, gateway, mask);
	WiFi.begin(ssid, password);

	while(WiFi.status() != WL_CONNECTED && (millis() - bootTime) < WAKE_TIMEOUT) {
		delay(5);
	}

	wifiTime = millis();

	client.setServer(mqtt_server, 1883);

	String clientId = "Tilted-";
	clientId += String(random(0xffff), HEX);

	while (!client.connected() && (millis() - bootTime) < WAKE_TIMEOUT) {
		if (client.connect(clientId.c_str())) {
			client.publish(MQTT_TOPIC, jsonString, true);
			sent = millis();
		} else {
			delay(5);
		}
	}

	mqttTime = millis();
}

void setup()
{
	// Connect GPIO 16 to RST to wake up.
	// Possibly only needed on Wemos D1
	pinMode(16, WAKEUP_PULLUP);

	pinMode(led, OUTPUT);
	ledOff();

	Serial.begin(115200);
	Serial.println("Reboot");

	Serial.print("Booting because ");
	Serial.println(ESP.getResetReason());

	Serial.println("Build: " __DATE__ " " __TIME__);

	pinMode(CALIBRATE_PIN, INPUT_PULLUP);
	if (digitalRead(CALIBRATE_PIN))
	{
		Serial.println("Normal mode");

		readVoltage();
		Serial.println(voltage);
		bool lowv = !(voltage != 0 && voltage > LOW_VOLTAGE_THRESHOLD);
		if (lowv)
		{
			Serial.println("Voltage below threshold, sleeping longer");
			sleep_interval *= LOW_VOLTAGE_MULTIPLIER;
		}
	}
	else
	{
		Serial.println("Calibration mode");

		// The only difference between "normal" and "calibration"
		// is the update frequency. We still deep sleep between samples.
		sleep_interval = CALIBRATION_INTERVAL;
	}

	/* INITIALIZE MPU
	Serial.println("Starting MPU-6050");
	Wire.begin(SDA_PIN, SCL_PIN);
	Wire.setClock(400000);

	mpu.initialize();
	mpu.setFullScaleAccelRange(MPU6050_ACCEL_FS_2);
	mpu.setFullScaleGyroRange(MPU6050_GYRO_FS_250);
	mpu.setDLPFMode(MPU6050_DLPF_BW_5);
	mpu.setTempSensorEnabled(true);
	mpu.setInterruptLatch(0); // pulse
	mpu.setInterruptMode(1);  // Active Low
	mpu.setInterruptDrive(1); // Open drain
	mpu.setRate(17);
	mpu.setIntDataReadyEnabled(true);
	*/

	Serial.println("Finished setup");
}

static float calculateTilt(float ax, float az, float ay)
{
	float pitch = (atan2(ay, sqrt(ax * ax + az * az))) * 180.0 / PI;
	float roll = (atan2(ax, sqrt(ay * ay + az * az))) * 180.0 / PI;
	return sqrt(pitch * pitch + roll * roll);
}

void loop()
{
	if (sent)
	{
		actuallySleep();
	}
	else if ((millis() - bootTime) > WAKE_TIMEOUT)
	{
		actuallySleep();
	}
	//else if (nsamples < MAX_SAMPLES && mpu.getIntDataReadyStatus())
	else if (nsamples < MAX_SAMPLES)
	{
		int16_t ax, ay, az;
		ax = 1;
		ay = 2;
		az = 3;
		//mpu.getAcceleration(&ax, &az, &ay);

		float tilt = calculateTilt(ax, az, ay);
		if (tilt > 0.0)
		{
			// we sometimes get bogus zero initial readings
			// after a hard boot. Ignore them.
			samples[nsamples++] = tilt;
		}

		if (nsamples >= MAX_SAMPLES)
		{
			// As soon as we have all our samples, read the temperature
			//temperature = mpu.getTemperature() / 340.0 + 36.53;
			temperature = 25.0;

			// ... and put the MPU back to sleep. No reason for it to
			// be sampling while we're doing networky things.
			//putMpuToSleep();

			// no need to wait for the delay
			sendSensorData();
		}
	}

	// mpu.getIntDataReadyStatus() hits the I2C bus. We don't need
	// to poll every ms while we're gathering samples. Once we have
	// the samples we're just waiting for the transmit to clear, so
	// loop a bit quicker.
	delay((nsamples < MAX_SAMPLES) ? 5 : 1);
}