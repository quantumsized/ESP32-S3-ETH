/*
This one will work eventually. Started 9/28/25
Tried PubSubClient. Might have to go to the other MQTT version that said it was string safe
Working as of 9/28/25
Publishes to MQTT server over Ethernet values from AHT20
*/
#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include "AHT20.h"
#include <EthernetESP32.h> // For ESP32 Ethernet
#include "ESP32MQTTClient.h"
#include "esp_idf_version.h" // check IDF version
#include <WiFi.h>
#include <esp_task_wdt.h>

#if !defined(ESP32)
#  error "This code is for ESP32."
#endif

// --- Network and MQTT settings ---
// Update with your Ethernet module's MAC address
uint8_t mac[6] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}; 
// Update with your MQTT broker settings
char *server = "mqtt://192.168.1.70:1883";
ESP32MQTTClient mqttClient; // all params are set later

// --- Set configuration for AHT20
AHT20 aht20;
#define I2C_SDA 16 // Custom GPIO pins for ESP32-S3-ETH board
#define I2C_SCL 17

// --------- W5500 SPI pins (change to match your board) ---------
// Use output-capable pins for CS; INT must be an input-capable pin.
#define W5500_SCK   13
#define W5500_MISO  12
#define W5500_MOSI  11
#define W5500_CS    14    // DO NOT use 34–39 (input-only) for CS
#define W5500_INT   10
IPAddress ip(192, 168, 1, 67);

// --- Watchdog timeout
// Define the watchdog timeout in seconds
#define WDT_TIMEOUT_SECONDS 32 

// --- MQTT topics ---
const char* topics[] = {
  "walkin/temperature",
  "walkin/humidity",
  "walkin/status"
};
const int numTopics = sizeof(topics) / sizeof(topics[0]);
char *subscribeTopic = "walkin/**";


// --- Variables for the publishing loop ---
long lastPublishMillis = 0;
const long publishInterval = 30000; // Publish every 5 seconds
int startupCount = 0;
int graceTime = 3;

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");

  for (unsigned int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }

  Serial.println();
}

// --- MQTT and Ethernet clients ---
// Create the Ethernet driver for W5500
W5500Driver driver(W5500_CS, W5500_INT);

void setup() {
  Serial.begin(115200);

  Wire.setTimeOut(2000);
  Wire.begin(I2C_SDA, I2C_SCL); //Join I2C bus
  // Current set for 100 kHz which is normal speed
  // Set I2C clock to 400 kHz for Fast Mode
  Wire.setClock(100000);
  Serial.println(__FILE__); // Print out to serial the sketch file name. Useful to remember which version

  if (aht20.begin() == false) {
    Serial.println("AHT20 not detected. Please check wiring. Freezing.");
    while(true) {
      delay(2000);
      //ESP.restart(); // Restart module on fail
    }
  }

  log_i();
  log_i("setup, ESP.getSdkVersion(): ");
  log_i("%s", ESP.getSdkVersion());

  mqttClient.enableDebuggingMessages();

  mqttClient.setURI(server);
  mqttClient.enableLastWillMessage("walkin/status", "Going offline or restarting.");
  mqttClient.setKeepAlive(30);
  mqttClient.setOnMessageCallback([](const std::string &topic, const std::string &payload) {
      log_i("Global callback: %s: %s", topic.c_str(), payload.c_str());
  });


  // Initialize W5500 Ethernet
  Ethernet.init(driver);
  SPI.begin(W5500_SCK, W5500_MISO, W5500_MOSI); // SCK, MISO, MOSI
  // start the Ethernet connection:
  Serial.println("Initialize Ethernet with DHCP:");
  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    // Check for Ethernet hardware present
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      Serial.println("Ethernet shield was not found.  Sorry, can't run without hardware. :(");
      while (true) {
        delay(2000); // do nothing, no point running without Ethernet hardware
      }
    }
    if (Ethernet.linkStatus() == LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    // try to configure using IP address instead of DHCP:
    Ethernet.begin(ip);
  } else {
    Serial.print("  DHCP assigned IP ");
    Serial.println(Ethernet.localIP());
  }

  
  mqttClient.loopStart();

  // Set random seed for client ID
  randomSeed(micros());

  // De-initialize the default watchdog, if it was already initialized
  esp_task_wdt_deinit(); 

  // Configure the TWDT
  esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT_SECONDS * 1000, // Convert seconds to milliseconds
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1, // Subscribe all cores' idle tasks
    .trigger_panic = true // Trigger a panic (and thus a reset) on timeout
  };

  // Initialize the TWDT with the specified configuration
  esp_err_t err = esp_task_wdt_init(&wdt_config);
  if (err != ESP_OK) {
    Serial.printf("Failed to initialize TWDT: %s\n", esp_err_to_name(err));
    while(true); // Halt on error
  }

  // Add the current task (main loop) to the TWDT's watch list
  err = esp_task_wdt_add(NULL); 
  if (err != ESP_OK) {
    Serial.printf("Failed to add current task to TWDT: %s\n", esp_err_to_name(err));
    while(true); // Halt on error
  }

  WiFi.disconnect(true); // Disconnect from any network
  WiFi.mode(WIFI_OFF);   // Turn off the WiFi module
  Serial.printf("TWDT configured with a timeout of %d seconds.\n", WDT_TIMEOUT_SECONDS);
}

int pubCount = 0;

void loop() {

  // Set Watchdog timer to go and feed it
    esp_task_wdt_reset(); 
    delay(1); // Crucial: A small delay after esp_task_wdt_reset() can be necessary for the reset to register properly.
    
  // Check if it's time to publish
  if (millis() - lastPublishMillis > publishInterval) {
    lastPublishMillis = millis();

    // Get AHT20 readings
    float tempC = aht20.getTemperature();
    float tempF = ((tempC*9)/5)+32;
    float humidity = aht20.getHumidity();

    
      
    if(tempC < -10) {
      Serial.println("AHT20 failed to read real number! Rebooting...");
      delay(2000);
      ESP.restart();
    }

    //std::string msg = "Hello: " + std::to_string(pubCount++);

    Serial.println("--- Publishing to multiple topics ---");

    // Loop through the topics and publish a message to each
    for (int i = 0; i < numTopics; i++) {
      char payload[50];
      // Create a unique payload for each topic
      if (strcmp(topics[i], "walkin/temperature") == 0) {
        //float temp = random(20, 30) + (random(0, 100) / 100.0); // Simulate sensor data
        snprintf(payload, sizeof(payload), "%.2f", tempF);
      } else if (strcmp(topics[i], "walkin/humidity") == 0) {
        //float hum = random(40, 60) + (random(0, 100) / 100.0);
        snprintf(payload, sizeof(payload), "%.2f", humidity);
      } else {
        snprintf(payload, sizeof(payload), "ESP32-S3-ETH ** All good");
      }

      Serial.print("Publishing to topic: ");
      Serial.println(topics[i]);
      Serial.print("Payload: ");
      Serial.println(payload);

      //mqttClient.publish(topics[i], payload);
      mqttClient.publish(topics[i], payload, 0, false);
      delay(50); // Small delay between publishes to avoid overwhelming the broker
    }
  }
}

void onMqttConnect(esp_mqtt_client_handle_t client)
{
    if (mqttClient.isMyTurn(client)) // can be omitted if only one client
    {
        mqttClient.subscribe(subscribeTopic, [](const std::string &payload)
                             { log_i("%s: %s", subscribeTopic, payload.c_str()); });

        mqttClient.subscribe("bar/#", [](const std::string &topic, const std::string &payload)
                             { log_i("%s: %s", topic.c_str(), payload.c_str()); });
    }
}

#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(5, 0, 0)
esp_err_t handleMQTT(esp_mqtt_event_handle_t event)
{
    mqttClient.onEventCallback(event);
    return ESP_OK;
}
#else  // IDF CHECK
void handleMQTT(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    auto *event = static_cast<esp_mqtt_event_handle_t>(event_data);
    mqttClient.onEventCallback(event);
}
#endif // // IDF CHECK