#include <ESP8266WiFi.h>
#include <espnow.h>
#include <CREDENTIALS.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define RETRY_INTERVAL 5000

// Set up static IP and WiFi details
IPAddress ip(10, 3, 3, 6);
IPAddress gateway(10, 3, 3, 1);
IPAddress mask(255, 255, 255, 0);
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

volatile boolean haveReading = false;

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
    WiFi.config(ip, gateway, mask);
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

    String clientId = "Tilted-";
    clientId += String(random(0xffff), HEX);

    while (!mqttClient.connected())
    {
        if (mqttClient.connect(clientId.c_str()))
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

char jsonString[200];
char* jsonSensorData() {
    // Serialize data as JSON before sending.
	StaticJsonDocument<200> doc;
	doc["tilt"] = tiltData.tilt;
	doc["temp"] = tiltData.temp;
	doc["volt"] = tiltData.volt;
	doc["interval"] = tiltData.interval;

	serializeJson(doc, jsonString);

    return jsonString;
}

void publishMQTT()
{
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }

    mqttClient.publish(MQTT_TOPIC, jsonSensorData(), true);
    mqttClient.disconnect();
}

void setup()
{
    Serial.begin(115200);

    initEspNow();
}

void loop()
{
    if (haveReading) {
        haveReading = false;
        wifiConnect();
        reconnectMQTT();
        publishMQTT();
        ESP.restart();
    }
}