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
#include "ESP32_OV5640_AF.h"

// WiFi network credentials array.
// Add additional networks for failover support.
const char* wifiNetworks[][2] = {
    {"Go Go Gadget Shed", "GiveMeInternet"},
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
TaskHandle_t focusTaskHandle = NULL;

// OV5640 instance
OV5640 ov5640 = OV5640();

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


const char HOMEPAGE_PART2[] = ".controls{position:fixed;bottom:20px;right:20px;padding:15px;background:rgba(0,0,0,0.8);"
"border-radius:15px;opacity:0;transition:opacity 0.3s ease;backdrop-filter:blur(10px)}"
".stream-container:hover .controls{opacity:1}"
".rotation-controls{display:grid;grid-template-columns:repeat(3, 1fr);gap:5px;margin-bottom:10px}"
"button{padding:10px;margin:2px;border:none;border-radius:8px;background:rgba(255,255,255,0.15);"
"color:white;cursor:pointer;font-size:14px;backdrop-filter:blur(5px);transition:all 0.2s}"
"button:hover{background:rgba(255,255,255,0.25);transform:translateY(-1px)}"
"button.active{background:rgba(255,255,255,0.3)}"
".advanced-controls{display:none;margin-top:10px;padding-top:10px;border-top:1px solid rgba(255,255,255,0.2)}"
".advanced-controls.visible{display:block}"
".control-group{margin-bottom:10px}"
".control-group label{display:block;color:white;margin-bottom:5px;font-size:0.9em}"
".control-row{display:flex;align-items:center;gap:10px;margin-bottom:5px}"
".control-row input[type=\"range\"]{flex:1}"
".control-row input[type=\"number\"]{width:60px;background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);"
"color:white;padding:2px 5px;border-radius:4px}"
".control-row select{background:rgba(255,255,255,0.1);border:1px solid rgba(255,255,255,0.2);color:white;"
"padding:2px 5px;border-radius:4px;width:100%}"
".led-control{margin-top:10px;padding-top:10px;border-top:1px solid rgba(255,255,255,0.2)}"
".led-btn{width:100%;background:rgba(255,0,0,0.2)}"
".led-btn.active{background:rgba(0,255,0,0.3)}"
"</style>";

// Part 3: JavaScript with global function definitions
const char HOMEPAGE_PART3[] = "<script>"
"var ledStatus=false;"
"var currentRotation=0;"
"var isRequestPending=false;"
"function debounce(func, wait) {"
"    let timeout;"
"    return function executedFunction(...args) {"
"        const later = () => {"
"            clearTimeout(timeout);"
"            func(...args);"
"        };"
"        clearTimeout(timeout);"
"        timeout = setTimeout(later, wait);"
"    };"
"}"
"const debouncedUpdateSetting = debounce((setting, value) => {"
"    if(isRequestPending) return;"
"    isRequestPending = true;"
"    fetch('/camera?' + setting + '=' + value, {"
"        method: 'GET',"
"        cache: 'no-cache',"
"        mode: 'cors',"
"        headers: {'Accept': 'text/plain'}"
"    })"
"    .then(response => {"
"        if(!response.ok) throw new Error('Network response was not ok: ' + response.status);"
"        return response.text();"
"    })"
"    .then(result => {"
"        console.log('Camera setting updated:', setting, '=', result);"
"        if(result === '-1') throw new Error('Camera setting update failed');"
"    })"
"    .catch(error => {"
"        console.error('Camera setting error:', error);"
"    })"
"    .finally(() => {"
"        isRequestPending = false;"
"    });"
"}, 300);"
"function updateCameraSetting(setting, value) {"
"    debouncedUpdateSetting(setting, value);"
"}"
"function validateAndUpdate(element, setting, min, max) {"
"    let value = parseInt(element.value);"
"    if(isNaN(value)) value = 0;"
"    value = Math.max(min, Math.min(max, value));"
"    element.value = value;"
"    updateCameraSetting(setting, value);"
"}"
"function toggleAdvancedControls() {"
"    const controls = document.querySelector('.advanced-controls');"
"    controls.classList.toggle('visible');"
"    localStorage.setItem('advancedControlsVisible', controls.classList.contains('visible'));"
"}"
"function rotateVideo(change) {"
"    currentRotation = (currentRotation + change + 360) % 360;"
"    var video = document.getElementById('video-stream');"
"    video.style.transform = 'rotate(' + currentRotation + 'deg)';"
"    localStorage.setItem('rotation', currentRotation);"
"    var display = document.getElementById('rotation-display');"
"    if(display) { display.textContent = currentRotation + '°'; }"
"}"
"function resetRotation() {"
"    currentRotation = 0;"
"    var video = document.getElementById('video-stream');"
"    video.style.transform = 'rotate(0deg)';"
"    localStorage.setItem('rotation', '0');"
"    var display = document.getElementById('rotation-display');"
"    if(display) { display.textContent = '0°'; }"
"}"
"function toggleLED() {"
"    if(isRequestPending) return;"
"    isRequestPending = true;"
"    var newState = ledStatus ? '0' : '1';"
"    var ledBtn = document.getElementById('ledBtn');"
"    if(ledBtn) ledBtn.disabled = true;"
"    fetch('/led?state=' + newState, {"
"        method: 'GET',"
"        cache: 'no-cache',"
"        mode: 'cors',"
"        headers: {'Accept': 'text/plain'}"
"    })"
"    .then(function(response) {"
"        if(!response.ok) throw new Error('Network response was not ok: ' + response.status);"
"        return response.text();"
"    })"
"    .then(function(state) {"
"        ledStatus = (state === '1');"
"        if(ledBtn) {"
"            ledBtn.classList.toggle('active', ledStatus);"
"            ledBtn.textContent = ledStatus ? 'LED: ON' : 'LED: OFF';"
"        }"
"    })"
"    .catch(function(error) {"
"        console.error('LED error:', error);"
"        if(ledBtn) {"
"            ledBtn.style.background = 'rgba(255,0,0,0.4)';"
"            ledBtn.textContent = 'ERROR';"
"        }"
"    })"
"    .finally(function() {"
"        isRequestPending = false;"
"        if(ledBtn) ledBtn.disabled = false;"
"        setTimeout(checkLEDState, 1000);"
"    });"
"}"
"function checkLEDState() {"
"    if(isRequestPending) return;"
"    isRequestPending = true;"
"    fetch('/led?state=2', {"
"        method: 'GET',"
"        cache: 'no-cache',"
"        mode: 'cors',"
"        headers: {'Accept': 'text/plain'}"
"    })"
"    .then(function(response) {"
"        if(!response.ok) throw new Error('Network response was not ok: ' + response.status);"
"        return response.text();"
"    })"
"    .then(function(state) {"
"        ledStatus = (state === '1');"
"        var ledBtn = document.getElementById('ledBtn');"
"        if(ledBtn) {"
"            ledBtn.classList.toggle('active', ledStatus);"
"            ledBtn.textContent = ledStatus ? 'LED: ON' : 'LED: OFF';"
"        }"
"    })"
"    .catch(function(error) {"
"        console.error('LED check error:', error);"
"    })"
"    .finally(function() {"
"        isRequestPending = false;"
"    });"
"}"
"function refreshSettings() {"
"    const refreshBtn = document.getElementById('refreshBtn');"
"    if(refreshBtn) {"
"        refreshBtn.disabled = true;"
"        refreshBtn.textContent = 'Refreshing...';"
"    }"
"    fetch('/camera/status', {"
"        method: 'GET',"
"        cache: 'no-cache',"
"        mode: 'cors',"
"        headers: {'Accept': 'application/json'}"
"    })"
"    .then(response => {"
"        if(!response.ok) throw new Error('Network response was not ok: ' + response.status);"
"        return response.json();"
"    })"
"    .then(settings => {"
"        document.querySelectorAll(\"input[type='range'], input[type='number'], select\").forEach(element => {"
"            const settingName = element.getAttribute('data-setting');"
"            if(settingName && settings[settingName] !== undefined) {"
"                element.value = settings[settingName];"
"                if(element.type === 'range') {"
"                    const pairedInput = element.nextElementSibling;"
"                    if(pairedInput && pairedInput.type === 'number') {"
"                        pairedInput.value = settings[settingName];"
"                    }"
"                }"
"                else if(element.type === 'number') {"
"                    const pairedInput = element.previousElementSibling;"
"                    if(pairedInput && pairedInput.type === 'range') {"
"                        pairedInput.value = settings[settingName];"
"                    }"
"                }"
"            }"
"        });"
"        if(refreshBtn) {"
"            refreshBtn.disabled = false;"
"            refreshBtn.textContent = 'Refresh Settings';"
"        }"
"    })"
"    .catch(error => {"
"        console.error('Error fetching camera settings:', error);"
"        if(refreshBtn) {"
"            refreshBtn.disabled = false;"
"            refreshBtn.textContent = 'Refresh Failed';"
"            setTimeout(() => {"
"                refreshBtn.textContent = 'Refresh Settings';"
"            }, 2000);"
"        }"
"    });"
"}"
"window.refreshSettings = refreshSettings;"
"window.toggleAdvancedControls = toggleAdvancedControls;"
"window.onload = function() {"
"    var videoSrc = 'http://' + window.location.hostname + ':81';"
"    document.getElementById('video-stream').src = videoSrc;"
"    refreshSettings();"
"    var savedRotation = parseInt(localStorage.getItem('rotation')) || 0;"
"    currentRotation = savedRotation;"
"    var video = document.getElementById('video-stream');"
"    if(video) {"
"        video.style.transform = 'rotate(' + savedRotation + 'deg)';"
"        var display = document.getElementById('rotation-display');"
"        if(display) { display.textContent = savedRotation + '°'; }"
"    }"
"    const isVisible = localStorage.getItem('advancedControlsVisible') === 'true';"
"    const controls = document.querySelector('.advanced-controls');"
"    if(isVisible) { controls.classList.add('visible'); }"
"    checkLEDState();"
"    setInterval(checkLEDState, 30000);"
"};"
"</script></head><body><div class=\"stream-container\">"
"<img id=\"video-stream\" alt=\"Camera Stream\">"
"<div class=\"controls\">";

const char HOMEPAGE_PART4[] = "<div class=\"rotation-controls\">"
"<button onclick=\"rotateVideo(-45)\">↺ -45°</button>"
"<button onclick=\"resetRotation()\" id=\"rotation-display\">Reset</button>"
"<button onclick=\"rotateVideo(45)\">↻ +45°</button>"
"</div>"
"<button onclick=\"toggleAdvancedControls()\" class=\"w-full mb-2\">Advanced Camera Controls</button>"
"<button onclick=\"refreshSettings()\" class=\"w-full mb-2\" id=\"refreshBtn\">Refresh Settings</button>"
"<div class=\"advanced-controls\">"
"<div class=\"control-group\">"
"<label>Frame Size</label>"
"<div class=\"control-row\">"
"<select data-setting=\"framesize\" onchange=\"updateCameraSetting('framesize',this.value)\">"
"<option value=\"0\">96x96 (QQVGA)</option>"
"<option value=\"1\">160x120 (QQVGA2)</option>"
"<option value=\"2\">176x144 (QCIF)</option>"
"<option value=\"3\">240x176 (HQVGA)</option>"
"<option value=\"4\">240x240 (240X240)</option>"
"<option value=\"5\">320x240 (QVGA)</option>"
"<option value=\"6\">400x296 (CIF)</option>"
"<option value=\"7\">480x320 (HVGA)</option>"
"<option value=\"8\" selected>640x480 (VGA)</option>"
"<option value=\"9\">800x600 (SVGA)</option>"
"<option value=\"10\">1024x768 (XGA)</option>"
"<option value=\"11\">1280x720 (HD)</option>"
"<option value=\"12\">1280x1024 (SXGA)</option>"
"<option value=\"13\">1600x1200 (UXGA)</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>JPEG Quality (0-63, lower is better quality)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"quality\" type=\"range\" min=\"0\" max=\"63\" value=\"12\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'quality',0,63)\">"
"<input data-setting=\"quality\" type=\"number\" value=\"12\" min=\"0\" max=\"63\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'quality',0,63)\">"
"</div></div>"
"<div class=\"control-group\">"
"<label>Brightness (-2 to 2)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"brightness\" type=\"range\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'brightness',-2,2)\">"
"<input data-setting=\"brightness\" type=\"number\" value=\"0\" min=\"-2\" max=\"2\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'brightness',-2,2)\">"
"</div></div>"
"<div class=\"control-group\">"
"<label>Contrast (-2 to 2)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"contrast\" type=\"range\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'contrast',-2,2)\">"
"<input data-setting=\"contrast\" type=\"number\" value=\"0\" min=\"-2\" max=\"2\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'contrast',-2,2)\">"
"</div></div>"
"<div class=\"control-group\">"
"<label>White Balance Mode</label>"
"<div class=\"control-row\">"
"<select data-setting=\"wb_mode\" onchange=\"updateCameraSetting('wb_mode',this.value)\">"
"<option value=\"0\" selected>Auto</option>"
"<option value=\"1\">Sunny</option>"
"<option value=\"2\">Cloudy</option>"
"<option value=\"3\">Office/Indoor</option>"
"<option value=\"4\">Home/Indoor</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Saturation (-2 to 2)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"saturation\" type=\"range\" min=\"-2\" max=\"2\" value=\"0\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'saturation',-2,2)\">"
"<input data-setting=\"saturation\" type=\"number\" value=\"0\" min=\"-2\" max=\"2\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'saturation',-2,2)\">"
"</div></div>"
"<div class=\"control-group\">"
"<label>White Balance</label>"
"<div class=\"control-row\">"
"<select data-setting=\"whitebal\" onchange=\"updateCameraSetting('whitebal',this.value)\">"
"<option value=\"0\">Manual</option>"
"<option value=\"1\" selected>Auto</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>AWB Gain</label>"
"<div class=\"control-row\">"
"<select data-setting=\"awb_gain\" onchange=\"updateCameraSetting('awb_gain',this.value)\">"
"<option value=\"0\">Disabled</option>"
"<option value=\"1\" selected>Auto</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Advanced Auto Exposure</label>"
"<div class=\"control-row\">"
"<select data-setting=\"aec2\" onchange=\"updateCameraSetting('aec2',this.value)\">"
"<option value=\"0\">Disabled</option>"
"<option value=\"1\" selected>Enabled</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Manual Exposure Value (0-1200)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"aec_value\" type=\"range\" min=\"0\" max=\"1200\" value=\"1000\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'aec_value',0,1200)\">"
"<input data-setting=\"aec_value\" type=\"number\" value=\"1000\" min=\"0\" max=\"1200\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'aec_value',0,1200)\">"
"</div></div>"
"<div class=\"control-group\">"
"<label>Image Processing</label>"
"<div class=\"control-row\">"
"<select data-setting=\"raw_gma\" onchange=\"updateCameraSetting('raw_gma',this.value)\">"
"<option value=\"0\">Disable Gamma Correction</option>"
"<option value=\"1\" selected>Enable Gamma Correction</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Lens Correction</label>"
"<div class=\"control-row\">"
"<select data-setting=\"lenc\" onchange=\"updateCameraSetting('lenc',this.value)\">"
"<option value=\"0\">Disabled</option>"
"<option value=\"1\" selected>Enabled</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Downsize EN</label>"
"<div class=\"control-row\">"
"<select data-setting=\"dcw\" onchange=\"updateCameraSetting('dcw',this.value)\">"
"<option value=\"0\" selected>Disabled</option>"
"<option value=\"1\">Enabled</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Gain Ceiling</label>"
"<div class=\"control-row\">"
"<select data-setting=\"gainceiling\" onchange=\"updateCameraSetting('gainceiling',this.value)\">"
"<option value=\"2\">2X</option>"
"<option value=\"4\">4X</option>"
"<option value=\"8\">8X</option>"
"<option value=\"16\">16X</option>"
"<option value=\"32\">32X</option>"
"<option value=\"64\">64X</option>"
"<option value=\"128\" selected>128X</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Special Effects</label>"
"<div class=\"control-row\">"
"<select data-setting=\"special_effect\" onchange=\"updateCameraSetting('special_effect',this.value)\">"
"<option value=\"0\" selected>No Effect</option>"
"<option value=\"1\">Negative</option>"
"<option value=\"2\">Grayscale</option>"
"<option value=\"3\">Red Tint</option>"
"<option value=\"4\">Green Tint</option>"
"<option value=\"5\">Blue Tint</option>"
"<option value=\"6\">Sepia</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\" style=\"display: none\" >"
"<label>Color Bar Test</label>"
"<div class=\"control-row\">"
"<select data-setting=\"colorbar\" onchange=\"updateCameraSetting('colorbar',this.value)\">"
"<option value=\"0\" selected>Normal Image</option>"
"<option value=\"1\">Show Color Bars</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>Exposure Control</label>"
"<div class=\"control-row\">"
"<select data-setting=\"exposure_ctrl\" onchange=\"updateCameraSetting('exposure_ctrl',this.value)\">"
"<option value=\"0\">Manual</option>"
"<option value=\"1\" selected>Auto</option>"
"</select>"
"</div></div>"
"<div class=\"control-group\">"
"<label>AE Level (-2 to 2)</label>"
"<div class=\"control-row\">"
"<input data-setting=\"ae_level\" type=\"range\" min=\"-2\" max=\"2\" value=\"-1\" step=\"1\" "
"oninput=\"this.nextElementSibling.value=this.value;validateAndUpdate(this,'ae_level',-2,2)\">"
"<input data-setting=\"ae_level\" type=\"number\" value=\"-1\" min=\"-2\" max=\"2\" "
"onchange=\"this.previousElementSibling.value=this.value;validateAndUpdate(this,'ae_level',-2,2)\">"
"</div></div>"
"</div>"
"<div class=\"led-control\">"
"<button id=\"ledBtn\" class=\"led-btn\" onclick=\"toggleLED()\">LED: OFF</button>"
"</div>"
"</div></div></body></html>";

struct CameraSettings {
    int brightness;
    int contrast;
    int saturation;
    int wb_mode;
    int whitebal;
    int awb_gain;
    int exposure_ctrl;
    int aec2;
    int ae_level;
    int aec_value;
    int raw_gma;
    int lenc;
    int dcw;
    int gainceiling;
    int special_effect;
    int colorbar;
    int quality;
    int framesize;
} currentSettings;

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
        ov5640.start(s);

        // Store default settings
        currentSettings = {
            .brightness = 0,      // -2 to 2
            .contrast = 0,        // -2 to 2
            .saturation = 0,      // -2 to 2
            .wb_mode = 0,         // 0 = Auto
            .whitebal = 1,        // 1 = Auto
            .awb_gain = 1,        // 1 = Enable
            .exposure_ctrl = 1,   // 1 = Auto
            .aec2 = 1,            // 1 = Enable
            .ae_level = 0,        // -2 to 2
            .aec_value = 1000,    // 0 to 1200
            .raw_gma = 1,         // 1 = Enable
            .lenc = 1,            // 1 = Enable
            .dcw = 0,             // 0 = Disable
            .gainceiling = 128,   // 128x
            .special_effect = 0,  // 0 = No effect
            .colorbar = 0,         // 0 = Disable
            .quality = 12,         // Default
            .framesize = config.frame_size // default
        };

        // Set grab mode for latest frame
        //s->set_grab_mode(s, GRAB_LATEST);

        // Optimize for speed
        s->set_framesize(s, config.frame_size);
        s->set_quality(s, 12);

        // Basic image adjustments
        s->set_brightness(s, currentSettings.brightness); // Range: -2 to 2
                           // Controls overall image brightness
                           // 0 is default, positive makes brighter, negative makes darker

        s->set_contrast(s, currentSettings.contrast); // Range: -2 to 2
                           // Controls difference between light and dark areas
                           // 0 is default, positive increases contrast, negative decreases

        s->set_saturation(s, currentSettings.saturation); // Range: -2 to 2
                           // Controls colour intensity
                           // 0 is default, positive makes colours more vivid, negative more muted

        s->set_wb_mode(s, currentSettings.wb_mode); // Range: 0 to 4
                           // 0 = Auto
                           // 1 = Sunny
                           // 2 = Cloudy
                           // 3 = Office/Indoor
                           // 4 = Home/Indoor

        // White balance settings for indoor lighting
        s->set_whitebal(s, currentSettings.whitebal); // Range: 0 or 1
                           // 0 = manual white balance
                           // 1 = auto white balance

        s->set_awb_gain(s, currentSettings.awb_gain); // Range: 0 or 1
                           // 0 = disable automatic white balance gain
                           // 1 = enable automatic white balance gain

        // Boost exposure for brightness
        s->set_exposure_ctrl(s, currentSettings.exposure_ctrl); // Range: 0 or 1
                           // 0 = manual exposure
                           // 1 = auto exposure

        s->set_aec2(s, currentSettings.aec2); // Range: 0 or 1
                           // 0 = disable advanced auto exposure
                           // 1 = enable advanced auto exposure

        s->set_ae_level(s, currentSettings.ae_level); // Range: -2 to 2
                           // Fine-tunes auto exposure level
                           // 0 is default, positive increases exposure, negative decreases

        s->set_aec_value(s, currentSettings.aec_value); // Range: 0 to 1200
                           // Manual exposure value when auto exposure is disabled
                           // Higher values = brighter image
        
        // Image processing tweaks
        s->set_raw_gma(s, currentSettings.raw_gma); // Range: 0 or 1
                           // 0 = disable gamma correction
                           // 1 = enable gamma correction for better dynamic range

        s->set_lenc(s, currentSettings.lenc); // Range: 0 or 1
                           // 0 = disable lens error correction
                           // 1 = enable lens error correction

        s->set_dcw(s, currentSettings.dcw); // Range: 0 or 1
                           // 0 = disable downsize EN
                           // 1 = enable downsize EN (helps with image quality)

        s->set_gainceiling(s, (gainceiling_t)currentSettings.gainceiling); // Range: GAINCEILING_2X to GAINCEILING_128X
                           // Controls maximum gain amplification
                           // Options: 2X, 4X, 8X, 16X, 32X, 64X, 128X
                           // Higher gain = brighter but more noise
        
        // Special effects off to ensure clean image
        s->set_special_effect(s, currentSettings.special_effect); // Range: 0 to 6
                           // 0 = No Effect
                           // 1 = Negative
                           // 2 = Grayscale
                           // 3 = Red Tint
                           // 4 = Green Tint
                           // 5 = Blue Tint
                           // 6 = Sepia

        s->set_colorbar(s, currentSettings.colorbar); // Range: 0 or 1
                           // 0 = normal image
                           // 1 = show color test bars
        
        // Keep orientation settings
        s->set_hmirror(s, 0);
        s->set_vflip(s, 1);
        
        if (ov5640.focusInit() == 0) {
            Serial.println("OV5640 Focus Init Successful");
            if (ov5640.autoFocusMode() == 0) {
                Serial.println("OV5640 Auto Focus Enabled");
            }
        }

    } else {
        Serial.println("Camera sensor is not available!");
    }
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
    const TickType_t xDelay = pdMS_TO_TICKS(50); // 50ms delay between frames
    static uint8_t streamCounter = 0;
    
    for(;;) {
        WiFiClient client = streamServer.available();
        if (client) {
            client.println("HTTP/1.1 200 OK");
            client.println("Content-Type: multipart/x-mixed-replace; boundary=frame");
            client.println();
            
            while (client.connected()) {
                camera_fb_t *fb = NULL;
                streamCounter++;
                
                // Only capture frame if client is still connected
                if (client.connected()) {
                    fb = esp_camera_fb_get();
                }
                
                if (fb) {
                    // Check client is still connected before sending
                    if (client.connected()) {
                        client.printf("--frame\r\nContent-Type: image/jpeg\r\n"
                                    "Content-Length: %u\r\n\r\n",
                                    fb->len);
                        
                        // Send in chunks to prevent buffer overflow
                        const size_t chunkSize = 4096;
                        size_t remaining = fb->len;
                        uint8_t *pos = fb->buf;
                        
                        while (remaining > 0 && client.connected()) {
                            size_t sendSize = (remaining < chunkSize) ? remaining : chunkSize;
                            client.write(pos, sendSize);
                            pos += sendSize;
                            remaining -= sendSize;
                        }
                        
                        if (client.connected()) {
                            client.println();
                        }
                    }
                    esp_camera_fb_return(fb);
                }
                
                // Temperature management delay
                vTaskDelay(xDelay);
                
                // Every 100 frames, give a longer delay to help with heat management
                if (streamCounter >= 100) {
                    vTaskDelay(pdMS_TO_TICKS(500));  // 500ms cooling delay
                    streamCounter = 0;
                }
            }
            client.stop();
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Reduced polling frequency when no client
    }
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
    String currentLine = "";
    String header = "";
    unsigned long currentTime = millis();
    unsigned long previousTime = currentTime;
    const long timeoutTime = 2000;
    const unsigned int maxRequestSize = 1024; // Limit request size
    
    while (client.connected() && (millis() - previousTime <= timeoutTime)) {
        if (client.available()) {
            char c = client.read();
            header += c;
            
            if (header.length() > maxRequestSize) {
                client.stop();
                return;
            }
            
            if (c == '\n') {
                if (currentLine.length() == 0) {
                    if (header.indexOf("GET /led") >= 0) {
                        handleLED(client, header);
                    }
                    else if (header.indexOf("GET /camera") >= 0) {
                        handleCameraSettings(client, header);
                    }
                    else if (header.indexOf("GET /") >= 0) {
                        // Calculate total content length
                        size_t totalLength = strlen(HOMEPAGE_PART1) + 
                                          strlen(HOMEPAGE_PART2) + 
                                          strlen(HOMEPAGE_PART3) + 
                                          strlen(HOMEPAGE_PART4);

                        // Send headers with total content length
                        client.println("HTTP/1.1 200 OK");
                        client.println("Content-Type: text/html");
                        client.println("Connection: close");
                        client.printf("Content-Length: %d\r\n", totalLength);
                        client.println();  // Empty line between headers and body
                        
                        // Send chunks without additional newlines
                        client.print(HOMEPAGE_PART1);
                        vTaskDelay(1);
                        client.print(HOMEPAGE_PART2);
                        vTaskDelay(1);
                        client.print(HOMEPAGE_PART3);
                        vTaskDelay(1);
                        client.print(HOMEPAGE_PART4);
                    }
                    break;
                } else {
                    currentLine = "";
                }
            } else if (c != '\r') {
                currentLine += c;
            }
        }
    }
    client.stop();
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
 * Handles camera setting requests and updates camera parameters.
 * Returns -1 on error, 0 on success.
 */
void handleCameraSettings(WiFiClient &client, String header) {
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        client.println("HTTP/1.1 500 Internal Server Error");
        client.println("Content-Type: text/plain");
        client.println("Connection: close");
        client.println();
        client.println("-1");
        return;
    }

    int value = -1;
    int result = -1;
    String response = "-1";

    // Check if this is a GET request for current settings
    if (header.indexOf("GET /camera/status") >= 0) {
        String json = "{";
        json += "\"brightness\":" + String(currentSettings.brightness) + ",";
        json += "\"contrast\":" + String(currentSettings.contrast) + ",";
        json += "\"saturation\":" + String(currentSettings.saturation) + ",";
        json += "\"wb_mode\":" + String(currentSettings.wb_mode) + ",";
        json += "\"whitebal\":" + String(currentSettings.whitebal) + ",";
        json += "\"awb_gain\":" + String(currentSettings.awb_gain) + ",";
        json += "\"exposure_ctrl\":" + String(currentSettings.exposure_ctrl) + ",";
        json += "\"aec2\":" + String(currentSettings.aec2) + ",";
        json += "\"ae_level\":" + String(currentSettings.ae_level) + ",";
        json += "\"aec_value\":" + String(currentSettings.aec_value) + ",";
        json += "\"raw_gma\":" + String(currentSettings.raw_gma) + ",";
        json += "\"lenc\":" + String(currentSettings.lenc) + ",";
        json += "\"dcw\":" + String(currentSettings.dcw) + ",";
        json += "\"gainceiling\":" + String(currentSettings.gainceiling) + ",";
        json += "\"special_effect\":" + String(currentSettings.special_effect) + ",";
        json += "\"colorbar\":" + String(currentSettings.colorbar);
        json += "\"special_effect\":" + String(currentSettings.special_effect) + ",";
        json += "\"colorbar\":" + String(currentSettings.colorbar);
        json += "}";

        // Calculate content length before sending headers
        size_t jsonLength = json.length();

        client.println("HTTP/1.1 200 OK");
        client.println("Content-Type: application/json");
        client.println("Connection: close");
        client.printf("Content-Length: %d\r\n", jsonLength);
        client.println();  // Empty line between headers and body
        client.print(json);  // Use print instead of println to avoid extra newline
        return;
    }

    // Extract parameter name and value
    String param;
    if (header.indexOf("GET /camera?") >= 0) {
        param = header.substring(header.indexOf("?") + 1);
        param = param.substring(0, param.indexOf(" HTTP"));
        
        // Split parameter into name and value
        String name = param.substring(0, param.indexOf("="));
        value = param.substring(param.indexOf("=") + 1).toInt();

        // Validate and apply settings
        if (name == "brightness" && value >= -2 && value <= 2) {
            result = s->set_brightness(s, value);
            if (result == 0) currentSettings.brightness = value;
        }
        else if (name == "contrast" && value >= -2 && value <= 2) {
            result = s->set_contrast(s, value);
            if (result == 0) currentSettings.contrast = value;
        }
        else if (name == "wb_mode" && value >= 0 && value <= 4) {
            result = s->set_wb_mode(s, value);
            if (result == 0) currentSettings.wb_mode = value;
        }
        else if (name == "exposure_ctrl" && value >= 0 && value <= 1) {
            result = s->set_exposure_ctrl(s, value);
            if (result == 0) currentSettings.exposure_ctrl = value;
        }
        else if (name == "ae_level" && value >= -2 && value <= 2) {
            result = s->set_ae_level(s, value);
            if (result == 0) currentSettings.ae_level = value;
        }
        else if (name == "whitebal" && value >= 0 && value <= 1) {
            result = s->set_whitebal(s, value);
            if (result == 0) currentSettings.whitebal = value;
        }
        else if (name == "special_effect" && value >= 0 && value <= 6) {
            result = s->set_special_effect(s, value);
            if (result == 0) currentSettings.special_effect = value;
        }
        else if (name == "saturation" && value >= -2 && value <= 2) {
            result = s->set_saturation(s, value);
            if (result == 0) currentSettings.saturation = value;
        }
        else if (name == "awb_gain" && value >= 0 && value <= 1) {
            result = s->set_awb_gain(s, value);
            if (result == 0) currentSettings.awb_gain = value;
        }
        else if (name == "aec2" && value >= 0 && value <= 1) {
            result = s->set_aec2(s, value);
            if (result == 0) currentSettings.aec2 = value;
        }
        else if (name == "aec_value" && value >= 0 && value <= 1200) {
            result = s->set_aec_value(s, value);
            if (result == 0) currentSettings.aec_value = value;
        }
        else if (name == "raw_gma" && value >= 0 && value <= 1) {
            result = s->set_raw_gma(s, value);
            if (result == 0) currentSettings.raw_gma = value;
        }
        else if (name == "lenc" && value >= 0 && value <= 1) {
            result = s->set_lenc(s, value);
            if (result == 0) currentSettings.lenc = value;
        }
        else if (name == "dcw" && value >= 0 && value <= 1) {
            result = s->set_dcw(s, value);
            if (result == 0) currentSettings.dcw = value;
        }
        else if (name == "gainceiling" && value >= 2 && value <= 128) {
            result = s->set_gainceiling(s, (gainceiling_t)value);
            if (result == 0) currentSettings.gainceiling = value;
        }
        else if (name == "special_effect" && value >= 0 && value <= 6) {
            result = s->set_special_effect(s, value);
            if (result == 0) currentSettings.special_effect = value;
        }
        else if (name == "colorbar" && value >= 0 && value <= 1) {
            result = s->set_colorbar(s, value);
            if (result == 0) currentSettings.colorbar = value;
        }
        else if (name == "framesize" && value >= 0 && value <= 13) {
            result = s->set_framesize(s, static_cast<framesize_t>(value));
            //result = s->set_framesize(s, value);
            if (result == 0) currentSettings.framesize = value;
        }
        else if (name == "quality" && value >= 0 && value <= 63) {
            result = s->set_quality(s, value);
            if (result == 0) currentSettings.quality = value;
        }
        //else if (name == "hmirror" && value >= 0 && value <= 1) {
        //    result = s->set_hmirror(s, value);
        //    if (result == 0) currentSettings.hmirror = value;
        //}
        //else if (name == "vflip" && value >= 0 && value <= 1) {
        //    result = s->set_vflip(s, value);
        //    if (result == 0) currentSettings.vflip = value;
        //}
    }
    // Convert result to string and calculate length
    response = String(result);
    size_t responseLength = response.length();

    // Send response
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/plain");
    client.println("Connection: close");
    client.printf("Content-Length: %d\r\n", responseLength);
    client.println();  // Empty line between headers and body
    client.print(response);  // Use print instead of println to avoid extra newline
}

/**
 * FreeRTOS task for handling autofocus monitoring
 */
void focusTask(void *parameter) {
    const TickType_t focusCheckDelay = pdMS_TO_TICKS(5000); // Check every 5 seconds
    
    for(;;) {
        uint8_t rc = ov5640.getFWStatus();
        if (rc == FW_STATUS_S_FOCUSED) {
            vTaskDelay(pdMS_TO_TICKS(10000));  // Longer delay when focused
        } else if (rc == FW_STATUS_S_FOCUSING) {
            vTaskDelay(pdMS_TO_TICKS(1000));   // Check less frequently while focusing
        } else if (rc == -1) {
            Serial.println("OV5640 focus check failed");
            vTaskDelay(pdMS_TO_TICKS(10000));  // Longer delay on error
        }
        vTaskDelay(focusCheckDelay);
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
        4096,            // Stack size
        NULL,            // Parameters
        2,               // Priority
        &streamTaskHandle,// Handle
        0                // Core 0
    );

    // Create web server task on Core 1
    xTaskCreatePinnedToCore(
        webTask,         // Task function
        "WebTask",       // Name
        4096,            // Stack size
        NULL,            // Parameters
        1,               // Priority
        &webTaskHandle,  // Handle
        1                // Core 1
    );


    xTaskCreatePinnedToCore(
        focusTask,       // Task function
        "FocusTask",     // Name
        2048,            // Stack size
        NULL,            // Parameters
        1,               // Priority
        &focusTaskHandle,// Handle
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
