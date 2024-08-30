/*
 * ESP32-CAM Vehicle Number Plate Recognition
 * 
 * Overview:
 * This project uses the ESP32-CAM to capture an image of a vehicle's number plate 
 * and sends it to the Circuit Digest cloud server for recognition. The server 
 * processes the image using machine learning models and returns the recognized 
 * number plate data. The result is displayed on an OLED screen connected to the ESP32-CAM.
 *
 * Features:
 * - Captures an image using the ESP32-CAM.
 * - Sends the captured image to a cloud server via a secure HTTPS connection.
 * - Receives and displays the recognized number plate data.
 * - Uses an OLED display to show status messages and results.
 * - Supports any device with a camera and internet access using the API.
 * 
 * Components Required:
 * - ESP32-CAM Module
 * - OLED Display (SSD1306)
 * - Push Button (Trigger button)
 * - Flashlight (LED)
 *
 * Note:
 * Ensure that the ESP32-CAM is correctly wired, including the I2C pins for the OLED display.
 */
// This code captures an image using the ESP32-CAM and sends it to a server for processing.
// The captured image is uploaded securely via HTTPS, and the response from the server 
// is processed to extract specific information like number plate recognition data.

// Libraries for WiFi, Secure Client, and Camera functionalities
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_camera.h"

/* I2C and OLED Display Includes ------------------------------------------- */
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ESP32-CAM doesn't have dedicated I2C pins, so we define our own
#define I2C_SDA 15
#define I2C_SCL 14
TwoWire I2Cbus = TwoWire(0);

// Display defines
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &I2Cbus, OLED_RESET);

const char* ssid = "xxx";         // Replace "xxx" with your WiFi SSID
const char* password = "xxx";      // Replace "xxx" with your WiFi Password
String serverName = "www.circuitdigest.cloud";  // Replace with your server domain
String serverPath = "/readnumberplate";              // API endpoint path "/readqrcode" "/readnumberplate"
const int serverPort = 443;                     // HTTPS port
String apiKey = "xxx";             // Replace "xxx" with your API key

#define triggerButton 13  // GPIO pin for the trigger button
#define flashLight 4      // GPIO pin for the flashlight
int count = 0;           // Counter for image uploads

WiFiClientSecure client; // Secure client for HTTPS communication

// Camera GPIO pins - adjust based on your ESP32-CAM board
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22


// Function to extract a JSON string value by key
String extractJsonStringValue(const String& jsonString, const String& key) {
  int keyIndex = jsonString.indexOf(key);
  if (keyIndex == -1) {
    return "";
  }

  int startIndex = jsonString.indexOf(':', keyIndex) + 2;
  int endIndex = jsonString.indexOf('"', startIndex);

  if (startIndex == -1 || endIndex == -1) {
    return "";
  }

  return jsonString.substring(startIndex, endIndex);
}

// Function to display text on OLED
void displayText(String text) {
  display.clearDisplay();
  display.setCursor(0, 10);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.print(text);
  display.display();
}

void setup() {
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.begin(115200);
  pinMode(flashLight, OUTPUT);
  pinMode(triggerButton, INPUT);
  digitalWrite(flashLight, LOW);

  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(500);
  }
  Serial.println();
  Serial.print("ESP32-CAM IP Address: ");
  Serial.println(WiFi.localIP());

  // Configure camera
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Adjust frame size and quality based on PSRAM availability
  if (psramFound()) {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 5;  // Lower number means higher quality (0-63)
    config.fb_count = 2;
    Serial.println("PSRAM found");
  } else {
    config.frame_size = FRAMESIZE_CIF;
    config.jpeg_quality = 12;  // Lower number means higher quality (0-63)
    config.fb_count = 1;
  }

  // Initialize camera
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    delay(1000);
    ESP.restart();
  }

  // Initialize I2C with our defined pins
  I2Cbus.begin(I2C_SDA, I2C_SCL, 100000);

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.printf("SSD1306 OLED display failed to initialize.\nCheck that display SDA is connected to pin %d and SCL connected to pin %d\n", I2C_SDA, I2C_SCL);
    while (true);
  }

  // Display initialization messages
  displayText("System Initialization Successful");
  delay(1000);
  displayText("Press Trigger Button \n\nto Start Capturing");
}

void loop() {

  // Check if trigger button is pressed
  if (digitalRead(triggerButton) == HIGH) {
    int status = sendPhoto();
    if (status == -1) {
      displayText("Image Capture Failed");
    } else if (status == -2) {
      displayText("Server Connection Failed");
    }
  }

}

// Function to capture and send photo to the server
int sendPhoto() {
  camera_fb_t* fb = NULL;
  // Turn on flash light and capture image
  // digitalWrite(flashLight, HIGH);
  
  delay(100);
  fb = esp_camera_fb_get();
  delay(100);

  if (!fb) {
    Serial.println("Camera capture failed");
    return -1;
  }

  // Display success message
  displayText("Image Capture Success");
  delay(300);
  // digitalWrite(flashLight, LOW);

  // Connect to server
  Serial.println("Connecting to server:" + serverName);
  displayText("Connecting to server:\n\n" + serverName);
  client.setInsecure();  // Skip certificate validation for simplicity

  if (client.connect(serverName.c_str(), serverPort)) {
    Serial.println("Connection successful!");
    displayText("Connection successful!");
    delay(300);
    displayText("Data Uploading !");

    // Increment count and prepare file name
    count++;
    Serial.println(count);
    String filename = apiKey + ".jpeg";

    // Prepare HTTP POST request
    String head = "--CircuitDigest\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" + filename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
    String tail = "\r\n--CircuitDigest--\r\n";
    uint32_t imageLen = fb->len;
    uint32_t extraLen = head.length() + tail.length();
    uint32_t totalLen = imageLen + extraLen;

    client.println("POST " + serverPath + " HTTP/1.1");
    client.println("Host: " + serverName);
    client.println("Content-Length: " + String(totalLen));
    client.println("Content-Type: multipart/form-data; boundary=CircuitDigest");
    client.println("Authorization:" + apiKey);
    client.println();
    client.print(head);

    // Send image data in chunks
    uint8_t* fbBuf = fb->buf;
    size_t fbLen = fb->len;
    for (size_t n = 0; n < fbLen; n += 1024) {
      if (n + 1024 < fbLen) {
        client.write(fbBuf, 1024);
        fbBuf += 1024;
      } else {
        size_t remainder = fbLen % 1024;
        client.write(fbBuf, remainder);
      }
    }

    client.print(tail);

    // Clean up
    esp_camera_fb_return(fb);
    displayText("Waiting For Response!");

    // Wait for server response
    String response;
    long startTime = millis();
    while (client.connected() && millis() - startTime < 5000) { // Modifify the Waiting time as per the response time
      if (client.available()) {
        char c = client.read();
        response += c;
      }
    }

    // Extract and display NPR data from response
    String NPRData = extractJsonStringValue(response, "\"number_plate\"");
    String imageLink = extractJsonStringValue(response, "\"view_image\"");

    // Serial.print("NPR DATA: ");
    // Serial.println(NPRData);

    // Serial.print("ImageLink: ");
    // Serial.println(imageLink);

    Serial.print("Response: ");
    Serial.println(response);

    displayText("NPR Data:\n\n" + NPRData);

    client.stop();
    esp_camera_fb_return(fb);
    return 0;
  } else {
    Serial.println("Connection to server failed");
    esp_camera_fb_return(fb);
    return -2;
  }
}
