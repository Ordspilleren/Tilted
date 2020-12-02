#include <ESP8266WiFi.h>
#include <espnow.h>
#include <CREDENTIALS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <DoubleResetDetector.h>
#include <ESP8266HTTPClient.h>
#include <tinyexpr.h>

// Number of seconds after reset during which a
// subseqent reset will be considered a double reset.
#define DRD_TIMEOUT 10

// RTC Memory Address for the DoubleResetDetector to use
#define DRD_ADDRESS 0

DoubleResetDetector drd(DRD_TIMEOUT, DRD_ADDRESS);

String deviceName = "TiltedGateway";

#define RETRY_INTERVAL 5000

// Set up WiFi details
const char *ssid = WIFI_SSID;
const char *password = WIFI_PASS;

// MQTT config
const char *mqtt_server = MQTT_SERVER;
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

#define MQTT_TOPIC "tilted/data"

// the following three settings must match the slave settings
uint8_t mac[] = {0x3A, 0x33, 0x33, 0x33, 0x33, 0x33};
const uint8_t channel = 11;
struct __attribute__((packed)) DataStruct
{
    float tilt;
    float temp;
    int volt;
    long interval;
};

DataStruct tiltData;

String calibrationPolynomial = "0.9833333333176023-0.000007936506823478075 *tilt + 0.00003095238092931853 *tilt*tilt-1.58730158581862e-7 *tilt*tilt*tilt";

String brewfatherURL = "";

// Toggles for integrations
bool enableMQTT = true;
bool enableBrewfather = false;

volatile boolean haveReading = false;

float calculateGravity()
{
    String polynomial = calibrationPolynomial;
    polynomial.replace("tilt", String(tiltData.tilt));
    polynomial.replace(" ", "");

    return te_interp(polynomial.c_str(), 0);
}

void receiveCallBackFunction(uint8_t *senderMac, uint8_t *incomingData, uint8_t len)
{
    memcpy(&tiltData, incomingData, len);
    Serial.printf("Transmitter MacAddr: %02x:%02x:%02x:%02x:%02x:%02x, ", senderMac[0], senderMac[1], senderMac[2], senderMac[3], senderMac[4], senderMac[5]);
    Serial.printf("\nTilt: %.2f, ", tiltData.tilt);
    Serial.printf("\nTemperature: %.2f, ", tiltData.temp);
    Serial.printf("\nVoltage: %d, ", tiltData.volt);
    Serial.printf("\nInterval: %ld, ", tiltData.interval);

    haveReading = true;
}

void initEspNow()
{
    WiFi.mode(WIFI_AP);
    wifi_set_macaddr(SOFTAP_IF, &mac[0]);
    wifi_set_channel(channel);
    WiFi.disconnect();

    Serial.println();
    Serial.println("ESP-Now Receiver");
    Serial.printf("Transmitter mac: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Receiver mac: %s\n", WiFi.softAPmacAddress().c_str());
    if (esp_now_init() != 0)
    {
        Serial.println("ESP_Now init failed...");
        delay(RETRY_INTERVAL);
        ESP.restart();
    }
    esp_now_set_self_role(ESP_NOW_ROLE_COMBO);
    esp_now_register_recv_cb(receiveCallBackFunction);
    Serial.println("Slave ready. Waiting for messages...");
}

void wifiConnect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
    }

    Serial.print("\nWiFi connected, IP address: ");
    Serial.println(WiFi.localIP());
}

void reconnectMQTT()
{
    mqttClient.setServer(mqtt_server, 1883);

    while (!mqttClient.connected())
    {
        if (mqttClient.connect(deviceName.c_str()))
        {
            Serial.println("MQTT connected!");
        }
        else
        {
            Serial.print("failed, rc = ");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            // Wait 5 seconds before retrying
            delay(5000);
        }
    }
}

void publishMQTT()
{
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }

    const size_t capacity = JSON_OBJECT_SIZE(5);
    DynamicJsonDocument doc(capacity);
    
    doc["gravity"] = calculateGravity();
    doc["tilt"] = tiltData.tilt;
    doc["temp"] = tiltData.temp;
    doc["volt"] = tiltData.volt;
    doc["interval"] = tiltData.interval;

    String jsonString;
    serializeJson(doc, jsonString);

    mqttClient.publish(MQTT_TOPIC, jsonString.c_str(), true);
    mqttClient.disconnect();
}

void publishBrewfather()
{
    Serial.println("Sending to Brewfather...");
    const size_t capacity = JSON_OBJECT_SIZE(5);
    DynamicJsonDocument doc(capacity);

    doc["name"] = deviceName;
    doc["temp"] = tiltData.temp;
    doc["temp_unit"] = "C";
    doc["gravity"] = calculateGravity();
    doc["gravity_unit"] = "G";

    String jsonBody;
    serializeJson(doc, jsonBody);

    HTTPClient http;
    http.begin(wifiClient, brewfatherURL);
    http.addHeader("Content-Type", "application/json");
    http.POST(jsonBody);
    http.end();
}

void setup()
{
    Serial.begin(115200);

    if (drd.detectDoubleReset())
    {
        Serial.println("Double Reset Detected");
    }

    initEspNow();
}

void loop()
{
    drd.loop();
    if (haveReading)
    {
        haveReading = false;
        wifiConnect();
        if (enableMQTT)
        {
            publishMQTT();
        }
        if (enableBrewfather)
        {
            publishBrewfather();
        }
        ESP.restart();
    }
}