#include "WiFi.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <tinyexpr.h>
#include <IotWebConf.h>
#include <InfluxDbClient.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <CircularBuffer.h>

// Button definitions
#define BUTTON_1 35
#define BUTTON_2 0
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

// Init display
TFT_eSPI tft = TFT_eSPI(135, 240);

#define CONFIG_VERSION "tiltedgw1"

const char defaultDeviceName[] = "TiltedGateway";

const char defaultAPPassword[] = "tilted123";

#define DEFAULT_MQTT_TOPIC "tilted/data"

#define STRING_LEN 128

char polynomial[STRING_LEN];
char mqttServer[STRING_LEN];
char mqttTopic[STRING_LEN];
char brewfatherURL[STRING_LEN];
char influxdbURL[STRING_LEN];
char influxdbOrg[STRING_LEN];
char influxdbBucket[STRING_LEN];
char influxdbToken[STRING_LEN];

DNSServer dnsServer;
WebServer server(80);

IotWebConf iotWebConf(defaultDeviceName, &dnsServer, &server, defaultAPPassword, CONFIG_VERSION);
iotwebconf::ParameterGroup calibrationGroup = iotwebconf::ParameterGroup("Calibration", "");
iotwebconf::TextParameter polynomialParam = iotwebconf::TextParameter("Polynomial", "polynomial", polynomial, STRING_LEN);
iotwebconf::ParameterGroup mqttGroup = iotwebconf::ParameterGroup("MQTT", "");
iotwebconf::TextParameter mqttServerParam = iotwebconf::TextParameter("MQTT Server", "mqttserver", mqttServer, STRING_LEN);
iotwebconf::TextParameter mqttTopicParam = iotwebconf::TextParameter("MQTT Topic", "mqtttopic", mqttTopic, STRING_LEN, "text", NULL, DEFAULT_MQTT_TOPIC);
iotwebconf::ParameterGroup brewfatherGroup = iotwebconf::ParameterGroup("Brewfather", "");
iotwebconf::TextParameter brewfatherURLParam = iotwebconf::TextParameter("Brewfather URL", "brewfatherurl", brewfatherURL, STRING_LEN);
iotwebconf::ParameterGroup influxdbGroup = iotwebconf::ParameterGroup("InfluxDB", "");
iotwebconf::TextParameter influxdbURLParam = iotwebconf::TextParameter("InfluxDB URL", "influxdburl", influxdbURL, STRING_LEN);
iotwebconf::TextParameter influxdbOrgParam = iotwebconf::TextParameter("InfluxDB Org", "influxdborg", influxdbOrg, STRING_LEN);
iotwebconf::TextParameter influxdbBucketParam = iotwebconf::TextParameter("InfluxDB Bucket", "influxdbbucket", influxdbBucket, STRING_LEN);
iotwebconf::TextParameter influxdbTokenParam = iotwebconf::TextParameter("InfluxDB Token", "influxdbtoken", influxdbToken, STRING_LEN);

bool configMode = false;

#define RETRY_INTERVAL 5000

// MQTT config
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// InfluxDB
InfluxDBClient influxClient;
Point influxDataPoint("tilted_data");

// the following three settings must match the slave settings
uint8_t mac[] = {0x3A, 0x33, 0x33, 0x33, 0x33, 0x33};
const uint8_t channel = 1;
struct __attribute__((packed)) DataStruct
{
    float tilt;
    float temp;
    int volt;
    long interval;
};

DataStruct tiltData;
float tiltGravity = 0;

// Buffer with readings for graph display.
// Can be either tilt value or gravity.
CircularBuffer<float, 24> readingsHistory;

volatile boolean haveReading = false;

float round3(float value)
{
    return (int)(value * 1000 + 0.5) / 1000.0;
}

float calculateGravity()
{
    double tilt = tiltData.tilt;
    double temp = tiltData.temp;
    float gravity = 0;
    int err;
    te_variable vars[] = {{"tilt", &tilt}, {"temp", &temp}};
    te_expr *expr = te_compile(polynomial, vars, 2, &err);

    if (expr)
    {
        gravity = te_eval(expr);
        te_free(expr);
    }
    else
    {
        Serial.printf("Could not calculate gravity. Parse error at %d\n", err);
    }

    return round3(gravity);
}

void receiveCallBackFunction(const uint8_t *senderMac, const uint8_t *incomingData, int len)
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
    WiFi.softAPdisconnect(true);
    WiFi.disconnect();
    WiFi.mode(WIFI_STA);
    esp_wifi_set_mac(WIFI_IF_STA, &mac[0]);
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
    esp_wifi_set_promiscuous(false);

    Serial.println();
    Serial.println("ESP-Now Receiver");
    Serial.printf("Transmitter mac: %s\n", WiFi.macAddress().c_str());
    Serial.printf("Receiver mac: %s\n", WiFi.softAPmacAddress().c_str());
    if (esp_now_init() != ESP_OK)
    {
        Serial.println("ESP_Now init failed...");
        delay(RETRY_INTERVAL);
        ESP.restart();
    }
    Serial.println(WiFi.channel());
    esp_now_register_recv_cb(receiveCallBackFunction);
    Serial.println("Slave ready. Waiting for messages...");
}

void wifiConnect()
{
    WiFi.mode(WIFI_STA);
    WiFi.begin(iotWebConf.getWifiSsidParameter()->valueBuffer, iotWebConf.getWifiPasswordParameter()->valueBuffer);

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
    mqttClient.setServer(mqttServer, 1883);

    while (!mqttClient.connected())
    {
        if (mqttClient.connect(iotWebConf.getThingName()))
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

    doc["gravity"] = tiltGravity;
    doc["tilt"] = tiltData.tilt;
    doc["temp"] = tiltData.temp;
    doc["volt"] = tiltData.volt;
    doc["interval"] = tiltData.interval;

    String jsonString;
    serializeJson(doc, jsonString);

    mqttClient.publish(mqttTopic, jsonString.c_str(), true);
    mqttClient.disconnect();
}

void publishBrewfather()
{
    Serial.println("Sending to Brewfather...");
    const size_t capacity = JSON_OBJECT_SIZE(5);
    DynamicJsonDocument doc(capacity);

    doc["name"] = iotWebConf.getThingName();
    doc["temp"] = tiltData.temp;
    doc["temp_unit"] = "C";
    doc["gravity"] = tiltGravity;
    doc["gravity_unit"] = "G";

    String jsonBody;
    serializeJson(doc, jsonBody);

    HTTPClient http;
    http.begin(wifiClient, brewfatherURL);
    http.addHeader("Content-Type", "application/json");
    http.POST(jsonBody);
    http.end();
}

void publishInfluxDB()
{
    // Set tags
    influxDataPoint.addTag("name", iotWebConf.getThingName());
    // Add data fields
    influxDataPoint.addField("gravity", tiltGravity, 3);
    influxDataPoint.addField("tilt", tiltData.tilt);
    influxDataPoint.addField("temp", tiltData.temp);
    influxDataPoint.addField("voltage", tiltData.volt);
    influxDataPoint.addField("interval", tiltData.interval);

    if (!influxClient.writePoint(influxDataPoint))
    {
        Serial.print("InfluxDB write failed: ");
        Serial.println(influxClient.getLastErrorMessage());
    }
}

bool integrationEnabled(char *integrationVariable)
{
    return (integrationVariable[0] != '\0') ? true : false;
}

void handleRoot()
{
    // -- Let IotWebConf test and handle captive portal requests.
    if (iotWebConf.handleCaptivePortal())
    {
        // -- Captive portal request were already served.
        return;
    }
    String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
    s += "<title>IotWebConf 01 Minimal</title></head><body>Hello world!";
    s += "Go to <a href='config'>configure page</a> to change settings.";
    s += "</body></html>\n";

    server.send(200, "text/html", s);
}

void configSaved()
{
    Serial.println("Config has been saved, restarting...");
    ESP.restart();
}

void button1Pressed(Button2 &btn)
{
    Serial.println("Button pressed, going into config mode...");
    configMode = true;
    server.on("/", handleRoot);
    server.on("/config", [] { iotWebConf.handleConfig(); });
    server.onNotFound([]() { iotWebConf.handleNotFound(); });
    iotWebConf.setConfigSavedCallback(&configSaved);
    iotWebConf.init();
    iotWebConf.forceApMode(true);
}

void screenUpdateVariables(float gravity, float temp)
{
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth("11.000", 4));
    tft.drawString((String)gravity, tft.width() / 2, tft.height() / 2 + 60, 4);
    tft.drawString((String)temp, tft.width() / 2, tft.height() / 2 + 100, 4);
    tft.setTextPadding(0);
}

void Trace(TFT_eSPI &tft, double x, double y,
           double gx, double gy,
           double w, double h,
           double xlo, double xhi,
           double ylo, double yhi,
           double &ox, double &oy,
           bool &update1, unsigned int color)
{

    //unsigned int gcolor = DKBLUE;   // gcolor = graph grid color
    unsigned int pcolor = color; // pcolor = color of your plotted data

    // initialize old x and old y in order to draw the first point of the graph
    // but save the transformed value
    // note my transform funcition is the same as the map function, except the map uses long and we need doubles
    if (update1)
    {
        update1 = false;

        ox = (x - xlo) * (w) / (xhi - xlo) + gx;
        oy = (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;

        if ((ox < gx) || (ox > gx + w))
        {
            update1 = true;
            return;
        }
        if ((oy < gy - h) || (oy > gy))
        {
            update1 = true;
            return;
        }

        tft.setTextDatum(MR_DATUM);
    }

    // the coordinates are now drawn, plot the data
    // the entire plotting code are these few lines...
    // recall that ox and oy are initialized above
    x = (x - xlo) * (w) / (xhi - xlo) + gx;
    y = (y - ylo) * (gy - h - gy) / (yhi - ylo) + gy;

    if ((x < gx) || (x > gx + w))
    {
        update1 = true;
        return;
    }
    if ((y < gy - h) || (y > gy))
    {
        update1 = true;
        return;
    }

    tft.drawLine(ox, oy, x, y, pcolor);
    // it's up to you but drawing 2 more lines to give the graph some thickness
    tft.drawLine(ox, oy + 1, x, y + 1, pcolor);
    tft.drawLine(ox, oy - 1, x, y - 1, pcolor);
    ox = x;
    oy = y;
}

void drawGraph()
{
    // No point in drawing the graph if we don't have at least two readings.
    if (readingsHistory.size() < 2)
    {
        return;
    }

    // Clear graph before update.
    tft.fillRect(0, 0, tft.width(), tft.height() / 2, TFT_BLACK);

    // Draw rectangle around graph
    tft.drawRect(0, 0, tft.width(), tft.height() / 2, TFT_WHITE);

    float minValue = readingsHistory[0];
    float maxValue = 0;

    for (int i = 0; i < readingsHistory.size(); i++)
    {
        if (readingsHistory[i] > maxValue)
        {
            maxValue = readingsHistory[i];
        }
        if (readingsHistory[i] < minValue)
        {
            minValue = readingsHistory[i];
        }
    }
    Serial.printf("Min: %f, Max: %f", minValue, maxValue);

    double x, y;
    bool update1 = true;
    double ox = -999, oy = -999; // Force them to be off screen
    for (int i = 0; i < readingsHistory.size(); i++)
    {
        x = i + 1;
        y = readingsHistory[i];
        Trace(tft, x, y, 0, tft.height() / 2 - 10, tft.width(), tft.height() / 2 - 20, 1, readingsHistory.size(), minValue, maxValue, ox, oy, update1, TFT_YELLOW);
        Serial.printf("Update %f", x);
    }

    tft.setTextPadding(tft.textWidth("111.000", 2));
    tft.setTextDatum(ML_DATUM);
    tft.drawFloat(readingsHistory.first(), 3, 0, tft.height() / 2 + 10, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawFloat(readingsHistory.last(), 3, tft.width(), tft.height() / 2 + 10, 2);
    tft.setTextPadding(0);
}

void prepareScreen()
{
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    tft.setTextDatum(TL_DATUM);
    tft.drawString("Gravity", 0, tft.height() / 2 + 45, 2);
    tft.drawString("Temperature", 0, tft.height() / 2 + 85, 2);

    // Draw rectangle around graph
    tft.drawRect(0, 0, tft.width(), tft.height() / 2, TFT_WHITE);
}

void saveReading(float reading)
{
    readingsHistory.push(reading);
}

void setup()
{
    Serial.begin(115200);

    btn1.setTapHandler(button1Pressed);

    calibrationGroup.addItem(&polynomialParam);
    mqttGroup.addItem(&mqttServerParam);
    mqttGroup.addItem(&mqttTopicParam);
    brewfatherGroup.addItem(&brewfatherURLParam);
    influxdbGroup.addItem(&influxdbURLParam);
    influxdbGroup.addItem(&influxdbOrgParam);
    influxdbGroup.addItem(&influxdbBucketParam);
    influxdbGroup.addItem(&influxdbTokenParam);

    iotWebConf.addParameterGroup(&calibrationGroup);
    iotWebConf.addParameterGroup(&mqttGroup);
    iotWebConf.addParameterGroup(&brewfatherGroup);
    iotWebConf.addParameterGroup(&influxdbGroup);

    iotWebConf.loadConfig();

    // Disconnect from AP before initializing ESP-Now.
    // This is needed because IoTWebConf for some reason sets up the AP with init().
    //WiFi.softAPdisconnect(true);
    initEspNow();

    prepareScreen();
}

void loop()
{
    btn1.loop();

    if (configMode)
    {
        iotWebConf.doLoop();
    }

    if (haveReading)
    {
        haveReading = false;
        tiltGravity = calculateGravity();
        screenUpdateVariables(tiltData.tilt, tiltData.temp);
        saveReading(tiltData.tilt);
        drawGraph();
        wifiConnect();
        if (integrationEnabled(mqttServer))
        {
            publishMQTT();
        }
        if (integrationEnabled(brewfatherURL))
        {
            publishBrewfather();
        }
        if (integrationEnabled(influxdbURL))
        {
            influxClient.setConnectionParams(influxdbURL, influxdbOrg, influxdbBucket, influxdbToken);
            publishInfluxDB();
        }
        initEspNow();
    }
}