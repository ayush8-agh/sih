#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <DHT.h>

// ==================== CONFIGURATION ====================

// REPLACE with your Receiver ESP32's MAC Address
uint8_t serverAddress[] = {0xB8, 0xD6, 0x1A, 0xA7, 0x66, 0x88};

// DHT22 Sensor Configuration
#define DHTPIN 4
#define DHTTYPE DHT22

// MQ Sensor Configuration (Analog Pin)
#define MQ_PIN 34

// Send data every 12 seconds
const unsigned long SEND_INTERVAL = 12000;

// ==================== DATA STRUCTURE ====================

typedef struct sensor_data {
  float temperature;
  float humidity;
  int mq_value;
  float heartRate;
  float spo2;
  char mac[18];
  unsigned long timestamp;
} sensor_data;

// ==================== GLOBAL VARIABLES ====================

DHT dht(DHTPIN, DHTTYPE);
sensor_data sensorData;

bool espNowConnected = false;
unsigned long lastSendTime = 0;
int successCount = 0;
int failureCount = 0;

// ==================== HELPER FUNCTIONS ====================

/**
 * @brief Gets the MAC address of this ESP32 as a formatted string
 */
String getMacAddress() {
  uint8_t mac[6];
  WiFi.macAddress(mac);
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  return String(macStr);
}

/**
 * @brief Reads all sensors and updates the sensorData structure
 */
void readSensors() {
  // Read DHT22 Temperature & Humidity
  sensorData.temperature = dht.readTemperature();
  sensorData.humidity = dht.readHumidity();
  
  // Check if DHT reading failed
  if (isnan(sensorData.temperature) || isnan(sensorData.humidity)) {
    Serial.println("⚠️  DHT22 Read Failed! Using 0.0 as default.");
    sensorData.temperature = 0.0;
    sensorData.humidity = 0.0;
  }
  
  // Read MQ Gas Sensor (0-4095 for 12-bit ADC)
  sensorData.mq_value = analogRead(MQ_PIN);
  
  // Simulated Heart Rate and SpO2 (replace with actual sensor if available)
  sensorData.heartRate = 72.0 + random(-5, 5);
  sensorData.spo2 = 98.0 + random(-2, 2);
  
  // Add timestamp
  sensorData.timestamp = millis();
  
  // Add MAC address
  String macStr = getMacAddress();
  macStr.toCharArray(sensorData.mac, 18);
  
  // Print readings to Serial Monitor
  Serial.println("\n========== SENSOR READINGS ==========");
  Serial.printf("🌡️  Temperature : %.2f °C\n", sensorData.temperature);
  Serial.printf("💧 Humidity    : %.2f %%\n", sensorData.humidity);
  Serial.printf("🌫️  Gas Level   : %d PPM\n", sensorData.mq_value);
  Serial.printf("❤️  Heart Rate  : %.2f bpm\n", sensorData.heartRate);
  Serial.printf("🩺 SpO2        : %.2f %%\n", sensorData.spo2);
  Serial.printf("📱 MAC Address : %s\n", sensorData.mac);
  Serial.printf("⏱️  Timestamp   : %lu ms\n", sensorData.timestamp);
  Serial.println("=====================================");
}

// ==================== ESP-NOW FUNCTIONS ====================

/**
 * @brief Callback function when data is sent via ESP-NOW
 */
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("\n📤 Send Status: ");
  
  if (status == ESP_NOW_SEND_SUCCESS) {
    espNowConnected = true;
    successCount++;
    Serial.println("✅ Delivery Success");
    Serial.printf("   Total Success: %d\n", successCount);
  } else {
    espNowConnected = false;
    failureCount++;
    Serial.println("❌ Delivery Failed");
    Serial.printf("   Total Failures: %d\n", failureCount);
  }
}

/**
 * @brief Initialize ESP-NOW protocol
 */
void initESPNow() {
  // Set device as Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Disconnect from any AP first
  WiFi.disconnect();
  delay(100);
  
  Serial.println("\n📡 Initializing ESP-NOW...");
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("❌ ESP-NOW Initialization Failed!");
    espNowConnected = false;
    return;
  }
  
  Serial.println("✅ ESP-NOW Initialized Successfully");
  
  // Register send callback
  esp_now_register_send_cb(OnDataSent);
  
  // Register peer (receiver)
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, serverAddress, 6);
  peerInfo.channel = 0;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("❌ Failed to Add Peer!");
    espNowConnected = false;
    return;
  }
  
  espNowConnected = true;
  Serial.println("✅ Peer Added Successfully");
  
  // Print server MAC
  char serverMAC[18];
  snprintf(serverMAC, sizeof(serverMAC), "%02X:%02X:%02X:%02X:%02X:%02X",
           serverAddress[0], serverAddress[1], serverAddress[2],
           serverAddress[3], serverAddress[4], serverAddress[5]);
  Serial.printf("   Server MAC: %s\n", serverMAC);
}

/**
 * @brief Send sensor data via ESP-NOW to receiver
 */
void sendData() {
  Serial.println("\n📤 Sending data to receiver...");
  
  esp_err_t result = esp_now_send(serverAddress, (uint8_t *)&sensorData, sizeof(sensorData));
  
  if (result == ESP_OK) {
    Serial.println("✅ Data queued for transmission");
  } else {
    Serial.printf("❌ Error sending data (Error code: %d)\n", result);
    failureCount++;
  }
}

// ==================== SETUP ====================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n========================================");
  Serial.println("   ESP32 SENDER - DATA LOGGER");
  Serial.println("========================================\n");
  
  // Initialize DHT22 sensor
  dht.begin();
  Serial.println("✅ DHT22 Sensor Initialized");
  
  // Configure MQ sensor pin
  pinMode(MQ_PIN, INPUT);
  Serial.println("✅ MQ Sensor Pin Configured");
  
  // Print device MAC address
  Serial.println("\n📱 Device Information:");
  Serial.printf("   MAC Address: %s\n", getMacAddress().c_str());
  
  // Initialize ESP-NOW
  initESPNow();
  
  Serial.println("\n========================================");
  Serial.println("   SENDER READY!");
  Serial.println("========================================");
  Serial.printf("\n⏱️  Sending data every %lu seconds\n\n", SEND_INTERVAL / 1000);
}

// ==================== LOOP ====================

void loop() {
  unsigned long currentTime = millis();
  
  // Check if it's time to send data
  if (currentTime - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentTime;
    
    // Read all sensors
    readSensors();
    
    // Send data via ESP-NOW
    sendData();
  }
  
  // Small delay for stability
  delay(50);
}
