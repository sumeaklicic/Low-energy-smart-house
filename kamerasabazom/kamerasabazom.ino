#include "esp_camera.h"
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <addons/RTDBHelper.h>
#include <WiFiClient.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include "esp_http_server.h" // Ova je najvažnija za tvoju grešku
#include "esp_timer.h"
#include "img_converters.h"
#include "fb_gfx.h"
// ----------------------
// WARNING!!! Make sure that you have either selected ESP32 Wrover Module,
//            or another board which has PSRAM enabled
// ----------------------

// Select camera model
// #define CAMERA_MODEL_WROVER_KIT
// ...
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h" 
// --- WiFi Credentials (Iste kao kod drugarice) ---
const char* ssid = "medina";
const char* password = "nemasifre";

// --- Firebase Konfiguracija (Iste kao kod drugarice) ---
#define DATABASE_URL "https://smart-home-26a6e-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyBvLLHQ891y0adMOqXllmh9UX080MjctOk"

// Deklaracije Firebase objekata
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// --- Token status callback (funkcija za debug) ---
void tokenStatusCallback(TokenInfo info) {
  Serial.printf("Token status: %d\n", info.status);
}

void startCameraServer();

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // Ostatak camera setup koda je nepromijenjen, samo WiFi konekcija ide gore
  camera_config_t config_cam; // Promijenjen naziv varijable da se ne preklapa sa Firebase config
  config_cam.ledc_channel = LEDC_CHANNEL_0;
  config_cam.ledc_timer = LEDC_TIMER_0;
  config_cam.pin_d0 = Y2_GPIO_NUM;
  config_cam.pin_d1 = Y3_GPIO_NUM;
  config_cam.pin_d2 = Y4_GPIO_NUM;
  config_cam.pin_d3 = Y5_GPIO_NUM;
  config_cam.pin_d4 = Y6_GPIO_NUM;
  config_cam.pin_d5 = Y7_GPIO_NUM;
  config_cam.pin_d6 = Y8_GPIO_NUM;
  config_cam.pin_d7 = Y9_GPIO_NUM;
  config_cam.pin_xclk = XCLK_GPIO_NUM;
  config_cam.pin_pclk = PCLK_GPIO_NUM;
  config_cam.pin_vsync = VSYNC_GPIO_NUM;
  config_cam.pin_href = HREF_GPIO_NUM;
  config_cam.pin_sscb_sda = SIOD_GPIO_NUM;
  config_cam.pin_sscb_scl = SIOC_GPIO_NUM;
  config_cam.pin_pwdn = PWDN_GPIO_NUM;
  config_cam.pin_reset = RESET_GPIO_NUM;
  config_cam.xclk_freq_hz = 20000000;
  config_cam.pixel_format = PIXFORMAT_JPEG;
  
  if(psramFound()){
    config_cam.frame_size = FRAMESIZE_UXGA;
    config_cam.jpeg_quality = 10;
    config_cam.fb_count = 2;
  } else {
    config_cam.frame_size = FRAMESIZE_SVGA;
    config_cam.jpeg_quality = 12;
    config_cam.fb_count = 1;
  }

  // camera init
  esp_err_t err = esp_camera_init(&config_cam); // Koristimo novu varijablu
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x", err);
    return;
  }

  sensor_t * s = esp_camera_sensor_get();
  // initial sensors are flipped vertically and colors are a bit saturated
  if (s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }
  // drop down frame size for higher initial frame rate
  s->set_framesize(s, FRAMESIZE_QVGA);

  WiFi.begin(ssid, password);

  Serial.println("Spajam se na WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
  
  // ----------------------------------------------------
  // --- DIO ZA FIREBASE UNOS IP ADRESE ---
  // ----------------------------------------------------

  // Inicijalizacija Firebase konfiguracije
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Email + Password Authentication (isti podaci kao kod drugarice)
  auth.user.email = "sumeaklicic3052@gmail.com";
  auth.user.password = "sumea123";

  // Status tokena (debug)
  config.token_status_callback = tokenStatusCallback;

  // Pokretanje Firebase servisa
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Cekamo da se Firebase autentifikacija zavrsi
  while (!Firebase.ready()) {
    Serial.print(".");
    delay(100);
  }
  Serial.println("\nFirebase OK!");

  // Konvertujemo IP adresu u string format koji Firebase prima
  String cameraIP = WiFi.localIP().toString();
  Serial.print("Lokalna IP adresa kamere: ");
  Serial.println(cameraIP);

  // Saljemo IP adresu u bazu pod specificnim atributom "IP_kamera1"
  if (Firebase.RTDB.setString(&fbdo, "/SmartHome/Cams/IP_Cam1", cameraIP)) {
      Serial.println("SUCCESS: IP adresa poslata u Firebase!");
  } else {
      Serial.print("FAILED: Nije uspjelo slanje IP adrese, ");
      Serial.println(fbdo.errorReason());
  }

  // Pokretanje servera za video stream (funkcija iz originalnog koda)
  startCameraServer();

  Serial.print("Camera Ready! Use 'http://");
  Serial.print(WiFi.localIP());
  Serial.println("' to connect");
}

void loop() {
  // put your main code here, to run repeatedly:
  delay(10000);
  // IP adresa se salje samo jednom u setup-u, loop je prazan.
}




#define PART_BOUNDARY "123456789000000000000987654321"

static const char* _STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char* _STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char* _STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n";

httpd_handle_t stream_httpd = NULL;

static esp_err_t stream_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    size_t _jpg_buf_len;
    uint8_t * _jpg_buf;
    char * part_buf[64];
    static int64_t last_frame = 0;
    if(!last_frame) {
        last_frame = esp_timer_get_time();
    }

    res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
    if(res != ESP_OK){
        return res;
    }

    while(true){
        fb = esp_camera_fb_get();
        if (!fb) {
            Serial.println("Camera capture failed");
            res = ESP_FAIL;
        } else {
            _jpg_buf = fb->buf;
            _jpg_buf_len = fb->len;
        }

        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, _STREAM_PART, sprintf((char *)part_buf, _STREAM_PART, _jpg_buf_len));
        }
        if(res == ESP_OK){
            res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
        }
        
        if(fb){
            esp_camera_fb_return(fb);
            fb = NULL;
            _jpg_buf = NULL;
        } else if(res != ESP_OK){
            break;
        }
        if(res != ESP_OK){
            break;
        }
    }
    last_frame = 0;
    return res;
}

static esp_err_t capture_handler(httpd_req_t *req){
    camera_fb_t * fb = NULL;
    esp_err_t res = ESP_OK;
    int64_t fr_start = esp_timer_get_time();

    fb = esp_camera_fb_get();
    if (!fb) {
        Serial.println("Camera capture failed");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "image/jpeg");
    httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    size_t fb_len = 0;
    if(fb->format == PIXFORMAT_JPEG){
        fb_len = fb->len;
        res = httpd_resp_send(req, (const char *)fb->buf, fb_len);
    } else {
        // Ako nije JPEG format, treba konverzija, ali za AI-Thinker je obično JPEG
        httpd_resp_send_500(req);
        res = ESP_FAIL;
    }

    esp_camera_fb_return(fb);
    
    int64_t fr_end = esp_timer_get_time();
    Serial.printf("JPG: %uB %ums\n", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start)/1000));
    return res;
}


void startCameraServer(){
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t stream_uri = {
        .uri       = "/", // Ovo postavlja stream da bude dostupan na glavnoj IP adresi kamere
        .method    = HTTP_GET,
        .handler   = stream_handler,
        .user_ctx  = NULL
    };
    // --- NOVO: Definicija za /capture putanju ---
    httpd_uri_t capture_uri = {
        .uri       = "/capture",
        .method    = HTTP_GET,
        .handler   = capture_handler, // Koristi novu funkciju
        .user_ctx  = NULL
    };
    // ---------------------------------------------

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &stream_uri);
        // --- NOVO: Registracija /capture putanje ---
        httpd_register_uri_handler(stream_httpd, &capture_uri);
        // ------------------------------------------
    }

}
