#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "roots.h"
#include "secrets.h"

const char* DB = "https://robot-limpiador-solar-default-rtdb.firebaseio.com";
String token;
unsigned long loginTime = 0, lastPoll = 0;


int request(const String& url, const char* method, const String& body, String& reply) {
  WiFiClientSecure client;
  client.setCACert(ROOTS);
  HTTPClient http;
  http.setConnectTimeout(5000);
  http.setTimeout(5000);
  if (!http.begin(client, url)) return -1;
  http.addHeader("Content-Type", "application/json");
  int status = http.sendRequest(method, body);
  if (status > 0) reply = http.getString();
  http.end();
  return status;
}

bool login() {
  JsonDocument doc;
  doc["email"] = EMAIL;
  doc["password"] = FIREBASE_PASSWORD;
  doc["returnSecureToken"] = true;
  String body, reply;
  serializeJson(doc, body);
  int status = request(String("https://identitytoolkit.googleapis.com/v1/accounts:signInWithPassword?key=") + API_KEY, "POST", body, reply);
  if (status != 200) { Serial.printf("[AUTH] HTTP %d\n", status); return false; }
  JsonDocument result;
  if (deserializeJson(result, reply)) return false;
  token = result["idToken"].as<String>();
  if (!token.length()) return false;
  loginTime = millis();
  Serial.println("[AUTH] OK");
  return true;
}

bool db(const char* path, const char* method, const String& body, String& reply) {
  reply = "";
  String ruta = path;
  if (!ruta.startsWith("/")) ruta = "/" + ruta;
  int status = request(String(DB) + ruta + ".json?auth=" + token, method, body, reply);
  Serial.printf("%s %s -> HTTP %d\n", method, path, status);
  if (status == 401) token = "";
  if (status != 200) { if (reply.length()) Serial.println(reply); return false; }

  return true;
}


int64_t ahoraUTC(){ struct timeval tv; gettimeofday(&tv,nullptr); return (int64_t)tv.tv_sec*1000+tv.tv_usec/1000; }
bool listo(){ if(WiFi.status()!=WL_CONNECTED || time(nullptr)<1700000000) return false; return (token.length() && millis()-loginTime<3000000UL) || login(); }
void wifiInicio(){ WiFi.mode(WIFI_STA); WiFi.setAutoReconnect(true); WiFi.begin(WIFI_SSID,WIFI_PASSWORD); configTime(0,0,"pool.ntp.org","time.google.com"); }
bool patch(const char* path, JsonDocument& d){ String b,r; serializeJson(d,b); return db(path,"PATCH",b,r); }
bool leerRaiz(JsonDocument& d){String r; return db("","GET","",r) && !deserializeJson(d,r);}

const uint8_t BOMBA=27;
// Calibracion experimental del ADC: 0 A y aproximadamente 1 A.
const uint8_t PIN_CORRIENTE = 34;
const float VOLTAJE_CERO = 1.0265f;
const float SENSIBILIDAD = 0.1439f;
const float ZONA_MUERTA = 0.05f;
struct Medicion { float voltaje; float corriente; unsigned long tomada; bool valida; };
QueueHandle_t qMedicion;

void tareaSensor(void*) {
  for (;;) {
    float suma = 0;
    for (int i = 0; i < 200; ++i) {
      suma += analogReadMilliVolts(PIN_CORRIENTE);
      vTaskDelay(pdMS_TO_TICKS(2) > 0 ? pdMS_TO_TICKS(2) : 1);
    }
    Medicion m{};
    m.voltaje = suma / 200.0f / 1000.0f;
    m.corriente = (m.voltaje - VOLTAJE_CERO) / SENSIBILIDAD;
    // No publicar valores incompatibles con el sentido y rango del prototipo.
    m.valida = isfinite(m.corriente) && m.corriente >= -ZONA_MUERTA && m.corriente <= 5.0f;
    if (fabsf(m.corriente) <= ZONA_MUERTA) m.corriente = 0.0f;
    m.tomada = millis();
    xQueueOverwrite(qMedicion, &m);
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
const unsigned long TIEMPO_BOMBA=4000;
QueueHandle_t qBomba; portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED; bool activa=false;
void estado(bool b){portENTER_CRITICAL(&mux);activa=b;portEXIT_CRITICAL(&mux);}
bool encendida(){portENTER_CRITICAL(&mux);bool b=activa;portEXIT_CRITICAL(&mux);return b;}
void tareaBomba(void*){bool orden;for(;;){if(xQueueReceive(qBomba,&orden,portMAX_DELAY)==pdTRUE){digitalWrite(BOMBA,HIGH);estado(true);Serial.println("[BOMBA] ENCENDIDA");vTaskDelay(pdMS_TO_TICKS(TIEMPO_BOMBA));digitalWrite(BOMBA,LOW);estado(false);Serial.println("[BOMBA] APAGADA");}}}
void setup(){digitalWrite(BOMBA,LOW);pinMode(BOMBA,OUTPUT);Serial.begin(115200);qBomba=xQueueCreate(1,sizeof(bool));if(!qBomba||xTaskCreatePinnedToCore(tareaBomba,"Bomba",4096,nullptr,3,nullptr,0)!=pdPASS){while(true)delay(1000);}pinMode(PIN_CORRIENTE, INPUT);
 analogReadResolution(12);
 analogSetPinAttenuation(PIN_CORRIENTE, ADC_11db);
 qMedicion = xQueueCreate(1, sizeof(Medicion));
 if (!qMedicion || xTaskCreatePinnedToCore(tareaSensor, "Sensor", 4096, nullptr, 1, nullptr, 1) != pdPASS) { Serial.println("[SENSOR] Error de inicio"); while(true) delay(1000); }
 wifiInicio();Serial.println("Externo: GPIO27 bomba, GPIO34 ACS712");}
void loop(){static bool armado=false;static int64_t ultimo=0;if(!listo()){armado=false;delay(500);return;}
 JsonDocument r;if(leerRaiz(r)){JsonVariantConst c=r["control"];bool cmd=c["bomba_comando"].as<bool>();int64_t id=c["id_comando"].as<int64_t>();int64_t edad=ahoraUTC()-c["ultimo_comando"].as<int64_t>();
 if(c["bomba_comando"].is<bool>() && !cmd)armado=true;
 else if(cmd && armado && id>ultimo && edad>=-2000 && edad<8000 && !encendida()){armado=false;ultimo=id;bool orden=true;xQueueSend(qBomba,&orden,0);delay(20);}
 }else armado=false;
 JsonDocument d;
 Medicion m{};
 if (xQueuePeek(qMedicion, &m, 0) == pdTRUE && m.valida && millis()-m.tomada < 2000UL) {
   d["mediciones/corriente"] = m.corriente;
   d["mediciones/ultima_medicion"][".sv"] = "timestamp";
   Serial.printf("[ACS712] ADC %.4f V | I %.3f A\n", m.voltaje, m.corriente);
 } else {
   Serial.println("[ACS712] Medicion invalida o no disponible");
 }
 d["control/bomba_estado"]=encendida();
 d["control/esp_externo_activo"][".sv"]="timestamp";
 patch("",d);delay(200);
}
