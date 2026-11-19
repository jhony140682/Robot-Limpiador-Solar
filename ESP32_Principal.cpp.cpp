#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>
#include <initializer_list>
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

if (!ruta.startsWith("/")) {
    ruta = "/" + ruta;
}

int status = request(
    String(DB) + ruta + ".json?auth=" + token,
    method, body, reply
);
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

// MOTOR DELANTERO - BTS7960
const uint8_t MOT_DEL_RPWM = 25;
const uint8_t MOT_DEL_LPWM = 26;
const uint8_t MOT_DEL_REN  = 32;
const uint8_t MOT_DEL_LEN  = 33;

// MOTOR TRASERO - BTS7960
const uint8_t MOT_TRAS_RPWM = 27;
const uint8_t MOT_TRAS_LPWM = 14;
const uint8_t MOT_TRAS_REN  = 18;
const uint8_t MOT_TRAS_LEN  = 19;


const bool HABILITAR_ACTUADORES = true;
const uint8_t VELOCIDAD_MOTORES = 180;

// Cambiar a true si algun motor gira al reves
const bool INVERTIR_MOTOR_DELANTERO = false;
const bool INVERTIR_MOTOR_TRASERO = false;

// =====================================================
// 6. CONFIGURACION PWM ESP32
// =====================================================

const uint32_t FRECUENCIA_PWM = 1000;
const uint8_t RESOLUCION_PWM = 8;

// Canales PWM para Arduino-ESP32 2.x
const uint8_t CH_DEL_R = 0;
const uint8_t CH_DEL_L = 1;
const uint8_t CH_TRAS_R = 2;
const uint8_t CH_TRAS_L = 3;

// Compatibilidad con Arduino-ESP32 2.x y 3.x
#if ESP_ARDUINO_VERSION_MAJOR >= 3

void configurarPWM(uint8_t pin, uint8_t canal) {
  (void)canal;
  ledcAttach(pin, FRECUENCIA_PWM, RESOLUCION_PWM);
}

void escribirPWM(uint8_t pin, uint8_t canal, uint8_t valor) {
  (void)canal;
  ledcWrite(pin, valor);
}

#else

void configurarPWM(uint8_t pin, uint8_t canal) {
  ledcSetup(canal, FRECUENCIA_PWM, RESOLUCION_PWM);
  ledcAttachPin(pin, canal);
}

void escribirPWM(uint8_t pin, uint8_t canal, uint8_t valor) {
  (void)pin;
  ledcWrite(canal, valor);
}

#endif


void detenerMotores() {

  escribirPWM(MOT_DEL_RPWM, CH_DEL_R, 0);
  escribirPWM(MOT_DEL_LPWM, CH_DEL_L, 0);

  escribirPWM(MOT_TRAS_RPWM, CH_TRAS_R, 0);
  escribirPWM(MOT_TRAS_LPWM, CH_TRAS_L, 0);

  digitalWrite(MOT_DEL_REN, LOW);
  digitalWrite(MOT_DEL_LEN, LOW);

  digitalWrite(MOT_TRAS_REN, LOW);
  digitalWrite(MOT_TRAS_LEN, LOW);
}

void controlarMotor(
  uint8_t pinRPWM,
  uint8_t pinLPWM,
  uint8_t pinREN,
  uint8_t pinLEN,
  uint8_t canalR,
  uint8_t canalL,
  bool avanzar,
  bool invertir
) {

  bool direccion = avanzar;

  if (invertir) {
    direccion = !direccion;
  }

  // Evitar activar ambos sentidos al mismo tiempo
  escribirPWM(pinRPWM, canalR, 0);
  escribirPWM(pinLPWM, canalL, 0);

  digitalWrite(pinREN, HIGH);
  digitalWrite(pinLEN, HIGH);

  if (direccion) {
    escribirPWM(pinRPWM, canalR, VELOCIDAD_MOTORES);
  } else {
    escribirPWM(pinLPWM, canalL, VELOCIDAD_MOTORES);
  }
}

void moverMotores(bool avanzar) {

  if (!HABILITAR_ACTUADORES) {
    Serial.println("[PRUEBA] Movimiento simulado.");
    return;
  }

  controlarMotor(
    MOT_DEL_RPWM,
    MOT_DEL_LPWM,
    MOT_DEL_REN,
    MOT_DEL_LEN,
    CH_DEL_R,
    CH_DEL_L,
    avanzar,
    INVERTIR_MOTOR_DELANTERO
  );

  controlarMotor(
    MOT_TRAS_RPWM,
    MOT_TRAS_LPWM,
    MOT_TRAS_REN,
    MOT_TRAS_LEN,
    CH_TRAS_R,
    CH_TRAS_L,
    avanzar,
    INVERTIR_MOTOR_TRASERO
  );

  Serial.println(
    avanzar ? "[MOTORES] Avanzando." :
              "[MOTORES] Retrocediendo."
  );
}


// Red en tarea separada; loop controla exclusivamente los motores.
struct Lectura {float corriente, umbral; bool bomba, comando, valida; int64_t id; unsigned long recibida; int64_t rearmeSolicitud, rearmeConfirmado; bool rearmeValido;};
struct Orden {bool activar; int64_t id;};
struct Telemetria {float potencia, rendimiento; bool motores; char estado[32]; int64_t rearmeProcesado;};
QueueHandle_t qLectura,qOrden,qTele;
const unsigned long CONF=20000, IDA=4500, VUELTA=10000, VERIF=10000, INTERVALO=60000;
const bool HORARIO_SIMULADO=false;
const unsigned long MAX_EDAD=10000;
void tareaRed(void*){
 bool inicializado=false; Orden pendiente{}; bool hayOrden=false;
 for(;;){
  if(!listo()){vTaskDelay(pdMS_TO_TICKS(500)); continue;}
  if(!inicializado){JsonDocument d; d["bomba_comando"]=false; if(!patch("/control",d)){vTaskDelay(pdMS_TO_TICKS(500));continue;} inicializado=true;}
  Orden o; if(xQueueReceive(qOrden,&o,0)==pdTRUE){pendiente=o;hayOrden=true;}
  if(hayOrden){JsonDocument d; d["bomba_comando"]=pendiente.activar; if(pendiente.activar){d["id_comando"]=pendiente.id;d["ultimo_comando"][".sv"]="timestamp";}
   // No enviar un arranque atrasado.
   if(pendiente.activar && ahoraUTC()-pendiente.id>5000){pendiente.activar=false;continue;}
   if(patch("/control",d))hayOrden=false;
  }
  JsonDocument r; bool lecturaOK=leerRaiz(r); if(lecturaOK){
   Lectura l{}; JsonVariantConst m=r["mediciones"]; JsonVariantConst c=r["control"]; JsonVariantConst u=r["configuracion"]["umbral_rendimiento"];
   int64_t edad=ahoraUTC()-m["ultima_medicion"].as<int64_t>(); int64_t ext=ahoraUTC()-c["esp_externo_activo"].as<int64_t>();
   l.corriente=m["corriente"].as<float>();l.umbral=u.as<float>();l.bomba=c["bomba_estado"].as<bool>();l.comando=c["bomba_comando"].as<bool>();l.id=c["id_comando"].as<int64_t>();l.recibida=millis();
   l.valida=m["corriente"].is<float>() && u.is<float>() && l.corriente>=0 && l.corriente<=5 && l.umbral>=0 && l.umbral<=100 && edad>=-2000 && edad<10000 && ext>=-2000 && ext<10000;
   l.rearmeValido=c["rearme_solicitud"].is<int64_t>() && c["rearme_confirmado"].is<int64_t>();
   l.rearmeSolicitud=c["rearme_solicitud"].as<int64_t>();
   l.rearmeConfirmado=c["rearme_confirmado"].as<int64_t>();
   xQueueOverwrite(qLectura,&l);
  }
  Telemetria t; if(xQueuePeek(qTele,&t,0)==pdTRUE){JsonDocument d; if(isfinite(t.potencia)){d["mediciones/potencia"]=t.potencia;d["mediciones/rendimiento"]=t.rendimiento;}d["control/motores_estado"]=t.motores;d["control/estado_robot"]=t.estado;d["control/esp_principal_activo"][".sv"]="timestamp";
   if(lecturaOK && t.rearmeProcesado>0 && r["control"]["rearme_confirmado"].is<int64_t>() && r["control"]["rearme_confirmado"].as<int64_t>()<t.rearmeProcesado && r["control"]["rearme_solicitud"].as<int64_t>()>=t.rearmeProcesado) d["control/rearme_confirmado"]=t.rearmeProcesado;
   patch("",d);}
  vTaskDelay(pdMS_TO_TICKS(200));
 }
}
enum Estado {MONITOREO,CONFIRMACION,ESPERA_BOMBA,LIMPIEZA,PAUSA,REGRESO,VERIFICACION,ESPERA_ENTRE_LIMPIEZAS,ALERTA};
const char* nombres[]={"MONITOREO","CONFIRMACION","ESPERA_BOMBA","LIMPIEZA","PAUSA","REGRESO","VERIFICACION","ESPERA_ENTRE_LIMPIEZAS","ALERTA"};
Estado estado=MONITOREO; unsigned long inicio=0,ultima=0,arranque=0; bool realizada=false; Lectura l{}; int64_t id=0; float antes=0;
int64_t rearmeProcesado=0;
bool rearmeInicializado=false;
void cambiar(Estado s){estado=s;inicio=millis();Serial.printf("[ESTADO] %s\n",nombres[s]);}
void orden(bool activar){Orden o{activar,id};xQueueOverwrite(qOrden,&o);}
void alerta(){detenerMotores();orden(false);cambiar(ALERTA);}
void setup(){Serial.begin(115200);
 for(uint8_t p:{MOT_DEL_REN,MOT_DEL_LEN,MOT_TRAS_REN,MOT_TRAS_LEN}){digitalWrite(p,LOW);pinMode(p,OUTPUT);}
 configurarPWM(MOT_DEL_RPWM,CH_DEL_R);configurarPWM(MOT_DEL_LPWM,CH_DEL_L);configurarPWM(MOT_TRAS_RPWM,CH_TRAS_R);configurarPWM(MOT_TRAS_LPWM,CH_TRAS_L);detenerMotores();
 qLectura=xQueueCreate(1,sizeof(Lectura));qOrden=xQueueCreate(1,sizeof(Orden));qTele=xQueueCreate(1,sizeof(Telemetria));
 if(!qLectura||!qOrden||!qTele){while(true)delay(1000);}wifiInicio();arranque=millis();inicio=arranque;
 if(xTaskCreatePinnedToCore(tareaRed,"Firebase",12288,nullptr,1,nullptr,0)!=pdPASS){while(true)delay(1000);}
 Serial.println("Principal listo. R = rearme; X = detener.");}
void loop(){
 bool nuevaLectura=xQueueReceive(qLectura,&l,0)==pdTRUE;unsigned long n=millis();bool fresca=l.valida && n-l.recibida<MAX_EDAD && WiFi.status()==WL_CONNECTED;
 float potencia=l.corriente*20.0f,rendimiento=constrain(potencia,0.0f,100.0f);bool dia=!HORARIO_SIMULADO || (n-arranque)%150000UL<120000UL;
 bool paroSerial=false;
 while(Serial.available()){char c=Serial.read();if(c=='x'||c=='X'){alerta();paroSerial=true;}if((c=='r'||c=='R') && estado==ALERTA && fresca && !l.bomba && !l.comando && rendimiento>=l.umbral){realizada=false;cambiar(MONITOREO);}}
 // Procesar una sola vez cada solicitud; las pendientes al arrancar se descartan.
 if(nuevaLectura && l.rearmeValido && l.rearmeSolicitud>=0 && l.rearmeConfirmado>=0 && l.rearmeSolicitud>=l.rearmeConfirmado){
   if(!rearmeInicializado){
     rearmeProcesado=l.rearmeSolicitud;
     rearmeInicializado=true;
   }else if(l.rearmeSolicitud>rearmeProcesado){
     rearmeProcesado=l.rearmeSolicitud;
     if(!paroSerial && estado==ALERTA && fresca && !l.bomba && !l.comando && rendimiento>=l.umbral){
       realizada=false; cambiar(MONITOREO);
       Serial.println("[REARME WEB] Aceptado");
     }else{
       Serial.println("[REARME WEB] Rechazado: condiciones no cumplidas");
     }
   }
 }
 switch(estado){
 case MONITOREO: if(fresca && dia && !l.bomba && !l.comando && rendimiento<l.umbral)cambiar(CONFIRMACION);break;
 case CONFIRMACION: if(!fresca||!dia||rendimiento>=l.umbral||l.bomba||l.comando)cambiar(MONITOREO);else if(n-inicio>=CONF){if(realizada && n-ultima<INTERVALO)cambiar(ESPERA_ENTRE_LIMPIEZAS);else {antes=rendimiento;id=ahoraUTC();orden(true);cambiar(ESPERA_BOMBA);}}break;
 case ESPERA_BOMBA: if(!fresca||n-inicio>=12000)alerta();else if(l.id==id && l.comando && l.bomba){moverMotores(true);cambiar(LIMPIEZA);}break;
 case LIMPIEZA: if(n-inicio>=IDA){detenerMotores();orden(false);cambiar(PAUSA);}break;
 case PAUSA: if(n-inicio>=200){moverMotores(false);cambiar(REGRESO);}break;
 case REGRESO: if(n-inicio>=VUELTA){detenerMotores();realizada=true;ultima=n;cambiar(VERIFICACION);}break;
 case VERIFICACION: if(n-inicio>=VERIF && fresca && dia){Serial.printf("Antes %.2f %% | Despues %.2f %%\n",antes,rendimiento);if(rendimiento>=l.umbral)cambiar(ESPERA_ENTRE_LIMPIEZAS);else alerta();}break;
 case ESPERA_ENTRE_LIMPIEZAS: if(n-ultima>=INTERVALO)cambiar(MONITOREO);break;
 case ALERTA: break;
 }
 Telemetria t{};t.rearmeProcesado=rearmeProcesado;t.potencia=fresca?potencia:NAN;t.rendimiento=rendimiento;t.motores=estado==LIMPIEZA||estado==REGRESO;snprintf(t.estado,sizeof(t.estado),"%s",nombres[estado]);xQueueOverwrite(qTele,&t);
 static unsigned long log=0;if(n-log>=2000){log=n;Serial.printf("I=%.2f A P=%.2f W R=%.2f %% U=%.2f %% %s\n",l.corriente,potencia,rendimiento,l.umbral,fresca?"OK":"SIN DATOS RECIENTES");}delay(10);
}
