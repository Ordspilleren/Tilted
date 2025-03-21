#include <ESP8266WiFi.h>
#include <ESP8266httpUpdate.h>
#include <espnow.h>
#include <Wire.h>
#include "MPU6050.h"
#include "credentials.h"

// Set ADC mode for voltage reading.
ADC_MODE(ADC_VCC);

// Maximum time to be awake, in ms. This is needed in case the MPU sensor
// fails to return any samples.
#define WAKE_TIMEOUT 10000

// I2C pins
#define SDA_PIN 4
#define SCL_PIN 5

// number of tilt samples to average
#define MAX_SAMPLES 5

// Normal interval should be long enough to stretch out battery life. Since
// we're using the MPU temp sensor, we're probably going to see slower
// response times so longer intervals aren't a terrible idea.
#define NORMAL_INTERVAL 1800

// In calibration mode, we need more frequent updates.
// Here we define the RTC address to use and the number of iterations.
// 60 iterations with an interval of 30 equals 30 minutes.
#define CALIBRATION_INTERVAL 30
#define RTC_ADDRESS 0
#define CALIBRATION_ITERATIONS 60
#define CALIBRATION_TILT_ANGLE_MIN 170
#define CALIBRATION_TILT_ANGLE_MAX 180
#define CALIBRATION_SETUP_TIME 30000
#define WIFI_TIMEOUT 10000

// When the battery cell (LiFePO4 in this case) gets this low,
// the ESP switches to every LOW_VOLTAGE_MULTIPLIER*SLEEP_UPDATE_INTERVAL second updates.
#define LOW_VOLTAGE_THRESHOLD 3000
#define LOW_VOLTAGE_MULTIPLIER 4

// Version identifier for OTA.
const char versionTimestamp[] = "TiltedSensor " __DATE__ " " __TIME__;

// Low-pass filter coefficient (0 = no filtering, 1 = ignore new readings)
#define FILTER_ALPHA 0.2

// the following three settings must match the slave settings
uint8_t remoteMac[] = {0x3A, 0x33, 0x33, 0x33, 0x33, 0x33};
const uint8_t channel = 1;
struct __attribute__((packed)) DataStruct
{
	float tilt;
	float temp;
	int volt;
	long interval;
};

DataStruct tiltData;

// when we booted
static unsigned long bootTime, wifiTime, mqttTime, sent, calibrationSetupStart, calibrationWifiStart = 0;

uint32_t calibrationIterations = 0;

// Sensor state variables
enum SensorState {
    STATE_INIT,
    STATE_SAMPLING,
    STATE_PROCESSING,
    STATE_TRANSMITTING,
    STATE_SLEEPING
};

SensorState currentState = STATE_INIT;

RF_PRE_INIT()
{
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
	digitalWrite(led, LOW);
}
static inline void ledOff()
{
	digitalWrite(led, HIGH);
}

//-----------------------------------------------------------------
static void putMpuToSleep()
{
	mpu.setSleepEnabled(true);
    Serial.println("MPU put to sleep");
}

// Calculate tilt angle from accelerometer readings
static float calculateTilt(float ax, float az, float ay)
{
	if (ax == 0 && ay == 0 && az == 0)
		return 0.f;

	return acos(az / (sqrt(ax * ax + ay * ay + az * az))) * 180.0 / PI;
}

// Apply median filter to remove outliers
static float medianFilter(float values[], int size)
{
    // Create a copy of the array for sorting
    float temp[size];
    for (int i = 0; i < size; i++) {
        temp[i] = values[i];
    }
    
    // Simple bubble sort
    for (int i = 0; i < size - 1; i++) {
        for (int j = 0; j < size - i - 1; j++) {
            if (temp[j] > temp[j + 1]) {
                float swap = temp[j];
                temp[j] = temp[j + 1];
                temp[j + 1] = swap;
            }
        }
    }
    
    // Return middle value for odd-sized array
    if (size % 2 == 1) {
        return temp[size / 2];
    } 
    // Return average of two middle values for even-sized array
    else {
        return (temp[size / 2 - 1] + temp[size / 2]) / 2.0;
    }
}

static void actuallySleep()
{
    // Put MPU to sleep if not already done
    putMpuToSleep();
    
    // Turn off WiFi completely to save power
    WiFi.mode(WIFI_OFF);
    WiFi.forceSleepBegin();
    delay(1); // Give WiFi time to shut down

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

static inline int readVoltage()
{
    // Read voltage multiple times and average for better accuracy
    const int readings = 3;
    int sum = 0;
    for (int i = 0; i < readings; i++) {
        sum += ESP.getVcc();
        delay(5);
    }
    return (voltage = sum / readings);
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
    Serial.println("Processing and sending data...");

    // Apply median filter to samples to remove outliers
    float filteredValue = medianFilter(samples, nsamples);
    
    tiltData.tilt = round1(filteredValue);
    tiltData.temp = round1(temperature);
    tiltData.volt = voltage;
    tiltData.interval = sleep_interval;

    // Initialize WiFi in STA mode
    WiFi.forceSleepWake();
    delay(1);
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();

    unsigned long espnow_start = millis();
    unsigned long timeout = WAKE_TIMEOUT / 2;  // Shorter timeout for ESP-NOW
    
    bool init_success = false;
    while ((millis() - espnow_start) < timeout) {
        if (esp_now_init() == 0) {
            init_success = true;
            break;
        }
        delay(10);
    }
    
    if (!init_success) {
        Serial.println("ESP-NOW init failed, sleeping without sending data");
        actuallySleep();
        return;
    }

    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_add_peer(remoteMac, ESP_NOW_ROLE_SLAVE, channel, NULL, 0);

    wifiTime = millis();

    uint8_t bs[sizeof(tiltData)];
    memcpy(bs, &tiltData, sizeof(tiltData));

    esp_now_send(NULL, bs, sizeof(tiltData)); // NULL means send to all peers
    sent = millis();
    mqttTime = millis();
    
    Serial.println("Data sent, preparing to sleep");
    
    // Clean up ESP-NOW to save power
    esp_now_deinit();
}

// The only difference between "normal" and "calibration"
// is the update frequency. We still deep sleep between samples.
void calibrationMode(bool firstIteration)
{
	readVoltage();
	sleep_interval = CALIBRATION_INTERVAL;
	if (firstIteration)
	{
		calibrationIterations = 1;
	}
	else
	{
		calibrationIterations += 1;
	}
	ESP.rtcUserMemoryWrite(RTC_ADDRESS, &calibrationIterations, sizeof(calibrationIterations));
}

static bool isCalibrationMode()
{
	return (calibrationIterations != 0) ? true : false;
}

void normalMode()
{
	readVoltage();
	Serial.println(voltage);
	bool lowv = !(voltage != 0 && voltage > LOW_VOLTAGE_THRESHOLD);
	if (lowv)
	{
		Serial.println("Voltage below threshold, sleeping longer");
		sleep_interval *= LOW_VOLTAGE_MULTIPLIER;
	}
}

void wifiConnect()
{
    WiFi.forceSleepWake();
    delay(1);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    calibrationWifiStart = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - calibrationWifiStart) < WIFI_TIMEOUT)
    {
        delay(250);
        Serial.print(".");
    }

    Serial.print("\nWiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
}

void checkOTAUpdate()
{
	WiFiClient wifiClient;
	wifiConnect();

	t_httpUpdate_return ret = ESPhttpUpdate.update(wifiClient, OTA_SERVER, OTA_PORT, OTA_PATH, versionTimestamp);
	switch (ret)
	{
	case HTTP_UPDATE_FAILED:
		Serial.println("[OTA] Update failed.");
		break;
	case HTTP_UPDATE_NO_UPDATES:
		Serial.println("[OTA] No update available.");
		break;
	case HTTP_UPDATE_OK:
		Serial.println("[OTA] Update ok."); // may not be called since we reboot the ESP
		break;
	}
}

void setup()
{
	pinMode(led, OUTPUT);
	ledOff();

	Serial.begin(115200);
	Serial.println("Reboot");

	Serial.print("Booting because ");
	Serial.println(ESP.getResetReason());

	Serial.println("Build: " + String(versionTimestamp));

	// Turn off WiFi by default to save power
	WiFi.mode(WIFI_OFF);
	WiFi.forceSleepBegin();

	// INITIALIZE MPU
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

	// Read RTC memory to get current number of calibration iterations.
	ESP.rtcUserMemoryRead(RTC_ADDRESS, &calibrationIterations, sizeof(calibrationIterations));

	rst_info *resetInfo;
	resetInfo = ESP.getResetInfoPtr();
	if (resetInfo->reason != REASON_DEEP_SLEEP_AWAKE)
	{
		int16_t ax, ay, az;
		float tilt;

		calibrationSetupStart = millis();
		while ((millis() - calibrationSetupStart) < CALIBRATION_SETUP_TIME)
		{
			mpu.getAcceleration(&ax, &az, &ay);
			tilt = calculateTilt(ax, az, ay);
			if (tilt > 0.0 && tilt > CALIBRATION_TILT_ANGLE_MIN && tilt < CALIBRATION_TILT_ANGLE_MAX)
			{
				Serial.println("Checking for OTA update...");
				checkOTAUpdate();

				Serial.println("Initiate calibration mode");
				calibrationMode(true);

				break;
			}
			delay(2000);
		}
	}
	else if (isCalibrationMode() && calibrationIterations < CALIBRATION_ITERATIONS)
	{
		Serial.printf("Calibration mode, %d iterations...", calibrationIterations);
		calibrationMode(false);
	}
	else
	{
		Serial.println("Normal mode");
		normalMode();
	}

	currentState = STATE_SAMPLING;
	Serial.println("Finished setup");
}

void loop()
{
    switch (currentState) {
        case STATE_SAMPLING:
            if (sent) {
                currentState = STATE_SLEEPING;
            }
            else if ((millis() - bootTime) > WAKE_TIMEOUT && !isCalibrationMode()) {
                currentState = STATE_SLEEPING;
            }
            else if (nsamples < MAX_SAMPLES && mpu.getIntDataReadyStatus()) {
                int16_t ax, ay, az;
                mpu.getAcceleration(&ax, &az, &ay);

                float tilt = calculateTilt(ax, az, ay);
                
                // Ignore zero readings as well as readings of precisely 90.
                // Both of these indicate failures to read correct data from the MPU.
                if (tilt > 0.0 && tilt != 90) {
                    samples[nsamples++] = tilt;
                }

                if (nsamples >= MAX_SAMPLES) {
                    // As soon as we have all our samples, read the temperature.
                    // This offset is from the MPU documentation. Displays temperature in degrees C.
                    temperature = mpu.getTemperature() / 340.0 + 36.53;
                    
                    // Put the MPU back to sleep immediately after data collection
                    putMpuToSleep();
                    
                    currentState = STATE_PROCESSING;
                }
            }
            
            // mpu.getIntDataReadyStatus() hits the I2C bus. We don't need
			// to poll every ms while we're gathering samples. Once we have
			// the samples we're just waiting for the transmit to clear, so
			// loop a bit quicker.
            delay((nsamples < MAX_SAMPLES) ? 10 : 1);
            break;
            
        case STATE_PROCESSING:
            // Process data and prepare for transmission
            currentState = STATE_TRANSMITTING;
            break;
            
        case STATE_TRANSMITTING:
            // Send sensor data through ESP-NOW
            sendSensorData();
            currentState = STATE_SLEEPING;
            break;
            
        case STATE_SLEEPING:
            // Go to deep sleep
            actuallySleep();
            break;
            
        default:
            // Should never reach here, but just in case reset to sampling state
            currentState = STATE_SAMPLING;
            break;
    }
}