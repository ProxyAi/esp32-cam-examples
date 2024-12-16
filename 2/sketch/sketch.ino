#include <WiFi.h>
#include <esp_camera.h>

// Wi-Fi credentials (add more as needed)
const char* wifiNetworks[][2] = {
  {"Network_1", "Password_1"},
  {"Network_2", "Password_2"},
  {"Network_3", "Password_3"},
  // Add more networks here
};

// ESP32-CAM Pin Definitions
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

WiFiServer server(80);

/**
 * @brief Initializes the camera with the necessary configurations
 * 
 * This function configures the camera sensor's pins, resolution, and other settings.
 * It also checks for PSRAM availability and adjusts the frame size accordingly.
 */
void startCamera() {
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
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Adjust for PSRAM availability
  if (psramFound()) {
    config.frame_size = FRAMESIZE_UXGA; // High resolution
    config.jpeg_quality = 10;
    config.fb_count = 2;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  // Camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    while (true);
  }

  // Adjust sensor settings if available
  sensor_t *s = esp_camera_sensor_get();
  if (s != NULL) {
    // General adjustments
    s->set_hmirror(s, 1);      // Horizontal mirror
    s->set_vflip(s, 1);        // Vertical flip
    s->set_saturation(s, 2);   // Increase saturation (range: -2 to 2)
    s->set_brightness(s, 1);   // Adjust brightness (range: -2 to 2)
    s->set_contrast(s, 1);     // Adjust contrast (range: -2 to 2)
    s->set_gainceiling(s, GAINCEILING_8X);  // Increase gain ceiling
    
    // Enable Auto Exposure (AE)
    s->set_exposure_ctrl(s, 1); // Enable auto exposure
    s->set_aec2(s, 1);          // Enable AEC algorithm (Advanced Exposure Control)
    s->set_aec_value(s, 300);   // Set manual exposure value as a fallback (range: 0 to 1200)

    // Enable Auto White Balance (AWB)
    s->set_whitebal(s, 1);      // Enable white balance
    s->set_awb_gain(s, 1);      // Enable AWB gain
    s->set_wb_mode(s, 0);       // Set white balance mode to "Auto"
  } else {
    Serial.println("Camera sensor is not available!");
  }
}

/**
 * @brief Streams MJPEG video to a client over HTTP
 * 
 * This function captures frames from the camera and sends them to a client using the
 * MJPEG format over an HTTP connection.
 * 
 * @param client The client to stream video to
 */
void streamMJPEG(WiFiClient client) {
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
  client.println();

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {
      Serial.println("Camera capture failed");
      continue;
    }

    client.printf("--frame\r\nContent-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", fb->len);
    client.write(fb->buf, fb->len);
    client.println();
    esp_camera_fb_return(fb);

    if (!client.connected()) break;
  }
}

/**
 * @brief Connects the ESP32 to a Wi-Fi network
 * 
 * This function attempts to connect to a list of predefined Wi-Fi networks. It will try each
 * network in sequence until a successful connection is established or all networks fail.
 */
void connectToWiFi() {
  Serial.println("Starting Wi-Fi connection...");

  for (size_t i = 0; i < sizeof(wifiNetworks) / sizeof(wifiNetworks[0]); i++) {
    const char* ssid = wifiNetworks[i][0];
    const char* password = wifiNetworks[i][1];
    Serial.printf("Trying to connect to SSID: %s\n", ssid);

    WiFi.begin(ssid, password);
    int attempt = 0;

    while (WiFi.status() != WL_CONNECTED && attempt < 20) { // 20 attempts (~10 seconds)
      delay(500);
      Serial.print(".");
      attempt++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("\nConnected to %s\n", ssid);
      Serial.print("IP Address: ");
      Serial.println(WiFi.localIP());
      return; // Exit once connected
    } else {
      Serial.printf("\nFailed to connect to %s\n", ssid);
    }
  }

  Serial.println("Failed to connect to any Wi-Fi network. Restarting...");
  ESP.restart();
}

/**
 * @brief Initializes the system: Wi-Fi connection, camera, and server
 * 
 * This function sets up the Wi-Fi connection, starts the camera, and initiates the server.
 */
void setup() {
  Serial.begin(115200);

  // Connect to Wi-Fi
  connectToWiFi();

  // Start the camera
  startCamera();

  // Start the server
  server.begin();
}

/**
 * @brief Main loop to handle incoming client requests
 * 
 * This function continuously listens for incoming client connections, and when a connection
 * is made, it starts streaming MJPEG video to the client.
 */
void loop() {
  WiFiClient client = server.accept();
  if (client) {
    Serial.println("New client connected");
    streamMJPEG(client);
    client.stop();
    Serial.println("Client disconnected");
  }
}
