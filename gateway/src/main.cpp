#include "WiFi.h"
#include <esp_wifi.h>
#include <esp_now.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <tinyexpr.h>
#include <Preferences.h>
#include <InfluxDbClient.h>
#include <Button2.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <CircularBuffer.h>
#include <WebServer.h>

// Button definitions
#define BUTTON_1 35
#define BUTTON_2 0
Button2 btn1(BUTTON_1);
Button2 btn2(BUTTON_2);

// Init display
TFT_eSPI tft = TFT_eSPI(135, 240);

// Preferences
Preferences preferences;

// Config variables
String deviceName = "TiltedGateway";
String wifiSSID = "";
String wifiPassword = "";
String polynomial = "";
String mqttServer = "";
String mqttTopic = "tilted/data";
String brewfatherURL = "";
String influxdbURL = "";
String influxdbOrg = "";
String influxdbBucket = "";
String influxdbToken = "";
String tiltedURL = "";

// AP mode settings
const char* apSSID = "TiltedGateway-Setup";
const char* apPassword = "tilted123";

// Config mode flag
bool configMode = false;

// Web server
WebServer server(80);

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

uint8_t sensorId[6];
DataStruct tiltData;
float tiltGravity = 0;

// Buffer with readings for graph display.
// Can be either tilt value or gravity.
CircularBuffer<float, 24> readingsHistory;

volatile boolean haveReading = false;

// HTML for configuration page
const char CONFIG_HTML[] PROGMEM = R"rawliteral(
    <!DOCTYPE html>
    <html>
    <head>
        <title>Tilted Gateway Configuration</title>
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <style>
            body { font-family: Arial, sans-serif; margin: 0; padding: 20px; }
            .form-group { margin-bottom: 15px; }
            label { display: block; margin-bottom: 5px; }
            input[type="text"], input[type="password"] { width: 100%; padding: 8px; box-sizing: border-box; }
            button { background-color: #4CAF50; color: white; padding: 10px 15px; border: none; cursor: pointer; }
            fieldset { margin-bottom: 20px; }
            .section { margin-bottom: 30px; }
        </style>
    </head>
    <body>
        <h1>Tilted Gateway Configuration</h1>
        <form action="/save" method="post">
            <div class="section">
                <fieldset>
                    <legend>Device Settings</legend>
                    <div class="form-group">
                        <label for="deviceName">Device Name:</label>
                        <input type="text" id="deviceName" name="deviceName" value="%DEVICE_NAME%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>WiFi Settings</legend>
                    <div class="form-group">
                        <label for="wifiSSID">WiFi SSID:</label>
                        <input type="text" id="wifiSSID" name="wifiSSID" value="%WIFI_SSID%">
                    </div>
                    <div class="form-group">
                        <label for="wifiPassword">WiFi Password:</label>
                        <input type="password" id="wifiPassword" name="wifiPassword" value="%WIFI_PASSWORD%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>Calibration</legend>
                    <div class="form-group">
                        <label for="polynomial">Polynomial:</label>
                        <input type="text" id="polynomial" name="polynomial" value="%POLYNOMIAL%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>MQTT Settings</legend>
                    <div class="form-group">
                        <label for="mqttServer">MQTT Server:</label>
                        <input type="text" id="mqttServer" name="mqttServer" value="%MQTT_SERVER%">
                    </div>
                    <div class="form-group">
                        <label for="mqttTopic">MQTT Topic:</label>
                        <input type="text" id="mqttTopic" name="mqttTopic" value="%MQTT_TOPIC%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>Brewfather Settings</legend>
                    <div class="form-group">
                        <label for="brewfatherURL">Brewfather URL:</label>
                        <input type="text" id="brewfatherURL" name="brewfatherURL" value="%BREWFATHER_URL%">
                    </div>
                </fieldset>
            </div>
            
            <div class="section">
                <fieldset>
                    <legend>InfluxDB Settings</legend>
                    <div class="form-group">
                        <label for="influxdbURL">InfluxDB URL:</label>
                        <input type="text" id="influxdbURL" name="influxdbURL" value="%INFLUXDB_URL%">
                    </div>
                    <div class="form-group">
                        <label for="influxdbOrg">InfluxDB Org:</label>
                        <input type="text" id="influxdbOrg" name="influxdbOrg" value="%INFLUXDB_ORG%">
                    </div>
                    <div class="form-group">
                        <label for="influxdbBucket">InfluxDB Bucket:</label>
                        <input type="text" id="influxdbBucket" name="influxdbBucket" value="%INFLUXDB_BUCKET%">
                    </div>
                    <div class="form-group">
                        <label for="influxdbToken">InfluxDB Token:</label>
                        <input type="text" id="influxdbToken" name="influxdbToken" value="%INFLUXDB_TOKEN%">
                    </div>
                </fieldset>
            </div>

            <div class="section">
                <fieldset>
                    <legend>Tilted API Settings</legend>
                    <div class="form-group">
                        <label for="tiltedURL">Tilted API URL:</label>
                        <input type="text" id="tiltedURL" name="tiltedURL" value="%TILTED_URL%">
                    </div>
                </fieldset>
            </div>
            
            <button type="submit">Save Configuration</button>
        </form>
    </body>
    </html>
    )rawliteral";

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
    te_expr *expr = te_compile(polynomial.c_str(), vars, 2, &err);

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

    // Copy the MAC address into the data structure for identification
    memcpy(sensorId, senderMac, 6);

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
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str());

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(250);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("\nWiFi connected, IP address: ");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi connection failed");
    }
}

void reconnectMQTT()
{
    mqttClient.setServer(mqttServer.c_str(), 1883);

    int attempts = 0;
    while (!mqttClient.connected() && attempts < 3) {
        if (mqttClient.connect(deviceName.c_str())) {
            Serial.println("MQTT connected!");
        } else {
            Serial.print("failed, rc = ");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
            attempts++;
        }
    }
}

void publishMQTT()
{
    if (!mqttClient.connected())
    {
        reconnectMQTT();
    }

    if (!mqttClient.connected()) {
        Serial.println("Failed to connect to MQTT server");
        return;
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

    mqttClient.publish(mqttTopic.c_str(), jsonString.c_str(), true);
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
    doc["gravity"] = tiltGravity;
    doc["gravity_unit"] = "G";

    String jsonBody;
    serializeJson(doc, jsonBody);

    HTTPClient http;
    http.begin(wifiClient, brewfatherURL.c_str());
    http.addHeader("Content-Type", "application/json");
    http.POST(jsonBody);
    http.end();
}

void publishInfluxDB()
{
    // Set tags
    influxDataPoint.addTag("name", deviceName.c_str());
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

String macToString(const uint8_t* mac) {
    char macStr[18];
    snprintf(macStr, sizeof(macStr), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(macStr);
}

void publishTilted(const String& apiUrl)
{
    if (apiUrl.isEmpty()) {
        Serial.println("JSON API URL not configured, skipping...");
        return;
    }

    Serial.println("Sending to JSON API...");
    const size_t capacity = JSON_OBJECT_SIZE(3) + JSON_OBJECT_SIZE(5);
    DynamicJsonDocument doc(capacity);

    // Create the nested reading object
    JsonObject reading = doc.createNestedObject("reading");
        
    reading["sensorId"] = macToString(sensorId);
    reading["tilt"] = tiltData.tilt;
    reading["temp"] = tiltData.temp;
    reading["volt"] = tiltData.volt;
    reading["interval"] = tiltData.interval;
    
    // Add gateway identification
    doc["gatewayId"] = WiFi.macAddress();
    doc["gatewayName"] = deviceName;

    String jsonBody;
    serializeJson(doc, jsonBody);

    HTTPClient http;
    http.begin(wifiClient, apiUrl.c_str());
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST(jsonBody);
    
    if (httpResponseCode > 0) {
        Serial.print("JSON API HTTP Response code: ");
        Serial.println(httpResponseCode);
    } else {
        Serial.print("JSON API Error code: ");
        Serial.println(httpResponseCode);
    }
    
    http.end();
}

bool integrationEnabled(String integration) {
    return !integration.isEmpty();
}

// Load settings from Preferences
void loadSettings() {
    preferences.begin("tilted", false);
    
    deviceName = preferences.getString("deviceName", "TiltedGateway");
    wifiSSID = preferences.getString("wifiSSID", "");
    wifiPassword = preferences.getString("wifiPassword", "");
    polynomial = preferences.getString("polynomial", "");
    mqttServer = preferences.getString("mqttServer", "");
    mqttTopic = preferences.getString("mqttTopic", "tilted/data");
    brewfatherURL = preferences.getString("brewfatherURL", "");
    influxdbURL = preferences.getString("influxdbURL", "");
    influxdbOrg = preferences.getString("influxdbOrg", "");
    influxdbBucket = preferences.getString("influxdbBucket", "");
    influxdbToken = preferences.getString("influxdbToken", "");
    tiltedURL = preferences.getString("tiltedURL", "");
    
    preferences.end();
    
    Serial.println("Settings loaded:");
    Serial.println("Device Name: " + deviceName);
    Serial.println("WiFi SSID: " + wifiSSID);
    Serial.println("Polynomial: " + polynomial);
    Serial.println("MQTT Server: " + mqttServer);
    Serial.println("Tilted API URL: " + tiltedURL);
}

// Save settings to Preferences
void saveSettings() {
    preferences.begin("tilted", false);
    
    preferences.putString("deviceName", deviceName);
    preferences.putString("wifiSSID", wifiSSID);
    preferences.putString("wifiPassword", wifiPassword);
    preferences.putString("polynomial", polynomial);
    preferences.putString("mqttServer", mqttServer);
    preferences.putString("mqttTopic", mqttTopic);
    preferences.putString("brewfatherURL", brewfatherURL);
    preferences.putString("influxdbURL", influxdbURL);
    preferences.putString("influxdbOrg", influxdbOrg);
    preferences.putString("influxdbBucket", influxdbBucket);
    preferences.putString("influxdbToken", influxdbToken);
    preferences.putString("tiltedURL", tiltedURL);
    
    preferences.end();
    
    Serial.println("Settings saved");
}

// Replace placeholders in HTML template
String processTemplate() {
    String html = CONFIG_HTML;
    html.replace("%DEVICE_NAME%", deviceName);
    html.replace("%WIFI_SSID%", wifiSSID);
    html.replace("%WIFI_PASSWORD%", wifiPassword);
    html.replace("%POLYNOMIAL%", polynomial);
    html.replace("%MQTT_SERVER%", mqttServer);
    html.replace("%MQTT_TOPIC%", mqttTopic);
    html.replace("%BREWFATHER_URL%", brewfatherURL);
    html.replace("%INFLUXDB_URL%", influxdbURL);
    html.replace("%INFLUXDB_ORG%", influxdbOrg);
    html.replace("%INFLUXDB_BUCKET%", influxdbBucket);
    html.replace("%INFLUXDB_TOKEN%", influxdbToken);
    html.replace("%TILTED_URL%", tiltedURL);
    return html;
}

// Start AP mode and web server
void startConfigMode() {
    WiFi.disconnect();
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    
    Serial.println("AP Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
    
    // Configure web server
    server.on("/", HTTP_GET, []() {
        server.send(200, "text/html", processTemplate());
    });
    
    server.on("/save", HTTP_POST, []() {
        deviceName = server.arg("deviceName");
        wifiSSID = server.arg("wifiSSID");
        wifiPassword = server.arg("wifiPassword");
        polynomial = server.arg("polynomial");
        mqttServer = server.arg("mqttServer");
        mqttTopic = server.arg("mqttTopic");
        brewfatherURL = server.arg("brewfatherURL");
        influxdbURL = server.arg("influxdbURL");
        influxdbOrg = server.arg("influxdbOrg");
        influxdbBucket = server.arg("influxdbBucket");
        influxdbToken = server.arg("influxdbToken");
        tiltedURL = server.arg("tiltedURL");
        
        saveSettings();
        
        server.send(200, "text/html", 
            "<html><head><meta http-equiv='refresh' content='5;url=/'></head>"
            "<body><h1>Configuration Saved</h1>"
            "<p>The device will restart in 5 seconds.</p></body></html>");
            
        delay(5000);
        ESP.restart();
    });
    
    server.begin();
    configMode = true;
}

void button1Pressed(Button2 &btn)
{
    Serial.println("Button pressed, going into config mode...");
    startConfigMode();
}

// Layout constants
#define STATUS_HEIGHT 20
#define GRAPH_HEIGHT ((tft.height() - STATUS_HEIGHT) / 2)
#define DATA_SECTION_Y (STATUS_HEIGHT + GRAPH_HEIGHT)

void screenUpdateVariables(float gravity, float temp) {
    tft.setTextDatum(TC_DATUM);
    tft.setTextPadding(tft.textWidth("11.000", 4));
    tft.drawString((String)gravity, tft.width() / 2, DATA_SECTION_Y + 40, 4);
    tft.drawString((String)temp, tft.width() / 2, DATA_SECTION_Y + 80, 4);
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

void drawGraph() {
    // No point in drawing the graph if we don't have at least two readings.
    if (readingsHistory.size() < 2) {
        return;
    }

    // Clear graph before update, but preserve status bar
    tft.fillRect(0, STATUS_HEIGHT, tft.width(), GRAPH_HEIGHT, TFT_BLACK);

    // Draw rectangle around graph
    tft.drawRect(0, STATUS_HEIGHT, tft.width(), GRAPH_HEIGHT, TFT_WHITE);

    float minValue = readingsHistory[0];
    float maxValue = 0;

    for (int i = 0; i < readingsHistory.size(); i++) {
        if (readingsHistory[i] > maxValue) {
            maxValue = readingsHistory[i];
        }
        if (readingsHistory[i] < minValue) {
            minValue = readingsHistory[i];
        }
    }
    Serial.printf("Min: %f, Max: %f", minValue, maxValue);

    double x, y;
    bool update1 = true;
    double ox = -999, oy = -999; // Force them to be off screen
    for (int i = 0; i < readingsHistory.size(); i++) {
        x = i + 1;
        y = readingsHistory[i];
        // Adjusted to account for status bar
        Trace(tft, x, y, 0, STATUS_HEIGHT + GRAPH_HEIGHT - 10, tft.width(), GRAPH_HEIGHT - 20, 
              1, readingsHistory.size(), minValue, maxValue, ox, oy, update1, TFT_YELLOW);
        Serial.printf("Update %f", x);
    }

    tft.setTextPadding(tft.textWidth("111.000", 2));
    tft.setTextDatum(ML_DATUM);
    tft.drawFloat(readingsHistory.first(), 3, 0, DATA_SECTION_Y + 10, 2);
    tft.setTextDatum(MR_DATUM);
    tft.drawFloat(readingsHistory.last(), 3, tft.width(), DATA_SECTION_Y + 10, 2);
    tft.setTextPadding(0);
}

void prepareScreen() {
    tft.init();
    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);
    
    // Battery indicator outline
    tft.drawRect(tft.width() - 30, 5, 25, 12, TFT_WHITE);
    tft.fillRect(tft.width() - 5, 8, 2, 6, TFT_WHITE);  // Battery tip
    
    tft.setTextDatum(TL_DATUM);
    tft.drawString("Gravity", 0, DATA_SECTION_Y + 25, 2);
    tft.drawString("Temperature", 0, DATA_SECTION_Y + 65, 2);

    // Draw rectangle around graph, adjusted for status bar
    tft.drawRect(0, STATUS_HEIGHT, tft.width(), GRAPH_HEIGHT, TFT_WHITE);
}

// Update the battery indicator based on voltage
void updateBatteryIndicator(int voltage) {
    // Map voltage to a battery percentage (adjust these values for your battery)
    // Assuming 3.0V is empty and 4.2V is full for a LiPo battery
    int percentage = map(constrain(voltage, 2800, 3400), 2800, 3400, 0, 100);
    
    // Determine color based on percentage
    uint16_t batteryColor;
    if (percentage > 70) {
        batteryColor = TFT_GREEN;
    } else if (percentage > 30) {
        batteryColor = TFT_YELLOW;
    } else {
        batteryColor = TFT_RED;
    }
    
    // Clear the previous battery level
    tft.fillRect(tft.width() - 29, 6, 23, 10, TFT_BLACK);
    
    // Draw the new battery level
    int fillWidth = map(percentage, 0, 100, 0, 23);
    tft.fillRect(tft.width() - 29, 6, fillWidth, 10, batteryColor);
    
    // Optionally display percentage
    if (percentage < 20) {
        // Display low battery warning
        tft.setTextDatum(TR_DATUM);
        tft.setTextColor(TFT_RED);
        tft.drawString("Low", tft.width() - 35, 6, 1);
        tft.setTextColor(TFT_WHITE);
    }
}

void saveReading(float reading)
{
    readingsHistory.push(reading);
}

void setup()
{
    Serial.begin(115200);

    btn1.setTapHandler(button1Pressed);

    // Load settings
    loadSettings();

    if (wifiSSID.isEmpty()) {
        startConfigMode();
    } else {
        // Disconnect from AP before initializing ESP-Now.
        // This is needed because IoTWebConf for some reason sets up the AP with init().
        //WiFi.softAPdisconnect(true);
        initEspNow();
    }

    prepareScreen();
}

void loop()
{
    btn1.loop();

    if (configMode)
    {
        server.handleClient();
    }

    if (haveReading)
    {
        haveReading = false;
        tiltGravity = calculateGravity();

        // Update battery indicator with each new reading
        updateBatteryIndicator(tiltData.volt);
        
        screenUpdateVariables(tiltData.tilt, tiltData.temp);
        saveReading(tiltData.tilt);
        drawGraph();
        wifiConnect();
        if (integrationEnabled(tiltedURL))
        {
            publishTilted(tiltedURL);
        }
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