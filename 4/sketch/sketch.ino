/**
 * ESP32-CAM Video Streaming Server
 * 
 * This programme implements a web server on the ESP32-CAM module that provides
 * real-time video streaming capabilities with rotation controls. It supports
 * multiple WiFi networks and includes automatic camera parameter optimisation.
 * 
 * Hardware Requirements:
 * - ESP32-CAM module
 * - Compatible power supply
 * - LED indicator (connected to GPIO 4)
 * 
 * Features:
 * - Multi-network WiFi support with failover
 * - Automatic camera parameter optimisation
 * - Web-based streaming interface with rotation controls
 * - LED status indicator
 * - MJPEG streaming over HTTP
 * - Responsive web interface
 */

#include <WiFi.h>
#include <esp_camera.h>

// WiFi network credentials array.
// Add additional networks for failover support.
const char* wifiNetworks[][2] = {
    {"Network_1", "Password_1"},
    {"Network_2", "Password_2"},
    {"Network_3", "Password_3"},
    // Expand with additional networks as required.
};

// GPIO pin for bulti-in LED.
#define LED_PIN 4

// Pin definitions for the ESP32-CAM module.
// These are hardware-specific and shouldn't be modified
// unless using a different camera module.
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

// Initialise web server on port 80.
WiFiServer webServer(80);  // Main web interface
WiFiServer streamServer(81);  // Dedicated stream server

// Task handles
TaskHandle_t streamTaskHandle = NULL;
TaskHandle_t webTaskHandle = NULL;

// HTML template for the web interface, split into parts for memory efficiency.
// Part 1: Basic HTML structure and CSS for the page layout.
const char HOMEPAGE_PART1[] = "<!DOCTYPE html><html><head>"
"<meta charset=\"UTF-8\">"
"<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\">"
"<title>ESP32-CAM Stream</title>"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
"<style>"
"*{margin:0;padding:0;box-sizing:border-box}"
"body{font-family:Arial,sans-serif;background:#000;height:100vh;overflow:hidden}"
".stream-container{position:relative;width:100vw;height:100vh;display:flex;align-items:center;justify-content:center}"
"#video-stream{max-width:100%;max-height:100%;object-fit:contain;transform:rotate(0deg);transform-origin:center;"
"transition:transform 0.3s cubic-bezier(0.4, 0.0, 0.2, 1)}";

// Part 2: CSS
const char HOMEPAGE_PART2[] = ".controls{position:fixed;bottom:20px;right:20px;padding:15px;background:rgba(0,0,0,0.8);"
"border-radius:15px;opacity:0;transition:opacity 0.3s ease;backdrop-filter:blur(10px)}"
".stream-container:hover .controls{opacity:1}"
".rotation-controls{display:grid;grid-template-columns:repeat(3, 1fr);gap:5px;margin-bottom:10px}"
"button{padding:10px;margin:2px;border:none;border-radius:8px;background:rgba(255,255,255,0.15);"
"color:white;cursor:pointer;font-size:14px;backdrop-filter:blur(5px);transition:all 0.2s}"
"button:hover{background:rgba(255,255,255,0.25);transform:translateY(-1px)}"
"button.active{background:rgba(255,255,255,0.3)}"
".led-control{margin-top:10px;padding-top:10px;border-top:1px solid rgba(255,255,255,0.2)}"
".led-btn{width:100%;background:rgba(255,0,0,0.2)}"
".led-btn.active{background:rgba(0,255,0,0.3)}"
"</style>";

// Part 3: JavaScript with global function definitions
const char HOMEPAGE_PART3[] = "<script>"
"var ledStatus=false;"
"var currentRotation=0;"
"var isRequestPending=false;"
"function rotateVideo(change){"
"    currentRotation=(currentRotation+change+360)%360;"
"    var video=document.getElementById('video-stream');"
"    video.style.transform='rotate('+currentRotation+'deg)';"
"    localStorage.setItem('rotation',currentRotation);"
"    var display=document.getElementById('rotation-display');"
"    if(display){display.textContent=currentRotation+'°';}"
"}"
"function resetRotation(){"
"    currentRotation=0;"
"    var video=document.getElementById('video-stream');"
"    video.style.transform='rotate(0deg)';"
"    localStorage.setItem('rotation','0');"
"    var display=document.getElementById('rotation-display');"
"    if(display){display.textContent='0°';}"
"}"
"function toggleLED(){"
"    if(isRequestPending)return;"
"    isRequestPending=true;"
"    var newState=ledStatus?'0':'1';"
"    var ledBtn=document.getElementById('ledBtn');"
"    if(ledBtn)ledBtn.disabled=true;"
"    fetch('/led?state='+newState,{"
"        method:'GET',"
"        cache:'no-cache',"
"        mode:'cors',"
"        headers:{'Accept':'text/plain'}"
"    })"
"    .then(function(response){"
"        if(!response.ok)throw new Error('Network response was not ok: '+response.status);"
"        return response.text();"
"    })"
"    .then(function(state){"
"        ledStatus=(state==='1');"
"        if(ledBtn){"
"            ledBtn.classList.toggle('active',ledStatus);"
"            ledBtn.textContent=ledStatus?'LED: ON':'LED: OFF';"
"        }"
"    })"
"    .catch(function(error){"
"        console.error('LED error:',error);"
"        if(ledBtn){"
"            ledBtn.style.background='rgba(255,0,0,0.4)';"
"            ledBtn.textContent='ERROR';"
"        }"
"    })"
"    .finally(function(){"
"        isRequestPending=false;"
"        if(ledBtn)ledBtn.disabled=false;"
"        setTimeout(checkLEDState,1000);"
"    });"
"}"
"function checkLEDState(){"
"    if(isRequestPending)return;"
"    isRequestPending=true;"
"    fetch('/led?state=2',{"
"        method:'GET',"
"        cache:'no-cache',"
"        mode:'cors',"
"        headers:{'Accept':'text/plain'}"
"    })"
"    .then(function(response){"
"        if(!response.ok)throw new Error('Network response was not ok: '+response.status);"
"        return response.text();"
"    })"
"    .then(function(state){"
"        ledStatus=(state==='1');"
"        var ledBtn=document.getElementById('ledBtn');"
"        if(ledBtn){"
"            ledBtn.classList.toggle('active',ledStatus);"
"            ledBtn.textContent=ledStatus?'LED: ON':'LED: OFF';"
"        }"
"    })"
"    .catch(function(error){"
"        console.error('LED check error:',error);"
"    })"
"    .finally(function(){"
"        isRequestPending=false;"
"    });"
"}"
"window.onload=function(){"
"    var videoSrc = 'http://' + window.location.hostname + ':81';"
"    document.getElementById('video-stream').src = videoSrc;"
"    var savedRotation=parseInt(localStorage.getItem('rotation'))||0;"
"    currentRotation=savedRotation;"
"    var video=document.getElementById('video-stream');"
"    if(video){"
"        video.style.transform='rotate('+savedRotation+'deg)';"
"        var display=document.getElementById('rotation-display');"
"        if(display){display.textContent=savedRotation+'°';}"
"    }"
"    checkLEDState();"
"    setInterval(checkLEDState,5000);"
"};"
"</script></head><body><div class=\"stream-container\">"
"<img id=\"video-stream\" alt=\"Camera Stream\">"
"<div class=\"controls\">";

const char HOMEPAGE_PART4[] = "<div class=\"rotation-controls\">"
"<button onclick=\"rotateVideo(-45)\">↺ -45°</button>"
"<button onclick=\"resetRotation()\" id=\"rotation-display\">Reset</button>"
"<button onclick=\"rotateVideo(45)\">↻ +45°</button>"
"</div>"
"<div class=\"led-control\">"
"<button id=\"ledBtn\" class=\"led-btn\" onclick=\"toggleLED()\">LED: OFF</button>"
"</div>"
"</div></div></body></html>";

/**
 * Initialises and configures the ESP32-CAM camera module.
 * Sets up resolution based on PSRAM availability and optimises
 * camera parameters for better image quality.
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

    // Optimise camera settings based on PSRAM availability.
    if (psramFound()) {
        config.frame_size = FRAMESIZE_UXGA; // Higher resolution for PSRAM-enabled modules.
        config.jpeg_quality = 10;
        config.fb_count = 2;
    } else {
        config.frame_size = FRAMESIZE_SVGA; // Lower resolution for non-PSRAM modules.
        config.jpeg_quality = 12;
        config.fb_count = 1;
    }

    // Initialise the camera module.
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera initialisation failed with error 0x%x\n", err);
        while (true);
    }

    // Optimise sensor settings if available.
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        // Apply general image improvements.
        s->set_hmirror(s, 1);      // Enable horizontal mirror.
        s->set_vflip(s, 1);        // Enable vertical flip.
        s->set_saturation(s, 2);   // Enhance saturation.
        s->set_brightness(s, 1);   // Adjust brightness.
        s->set_contrast(s, 1);     // Adjust contrast.
        s->set_gainceiling(s, GAINCEILING_8X);  // Increase gain ceiling.
        
        // Configure automatic exposure settings.
        s->set_exposure_ctrl(s, 1); // Enable auto exposure.
        s->set_aec2(s, 1);          // Enable advanced exposure control.
        s->set_aec_value(s, 300);   // Set fallback exposure value.

        // Configure white balance settings.
        s->set_whitebal(s, 1);      // Enable white balance.
        s->set_awb_gain(s, 1);      // Enable automatic white balance gain.
        s->set_wb_mode(s, 0);       // Set automatic white balance mode.
    } else {
        Serial.println("Camera sensor is not available!");
    }
}

/**
 * Handles LED control requests received from the client.
 * Parses the HTTP header to determine the requested LED state,
 * updates the LED state accordingly, and sends a response back to the client.
 * 
 * LED states:
 * - 0: Turn off the LED.
 * - 1: Turn on the LED.
 * - 2: Query the current LED state.
 * 
 * @param client The connected WiFiClient instance.
 * @param header The HTTP request header as a String, containing the LED control command.
 */
void handleLED(WiFiClient client, String header) {
    Serial.println("LED request received");
    Serial.println(header);
    
    int ledState = -1;
    
    // Parse the state parameter
    if (header.indexOf("GET /led?state=1") >= 0) {
        digitalWrite(LED_PIN, HIGH);
        ledState = 1;
    } else if (header.indexOf("GET /led?state=0") >= 0) {
        digitalWrite(LED_PIN, LOW);
        ledState = 0;
    } else if (header.indexOf("GET /led?state=2") >= 0) {
        ledState = digitalRead(LED_PIN);
    }
    
    // Calculate content length first
    String response = String(ledState);
    int contentLength = response.length();
    
    // Send headers
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Access-Control-Allow-Origin: *");
    client.println("Cache-Control: no-cache, no-store, must-revalidate");
    client.printf("Content-Length: %d\r\n", contentLength);
    client.println("Connection: close");
    client.println();  // Important: blank line after headers
    
    // Send body
    client.print(response);
    
    // Ensure everything is sent
    client.flush();
    delay(1);  // Small delay to ensure data is sent
    Serial.printf("LED response sent: %d\n", ledState);
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

        while (WiFi.status() != WL_CONNECTED && attempt < 20) { // Try for ~10 seconds.
            delay(500);
            Serial.print(".");
            attempt++;
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.printf("\nConnected to %s\n", ssid);
            Serial.print("IP Address: ");
            Serial.println(WiFi.localIP());
            return; // Exit once connected.
        } else {
            Serial.printf("\nFailed to connect to %s\n", ssid);
        }
    }

    Serial.println("Failed to connect to any Wi-Fi network. Restarting...");
    ESP.restart();
}

/**
 * FreeRTOS task for handling incoming HTTP client requests.
 * 
 * This task continuously checks for new client connections on the 
 * web server. When a connection is available, it delegates the 
 * processing of the request to `handleWebClient`.
 * 
 * The task runs indefinitely and includes a short delay to prevent
 * watchdog timer resets and reduce CPU usage.
 * 
 * @param parameter Pointer to the task parameter, unused in this implementation.
 */
void webTask(void *parameter) {
    for(;;) {
        WiFiClient client = webServer.available();
        if (client) {
            handleWebClient(client);
        }
        vTaskDelay(1);
    }
}

/**
 * Handles incoming HTTP requests from clients and routes them
 * to appropriate handlers based on the requested URL path.
 * This function processes requests for controlling the LED or serving
 * the homepage and sends the corresponding response.
 * 
 * The function monitors the client connection, reads the HTTP request
 * header, and determines the appropriate action based on its content.
 * 
 * @param client The connected WiFiClient instance handling the request.
 */
void handleWebClient(WiFiClient &client) {
    String header = "";
    unsigned long currentTime = millis();
    unsigned long previousTime = currentTime;
    const long timeoutTime = 2000;
    bool requestComplete = false;

    while (client.connected() && !requestComplete && (millis() - previousTime <= timeoutTime)) {
        if (client.available()) {
            char c = client.read();
            header += c;
            
            if (c == '\n' && header.indexOf("\r\n\r\n") >= 0) {
                if (header.indexOf("GET /led") >= 0) {
                    handleLED(client, header);
                }
                else if (header.indexOf("GET /") >= 0) {
                    size_t totalLength = strlen(HOMEPAGE_PART1) + 
                                       strlen(HOMEPAGE_PART2) + 
                                       strlen(HOMEPAGE_PART3) + 
                                       strlen(HOMEPAGE_PART4);
                    
                    client.println("HTTP/1.1 200 OK");
                    client.println("Content-Type: text/html");
                    client.println("Connection: close");
                    client.printf("Content-Length: %u\r\n", totalLength);
                    client.println();
                    client.print(HOMEPAGE_PART1);
                    client.print(HOMEPAGE_PART2);
                    client.print(HOMEPAGE_PART3);
                    client.print(HOMEPAGE_PART4);
                }
                requestComplete = true;
                break;
            }
        }
    }
    client.stop();
}

/**
 * Streams MJPEG video to the connected client over HTTP.
 * Continuously captures frames from the ESP32 camera and transmits them
 * using multipart content headers to ensure seamless video streaming.
 *
 * This function runs indefinitely as part of a FreeRTOS task.
 * It waits for a client connection, streams video frames, and handles
 * the connection lifecycle.
 *
 * @param parameter Pointer to the task parameter, unused in this implementation.
 */
void streamTask(void *parameter) {
    for(;;) {
        WiFiClient client = streamServer.available();
        if (client) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
            client.println();
            
            while (client.connected()) {
                camera_fb_t *fb = esp_camera_fb_get();
                if (fb) {
                    client.printf("--frame\r\nContent-Type: image/jpeg\r\n"
                                "Content-Length: %u\r\n\r\n",
                                fb->len);
                    client.write(fb->buf, fb->len);
                    client.println();
                    esp_camera_fb_return(fb);
                }
                yield();  // Allow other tasks to run
            }
            client.stop();
        }
        vTaskDelay(1);  // Short delay to prevent watchdog trigger
    }
}

/**
 * Performs the initial setup for the application.
 * Configures the serial communication, LED pin, WiFi connection,
 * camera module, and web servers. Additionally, it creates
 * FreeRTOS tasks for streaming and handling web server requests.
 *
 * Tasks are pinned to specific CPU cores for optimised performance:
 * - StreamTask is pinned to Core 0.
 * - WebTask is pinned to Core 1.
 */
void setup() {
    Serial.begin(115200);

    // Configure LED pin for status indication.
    pinMode(LED_PIN, OUTPUT);

    // However, no initial state is set. Should add:
    digitalWrite(LED_PIN, LOW);  // Set initial state

    // Establish WiFi connection.
    connectToWiFi();

    // Initialise camera module.
    startCamera();

    // Launch web servers.
    webServer.begin();
    streamServer.begin();

    // Create stream task on Core 0
    xTaskCreatePinnedToCore(
        streamTask,       // Task function
        "StreamTask",     // Name
        8192,            // Stack size
        NULL,            // Parameters
        2,               // Priority
        &streamTaskHandle,// Handle
        0                // Core 0
    );

    // Create web server task on Core 1
    xTaskCreatePinnedToCore(
        webTask,         // Task function
        "WebTask",       // Name
        8192,            // Stack size
        NULL,            // Parameters
        1,               // Priority
        &webTaskHandle,  // Handle
        1                // Core 1
    );
}

/**
 * Main programme loop.
 * 
 * This loop is intentionally terminated since the application
 * uses FreeRTOS tasks pinned to specific CPU cores, eliminating
 * the need for Arduino's default `loop` functionality.
 */
void loop() {
    vTaskDelete(NULL);  // Delete Arduino loop task, as we are pinning tasks to cores in the setup.
}
