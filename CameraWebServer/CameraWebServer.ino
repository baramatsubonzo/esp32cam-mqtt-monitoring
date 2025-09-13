#include "esp_camera.h"
#include <WiFi.h>
// Add
#include <WebServer.h>
// For MQTT
#include <PubSubClient.h>
// We use AIThinker
#define CAMERA_MODEL_AI_THINKER
// ===========================
// Select camera model in board_config.h
// ===========================
#include "board_config.h"
#include "secrets.h"

void setupLedFlash();

WebServer server(80);

// MQTT setup
const char* MQTT_HOST = "broker.hivemq.com";
const uint16_t MQTT_PORT = 1883;
WiFiClient espClient;
PubSubClient mqtt(espClient);

// MQTT topic
const char* MQTT_TOPIC_HELLO = "esp32cam/hello";

// MQTT Reconnect helper
bool mqttReconnect() {
  if (mqtt.connected()) return true;
  String clientId = "esp32-cam-" + String((uint32_t)esp_random(), 16);

  for (int i = 0; i < 20 && !mqtt.connected(); ++i) {
    if (mqtt.connect(clientId.c_str())) {
      Serial.println("\nMQTT connected");
      return true;
    }
    Serial.print(".");
    delay(500);
  }

  Serial.println("\nMQTT connect failed");
  return false;
}

// HTTP handler
void handleRoot() {
  String html = 
    "<html><head><meta charset='utf-8'>"
    "<meta http-equiv='refresh' content='5'>"
    "<title>Latest Snapshot</title></head>"
    "<body style='font-family:sans-serif'>"
    "<h2>Latest Snapshot (auto-refresh every 5s)</h2>"
    "<img src='/snapshot.jpg' style='max-width:100%;height:auto'/>"
    "<p><a href='/snapshot.jpg'>Capture now</a></p>"
    "</body></html>";
  server.send(200, "text/html", html);
}

void handleSnapshot() {
  for (int attempt = 0; attempt < 3; ++attempt) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) {delay(100); continue; }

    server.setContentLength(fb->len);
    // server.sendHeader("Content-Type", "image/jpeg");
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.send(200, "image/jpeg", "");

    WiFiClient client = server.client();
    client.write(fb->buf, fb->len);

    esp_camera_fb_return(fb);
    return;
  }
  server.send(503, "text/plain", "Capture failed");
}


void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

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

  config.frame_size = FRAMESIZE_VGA;
  config.pixel_format = PIXFORMAT_JPEG;  // for streaming
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.jpeg_quality = 22;
  config.fb_count = 1;


#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  // camera init
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        // flip it back
    s->set_brightness(s, 1);   // up the brightness just a bit
    s->set_saturation(s, -2);  // lower the saturation
  }
  // // drop down frame size for higher initial frame rate
  // if (config.pixel_format == PIXFORMAT_JPEG) {
  //   s->set_framesize(s, FRAMESIZE_QVGA);
  // }

#if defined(CAMERA_MODEL_M5STACK_WIDE) || defined(CAMERA_MODEL_M5STACK_ESP32CAM)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

#if defined(CAMERA_MODEL_ESP32S3_EYE)
  s->set_vflip(s, 1);
#endif

// Setup LED FLash if LED pin is defined in camera_pins.h
#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.begin(WIFI_SSID, WIFI_PASS);
  WiFi.setSleep(false);

  Serial.print("WiFi connecting");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");

  // Web Server
  server.on("/", handleRoot);
  server.on("/snapshot.jpg", handleSnapshot);
  server.begin(); 

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");

  // MQTT connection and publish text
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  if (mqttReconnect()) {
    String hello = "Hello from ESP32-CAM at " + WiFi.localIP().toString();
    mqtt.publish(MQTT_TOPIC_HELLO, hello.c_str());
  }
}

unsigned long lastHello = 0;

void loop() {
  server.handleClient();

  if (!mqtt.connected()) {
    mqttReconnect();
  }
  mqtt.loop();

  if (millis() - lastHello > 10000 && mqtt.connected()) {
    mqtt.publish(MQTT_TOPIC_HELLO, "heartbeat");
    lastHello = millis();
  }
}
