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

// MQ Sensor Configuration (Analog Pin - Use ADC1 pins only: 32-39)
// NOTE: ADC2 pins (0,2,4,12-15,25-27) don't work with WiFi!
#define MQ_PIN 34  // GPIO34 is ADC1_CH6 - Safe to use with WiFi

// Send data every 12 seconds
const unsigned long SEND_INTERVAL = 12000;

// WiFi Channel (1-13, match with receiver)
#define WIFI_CHANNEL 1

// Maximum retry attempts for ESP-NOW initialization
#define MAX_INIT_RETRIES 3

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
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
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
  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  
  // Check if DHT reading failed
  if (isnan(temp) || isnan(hum)) {
    Serial.println("⚠️  DHT22 Read Failed! Using previous values or defaults.");
    // Keep previous values if available, otherwise use defaults
    if (sensorData.temperature == 0.0) {
      sensorData.temperature = 25.0;  // Default room temperature
      sensorData.humidity = 50.0;      // Default humidity
    }
  } else {
    sensorData.temperature = temp;
    sensorData.humidity = hum;
  }
  
  // Read MQ Gas Sensor (0-4095 for 12-bit ADC)
  int mqRaw = analogRead(MQ_PIN);
  
  // Validate ADC reading
  if (mqRaw < 0 || mqRaw > 4095) {
    Serial.println("⚠️  Invalid MQ sensor reading!");
    sensorData.mq_value = 0;
  } else {
    sensorData.mq_value = mqRaw;
  }
  
  // Simulated Heart Rate and SpO2 (replace with actual MAX30102 sensor if available)
  // Realistic ranges: Heart Rate (60-100 bpm), SpO2 (95-100%)
  sensorData.heartRate = 72.0 + (random(-10, 11) / 2.0);  // 67-77 bpm range
  sensorData.spo2 = 97.5 + (random(-5, 6) / 2.0);         // 95-100% range
  
  // Clamp SpO2 to realistic range
  if (sensorData.spo2 > 100.0) sensorData.spo2 = 100.0;
  if (sensorData.spo2 < 90.0) sensorData.spo2 = 90.0;
  
  // Add timestamp
  sensorData.timestamp = millis();
  
  // Add MAC address
  String macStr = getMacAddress();
  macStr.toCharArray(sensorData.mac, 18);
  
  // Print readings to Serial Monitor
  Serial.println("\n========== SENSOR READINGS ==========");
  Serial.printf("🌡️  Temperature : %.2f °C\n", sensorData.temperature);
  Serial.printf("💧 Humidity    : %.2f %%\n", sensorData.humidity);
  Serial.printf("🌫️  Gas Level   : %d (Raw ADC)\n", sensorData.mq_value);
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
    Serial.printf("   Total Success: %d | Failures: %d\n", successCount, failureCount);
  } else {
    espNowConnected = false;
    failureCount++;
    Serial.println("❌ Delivery Failed");
    Serial.printf("   Total Success: %d | Failures: %d\n", successCount, failureCount);
    
    // Attempt reconnection if multiple failures
    if (failureCount % 5 == 0) {
      Serial.println("⚠️  Multiple failures detected. Attempting ESP-NOW restart...");
      esp_now_deinit();
      delay(1000);
      initESPNow();
    }
  }
}

/**
 * @brief Initialize ESP-NOW protocol
 */
bool initESPNow() {
  // Set device as Wi-Fi Station
  WiFi.mode(WIFI_STA);
  
  // Disconnect from any AP first
  WiFi.disconnect();
  delay(100);
  
  // Set WiFi channel
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);
  
  Serial.println("\n📡 Initializing ESP-NOW...");
  Serial.printf("   WiFi Channel: %d\n", WIFI_CHANNEL);
  
  // Initialize ESP-NOW
  esp_err_t initResult = esp_now_init();
  if (initResult != ESP_OK) {
    Serial.printf("❌ ESP-NOW Initialization Failed! Error: 0x%X\n", initResult);
    espNowConnected = false;
    return false;
  }
  
  Serial.println("✅ ESP-NOW Initialized Successfully");
  
  // Register send callback
  esp_err_t callbackResult = esp_now_register_send_cb(OnDataSent);
  if (callbackResult != ESP_OK) {
    Serial.printf("❌ Failed to register send callback! Error: 0x%X\n", callbackResult);
    return false;
  }
  
  // Register peer (receiver)
  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, serverAddress, 6);
  peerInfo.channel = WIFI_CHANNEL;  // Must match the channel set above
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;
  
  // Check if peer already exists
  if (esp_now_is_peer_exist(serverAddress)) {
    Serial.println("⚠️  Peer already exists, removing...");
    esp_now_del_peer(serverAddress);
    delay(100);
  }
  
  // Add peer
  esp_err_t addPeerResult = esp_now_add_peer(&peerInfo);
  if (addPeerResult != ESP_OK) {
    Serial.printf("❌ Failed to Add Peer! Error: 0x%X\n", addPeerResult);
    espNowConnected = false;
    return false;
  }
  
  espNowConnected = true;
  Serial.println("✅ Peer Added Successfully");
  
  // Print server MAC
  char serverMAC[18];
  snprintf(serverMAC, sizeof(serverMAC), "%02X:%02X:%02X:%02X:%02X:%02X",
           serverAddress[0], serverAddress[1], serverAddress[2],
           serverAddress[3], serverAddress[4], serverAddress[5]);
  Serial.printf("   Server MAC: %s\n", serverMAC);
  
  return true;
}

/**
 * @brief Send sensor data via ESP-NOW to receiver
 */
void sendData() {
  if (!espNowConnected) {
    Serial.println("⚠️  ESP-NOW not connected! Skipping transmission...");
    return;
  }
  
  Serial.println("\n📤 Sending data to receiver...");
  
  esp_err_t result = esp_now_send(serverAddress, (uint8_t *)&sensorData, sizeof(sensorData));
  
  if (result == ESP_OK) {
    Serial.println("✅ Data queued for transmission");
  } else {
    Serial.printf("❌ Error sending data (Error code: 0x%X)\n", result);
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
  
  // Initialize random seed for simulated sensor data
  randomSeed(analogRead(0));
  
  // Initialize DHT22 sensor
  dht.begin();
  Serial.println("✅ DHT22 Sensor Initialized");
  
  // Wait for DHT22 to stabilize
  Serial.println("⏳ Waiting for DHT22 to stabilize (2 seconds)...");
  delay(2000);
  
  // Configure MQ sensor pin
  pinMode(MQ_PIN, INPUT);
  
  // Configure ADC resolution (12-bit by default)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);  // Full range: 0-3.3V
  
  Serial.println("✅ MQ Sensor Pin Configured (ADC1_CH6)");
  Serial.println("   Note: Using GPIO34 (ADC1) - Safe with WiFi");
  
  // Print device MAC address
  Serial.println("\n📱 Device Information:");
  Serial.printf("   MAC Address: %s\n", getMacAddress().c_str());
  Serial.printf("   WiFi Channel: %d\n", WIFI_CHANNEL);
  
  // Initialize ESP-NOW with retry logic
  bool initSuccess = false;
  for (int attempt = 1; attempt <= MAX_INIT_RETRIES; attempt++) {
    Serial.printf("\n🔄 ESP-NOW Init Attempt %d/%d\n", attempt, MAX_INIT_RETRIES);
    
    if (initESPNow()) {
      initSuccess = true;
      break;
    }
    
    if (attempt < MAX_INIT_RETRIES) {
      Serial.println("⏳ Retrying in 2 seconds...");
      delay(2000);
    }
  }
  
  if (!initSuccess) {
    Serial.println("\n❌❌❌ ESP-NOW INITIALIZATION FAILED AFTER ALL RETRIES ❌❌❌");
    Serial.println("Please check:");
    Serial.println("  1. Server MAC address is correct");
    Serial.println("  2. Receiver is powered on and initialized");
    Serial.println("  3. Both devices use the same WiFi channel");
    Serial.println("\n⚠️  Device will continue but data transmission will fail!");
  }
  
  Serial.println("\n========================================");
  Serial.println(initSuccess ? "   SENDER READY!" : "   SENDER RUNNING (ESP-NOW FAILED)");
  Serial.println("========================================");
  Serial.printf("\n⏱️  Sending data every %lu seconds\n\n", SEND_INTERVAL / 1000);
  
  // Initialize sensor data structure with defaults
  sensorData.temperature = 0.0;
  sensorData.humidity = 0.0;
  sensorData.mq_value = 0;
  sensorData.heartRate = 0.0;
  sensorData.spo2 = 0.0;
  
  // Perform initial sensor reading
  Serial.println("📊 Performing initial sensor reading...");
  readSensors();
}

// ==================== LOOP ====================

void loop() {
  unsigned long currentTime = millis();
  
  // Handle millis() overflow (occurs after ~49 days)
  if (currentTime < lastSendTime) {
    lastSendTime = currentTime;
  }
  
  // Check if it's time to send data
  if (currentTime - lastSendTime >= SEND_INTERVAL) {
    lastSendTime = currentTime;
    
    // Read all sensors
    readSensors();
    
    // Send data via ESP-NOW
    sendData();
    
    // Print connection status
    Serial.printf("\n📊 Connection Status: %s\n", 
                  espNowConnected ? "✅ Connected" : "❌ Disconnected");
  }
  
  // Small delay for stability and to prevent watchdog reset
  delay(50);
}
