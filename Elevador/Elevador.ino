  #include <esp_int_wdt.h>
#include <esp_task_wdt.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include <PubSubClient.h>
#include <HardwareSerial.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <Wire.h> 
#include "SH1106Wire.h"

// -----------------------------------------------------------------------
//Dados WIFI / MQTT
const char *ssid = "IFMT_ELEVADOR";
const char *password = "05231003";   
//const char *ssid = "LaboratorioE117";
//const char *password = "LAB@E117"; 

//Dados IP REDE
IPAddress local_IP(172, 24, 1, 71);
IPAddress gateway(172, 24, 1, 1);
IPAddress subnet(255, 255, 0, 0);
IPAddress primaryDNS(8, 8, 8, 8);
IPAddress secondaryDNS(8, 8, 4, 4);

// -----------------------------------------------------------------------
//Conexão WIFI / MQTT
int BROKER_PORT = 1883;
String IP = "172.24.1.2";
char ip_conv[14];
const char *BROKER_MQTT;
WiFiClient espClient;
PubSubClient MQTT(espClient);

// -----------------------------------------------------------------------
//Variáveis TEMPO
long ultimoTempoCon = 0;
long ultimoTempoESP = 0;
int tempo_ESP = 1800000; //A cada 30 min reinicia
int tempo_conexao = 20000;

// -----------------------------------------------------------------------
//Variáveis
const String flagRestrita = "1";
const String flagLivre = "0";
String l = "";
char dados[20];
String dados_RFID;
int trava_RFID = 0;

// -----------------------------------------------------------------------
//Variaveis que indicam o núcleo
static uint8_t taskCoreZero = 0;
static uint8_t taskCoreOne  = 1;

// -----------------------------------------------------------------------
HardwareSerial SerialRFID(1);
SH1106Wire  display(0x3c, 21, 22);

// -----------------------------------------------------------------------
//Pinos
int elevador_rele = 32;
int rfid_rele = 33;
int pin_led_normal = 25; int pin_led_aprovado = 26; int pin_led_reprovado = 27; //Pinos dos LEDS

// -----------------------------------------------------------------------
// Funcão para se conectar ao Broker MQTT
void initMQTT() {
  MQTT.setServer(BROKER_MQTT, BROKER_PORT);
  MQTT.setCallback(mqtt_callback);
}

// -----------------------------------------------------------------------
void liberacao(){
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Falha na montagem.");
  }else{
    File rFile = SPIFFS.open("/liberacao.txt", "r");
    l = rFile.readStringUntil('\r');
  }
}

// -----------------------------------------------------------------------
void hard_restart() {
  esp_task_wdt_init(1,true);
  esp_task_wdt_add(NULL);
  while(true);
}

void setup(){
  Serial.begin(9600);
  display.init();
  display.flipScreenVertically();
  display.setContrast(255);
  display.clear();
  pinMode(pin_led_normal, OUTPUT); pinMode(pin_led_aprovado, OUTPUT); pinMode(pin_led_reprovado, OUTPUT);
  pinMode(elevador_rele, OUTPUT); pinMode(rfid_rele, OUTPUT);
  digitalWrite(elevador_rele, LOW); digitalWrite(rfid_rele, HIGH);
  // -----------------------------------------------------------------------
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("STA não conseguiu configurar.");
  }
  // -----------------------------------------------------------------------
  IP.toCharArray(ip_conv, 14);
  BROKER_MQTT = ip_conv;
  led_aprovado(); delay(200); led_reprovado(); delay(200);
  // -----------------------------------------------------------------------
  Serial.println("Acessando a REDE");
  exibir("Acessando o", "WIFI");
  // -----------------------------------------------------------------------
  Serial.println("Conectando a ");
  Serial.println(ssid);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  delay(100);
  Serial.print("Aguarde: ");
  ultimoTempoCon = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("#");
    if (millis() - ultimoTempoCon > tempo_conexao) {
      hard_restart();
    }
  }
  // -----------------------------------------------------------------------
  led_aprovado(); delay(200); led_reprovado(); delay(200);
  initMQTT();
  delay(100);
  Serial.println("Iniciando o RFID");
  exibir("Acessando o", "RFID");
  // -----------------------------------------------------------------------
  SerialRFID.begin(9600, SERIAL_8N1, 23, 18);  // speed, type, RX, TX
  // -----------------------------------------------------------------------
  led_aprovado(); delay(200); led_reprovado(); delay(200);
  ultimoTempoESP = millis();
  // -----------------------------------------------------------------------
  MQTT.connect("Elevador1");
  MQTT.subscribe("elevador/cadastro");
  
  // -----------------------------------------------------------------------
  liberacao();
//  OTA();
  led_aprovado(); delay(200); led_reprovado(); delay(200);


  // -----------------------------------------------------------------------
  Serial.println("Iniciando o OTA");
  exibir("Acessando o", "OTA");
  ArduinoOTA.setHostname("ESP_Elevador1");

  // No authentication by default
  ArduinoOTA.setPassword("17216822");

  
  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();
  // ----------------------------MULTI-CORE----------------------------------
  Serial.println("Iniciando primeiro core");
  delay(100);
  xTaskCreatePinnedToCore( //NUCLEO 1 - MQTT
    Mqtt,   
    "Mqtt", 
    10000,      
    NULL,       
    2,          
    NULL,       
    taskCoreZero);
  // -----------------------------------------------------------------------
  led_aprovado(); delay(200); led_reprovado(); delay(200);
  Serial.println("Iniciando segundo core");
  delay(250);
  xTaskCreatePinnedToCore( //NUCLEO 0 - RFID
    RFID, 
    "RFID",
    10000,   
    NULL,    
    2,       
    NULL,    
    taskCoreOne); 
  // ----------------------------MULTI-CORE----------------------------------
  exibir("Pronto", "para uso.");
  display.clear();
  digitalWrite(pin_led_reprovado, HIGH); digitalWrite(pin_led_aprovado, HIGH); digitalWrite(pin_led_normal, HIGH);  
  delay(1000);
  digitalWrite(pin_led_reprovado, LOW); digitalWrite(pin_led_aprovado, LOW);digitalWrite(pin_led_normal, LOW);  
}

void loop(){}

void Mqtt( void * pvParameters ) {
  while (true) {
    if (millis() - ultimoTempoESP > tempo_ESP) {
      hard_restart();
      delay(50);
    }
    vTaskDelay(50);
    if (!MQTT.connected()) {
      MQTT.connect("Elevador1");
      MQTT.subscribe("elevador/cadastro");
    }
    if(WiFi.status() != WL_CONNECTED){
      hard_restart();
    }
    MQTT.loop();
    vTaskDelay(100);
    
    ArduinoOTA.handle();
  }
}

void RFID( void * pvParameters ) {
  while (true) {
    display.clear();
    cartao();
    led_normal();
    if (l.equals(flagRestrita)){
      exibir("Somente Pessoa", "Autorizada");
    }else{
      exibir("Acesso", "Livre");
    }
    vTaskDelay(150);
  }
}

// -----------------------------------------------------------------------
//Função que recebe as mensagens publicadas
void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  String message;
  Serial.println("Mensagem recebida: ");
  Serial.println(topic);
  Serial.print("payload: ");
  for (int i = 0; i < length; i++)
  {
    char c = (char)payload[i];
    message += c;
  }

  delay(200);
  Serial.println("Topico => " + String(topic) + " | Valor => " + String(message));
  Serial.println(message);

  if (String(topic).equals("elevador/cadastro")){
    if(message=="500"){
      liberacao_arquivo();
      hard_restart();
      }else if(message=="100"){
        rele_atv(); 
        }else{
        cadastro(message);
      }
  }
  Serial.flush();
}

// -----------------------------------------------------------------------
void reniciar_RFID(){
  digitalWrite(rfid_rele, LOW);
  delay(500);
  digitalWrite(rfid_rele, HIGH);
  delay(650);
  SerialRFID.begin(9600, SERIAL_8N1, 23, 18);
}

// -----------------------------------------------------------------------
void rele_atv() {
  digitalWrite(elevador_rele, HIGH);
  delay(500);
  digitalWrite(elevador_rele, LOW);
}

// -----------------------------------------------------------------------
void cadastro(String msg){
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Falha na montagem.");
  }else{
    
    if (!SPIFFS.exists("/dados.txt")){
      File rFile = SPIFFS.open("/dados.txt", "w");
      
      if (!rFile){
        Serial.println("Erro ao abrir arquivo!");
      }else{
        rFile.println(msg);
        Serial.print("Salvou no arquivo:");
        Serial.println();
      }
      rFile.close();
    }else{
      if(verificar(msg)==true){
        return;}
      File rFile = SPIFFS.open("/dados.txt", "a+");
      if (!rFile){
        Serial.println("Erro ao abrir arquivo!");
      }else{ 
        rFile.println(" ");
        rFile.println(msg);
        Serial.print("Salvou no arquivo:");
        Serial.println();
      }
      rFile.close();
    }
  }
  hard_restart();
}

bool verificar(String msg){
  File file = SPIFFS.open("/dados.txt", "r");
  String dados_arquivo = file.readString();
        
        Serial.println("Lendo Arquivo");
        Serial.println(dados_arquivo);
        if (dados_arquivo.indexOf(msg) != -1) {
          Serial.println("Ja cadastrado");
          return true;
          }else{
            return false;
            }
          file.close();
  }

// -----------------------------------------------------------------------
void liberacao_arquivo(){
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Falha na montagem.");
  }else{
    File rFile = SPIFFS.open("/liberacao.txt", "w+");
    if (l.equals(flagLivre)){
      l = flagRestrita;
    }else{
      l = flagLivre;
    }
    rFile.print(l);
    Serial.println("Alteracao concluida\n");
    rFile.close();
    Serial.println("Reniciando.");
    hard_restart();
  }
}

// -----------------------------------------------------------------------
void cartao() {
  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Falha na montagem.");
  }
  int p = 0;
  while (SerialRFID.available() > 0) {
    p++;
    dados[p] = SerialRFID.read();
    trava_RFID = 1;
  }
  if (trava_RFID == 1) {
    for (int z = 2; z <= 13; z++) {
      dados_RFID += dados[z];
    }
    Serial.print("ID do Cartão:");
    Serial.println(dados_RFID);
    String get_MASTER = "66006C5A4F1F";
    if (get_MASTER.indexOf(dados_RFID) != -1) {
      liberacao_arquivo();
    }else if(get_MASTER.indexOf(dados_RFID) == -1) {
      if (l.equals(flagRestrita)){ //Modo Restrito
        File rFile = SPIFFS.open("/dados.txt", "r");
        String get_dados = rFile.readStringUntil('/r');;
        rFile.close();
        if (get_dados.indexOf(dados_RFID) != -1) {
          Serial.println("Aprovado./Restrito");
          exibir("Acesso", "Aprovado");
          led_aprovado();
          rele_atv();
        }else{
          Serial.println("Acesso Negado.");
          exibir("Acesso", "Negado");
          led_reprovado();
          }
      }else{ //Modo Livre
        Serial.println("Aprovado./Livre");
        exibir("Acesso", "Aprovado");
        led_aprovado();
        rele_atv();
      } 
    }
    dados_RFID="";
    trava_RFID = 0;
    reniciar_RFID();
  }
}

// -----------------------------------------------------------------------
/*

void OTA(){
  // Hostname defaults to esp3232-[MAC]
  ArduinoOTA.setHostname("ESP_Elevador2");
  
  // No authentication by default
  ArduinoOTA.setPassword("17216822");

   ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}*/

void led_normal(){
  if(trava_RFID == 0){
    digitalWrite(pin_led_normal, HIGH);
    digitalWrite(pin_led_aprovado, LOW);
    digitalWrite(pin_led_reprovado, LOW);
  }
}

void led_aprovado(){
  digitalWrite(pin_led_aprovado, HIGH);
  digitalWrite(pin_led_normal, LOW);
  digitalWrite(pin_led_reprovado, LOW);
}

void led_reprovado(){
  digitalWrite(pin_led_reprovado, HIGH);
  digitalWrite(pin_led_aprovado, LOW);
  digitalWrite(pin_led_normal, LOW);
}

void exibir(String texto1, String texto2) {
  display.clear();
  display.setTextAlignment(TEXT_ALIGN_CENTER);
  display.setFont(ArialMT_Plain_16);
  display.drawString(63, 1, "Elevador - IFMT");
  display.drawString(63, 26, texto1);
  display.drawString(63, 45, texto2);
  display.display();

 

}
