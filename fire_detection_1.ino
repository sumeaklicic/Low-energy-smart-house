#define BLYNK_TEMPLATE_ID "TMPL4pBbwNmJw"
#define BLYNK_TEMPLATE_NAME "fire alert"
#define BLYNK_AUTH_TOKEN "bizzAYq3X721Pkvx-uXgl7_hMy3jDXJu"

#define BLYNK_PRINT Serial

#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Firebase_ESP_Client.h>
#include <addons/RTDBHelper.h>

// ---- WiFi ----
char ssid[] = "medina";
char pass[] = "nemasifre";

// ---- Firebase ----
#define DATABASE_URL "https://smart-home-26a6e-default-rtdb.firebaseio.com"
#define API_KEY "AIzaSyBvLLHQ891y0adMOqXllmh9UX080MjctOk"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ---- Sensor ----
int smokeA0 = A0;
int sensorThres = 700;
BlynkTimer timer;

// ---- BUZZER ----
int buzzer = D5;  // <--- buzzer pin

// ---- Token status callback ----
void tokenStatusCallback(TokenInfo info) {
  Serial.printf("Token status: %d\n", info.status);
}

// ---- Funkcija za slanje podataka ----
void sendSensor() {
  int data = analogRead(smokeA0);
  Serial.print("Pin A0: ");
  Serial.println(data);

  // Šaljemo na Blynk
  Blynk.virtualWrite(V0, data);

  String smokeStatus = (data > sensorThres) ? "Smoke_detected" : "No_smoke";

  if (Firebase.ready()) {

    Firebase.RTDB.setInt(&fbdo, "/SmartHome/Sensors/smoke_value", data);
    Firebase.RTDB.setString(&fbdo, "/SmartHome/Sensors/smoke_status", smokeStatus);
    Firebase.RTDB.setInt(&fbdo, "/SmartHome/Sensors/last_update", millis());

    Serial.println("Poslato u Firebase!");
  }

  // Slanje upozorenja u Blynk
  if (data > sensorThres) {
    Blynk.logEvent("smoke_alert", "Dim detektovan!");
  
   
  
   // --- BUZZER UKLJUČI ---
    digitalWrite(buzzer, HIGH);
  } else {
    // --- BUZZER ISKLJUČI ---
    digitalWrite(buzzer, LOW);
  } 
}

void setup() {
  Serial.begin(115200);
  pinMode(smokeA0, INPUT);

  // BUZZER pin
  pinMode(buzzer, OUTPUT);   // <--- dodano
  digitalWrite(buzzer, LOW); // sigurnosti radi

  // ---- WiFi ----
  Serial.println("Spajam se na WiFi...");
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK!");

  // ---- Firebase konfiguracija ----
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Email + Password Authentication
  auth.user.email = "sumeaklicic3052@gmail.com";
  auth.user.password = "sumea123";

  // Status tokena (debug)
  config.token_status_callback = tokenStatusCallback;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // ---- Blynk ----
  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass);

  // Timer za slanje podataka svakih 2.5 sekunde
  timer.setInterval(2500L, sendSensor);
}

void loop() {
  Blynk.run();
  timer.run();
}
