#include <FS.h>
#include <StreamString.h>

#include <NTPClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include <Updater.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "BlinkLED.h"

#define SOFTWAREIMAGE   ARDUINO_BOARD "-" "00.00.08.f"
#define FIRMWAREFILE    "/firmware.bin"
#define CAFILE          "/ca.crt"
#define CONFIGFILE      "/config.json"

#define DEBUG
#ifdef DEBUG
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#endif

String   gMQTTServer     = "xx.xx";
uint16_t gMQTTPort       = 1883;
uint16_t gMQTTTls        = true;
String   gNTPServer      = "pool.ntp.org";
uint32_t gStatusInterval = 60000;
uint32_t gWaitTimeout    = 10000;
uint32_t gRetryTimeout   = 3000;

#define CONNECT_TIMEOUT 30000
#define DOT_TIMEOUT     1000
#define EPOCH_1_1_2019  1546300800

WiFiUDP ntpUDP;
NTPClient ntpClient(ntpUDP, gNTPServer.c_str(), 0);

typedef enum {
  WiFiNotConnected   = 0,
  WiFiSmartConfig,
  WiFiWaitSmartConfig,
  WiFiConnecting,
  WiFiConnected,
  NTPStart,
  NTPSyncing,
  NTPSynced,
  CertCheching,
  CertChecked,
  MQTTStart,
  MQTTCertificate,
  MQTTConnecting,
  MQTTConnected,
  MQTTSubscribe,
  MQTTReady
} ConnectionState;

auto gConnectionState = WiFiSmartConfig; //WiFiNotConnected;
auto gLastStatus      = millis();
auto gBootMillis      = millis();
auto gTimeoutStarted  = millis();
auto gRetryWaitUntil  = millis();
auto gMQTTMillis      = millis();

// Topics
String gStatusTopic     = "/status/";
String gCommandTopic    = "/command/";
String gWillTopic       = "/will/";
String gClientname      = "";
String gHostname        = "";
String gUpdateServerURL = "";
String gFlashError      = "";

WiFiClientSecure wifiClient;
PubSubClient mqttClient(wifiClient);


bool verifyTLS(String &server, uint16_t port) {
  auto ret = false;
  DEBUG_PRINT(F("connecting to MQTT-server ")); DEBUG_PRINT(server); DEBUG_PRINT(":"); DEBUG_PRINTLN(port);
  if (wifiClient.connect(server, port)) {
    ret = wifiClient.verifyCertChain(server.c_str());
    wifiClient.stop();
  }else {
    DEBUG_PRINT(F("connection to MQTT-server failed: ")); DEBUG_PRINTLN(wifiClient.status());
    ret = false;    
  }

  return(ret);
}


void setupSPIFFS(){
  FSInfo fsInfo;

  if (!SPIFFS.begin()) {
   DEBUG_PRINTLN(F("Failed to mount file system"));
   return;
 }

#ifdef DEBUG
  SPIFFS.info(fsInfo);
  DEBUG_PRINTLN(F("SPIFFS"));
  DEBUG_PRINT(F("Max:  ")); DEBUG_PRINTLN(fsInfo.totalBytes);
  DEBUG_PRINT(F("Used: ")); DEBUG_PRINTLN(fsInfo.usedBytes);
  DEBUG_PRINTLN(F("Files:"));
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
    File file2 = SPIFFS.open(dir.fileName(), "r");
    if(file2){
      DEBUG_PRINT(dir.fileName()); DEBUG_PRINT(": "); DEBUG_PRINTLN(file2.size());
    }
  }
#endif
}


void loadConfig(){
  DEBUG_PRINTLN("Config loading...");
  if(File configFile = SPIFFS.open("/config.json", "r")){
    DynamicJsonDocument json(1024);
    DeserializationError error = deserializeJson(json, configFile);
    if (error){
      DEBUG_PRINTLN(F("Config not loaded"));
    }else{
      gMQTTServer     = json["mqttserver"].as<const char*>();
      gMQTTPort       = json["mqttport"];
      gMQTTTls        = json["mqtttls"];
      gNTPServer      = json["ntpserver"].as<const char*>();
      gStatusInterval = json["statusinterval"];
      gWaitTimeout    = json["waittimeout"];
      gRetryTimeout   = json["retrytimeout"];
      gUpdateServerURL= json["updateserverurl"].as<const char *>();
      
      DEBUG_PRINTLN("Config loaded");
    }
  }else{
    DEBUG_PRINTLN("Failed to open /config.json");
  }  
}


void loadCerts(WiFiClientSecure &client) {
  DEBUG_PRINTLN("Certificates loading for " + gClientname + "...");
  if(File cert = SPIFFS.open("/" + gClientname + ".crt.der", "r")){
    if (client.loadCertificate(cert)){
      DEBUG_PRINTLN("cert loaded");
    } else {
      DEBUG_PRINTLN("cert not loaded");
    }
  }else{
    DEBUG_PRINTLN("Failed to open " + gClientname + ".crt.der");
  }

  if(File private_key = SPIFFS.open("/" + gClientname + ".key.der", "r")){ 
    if (client.loadPrivateKey(private_key)){
      DEBUG_PRINTLN("private key loaded");
    } else {
      DEBUG_PRINTLN("private key not loaded");
    }
  } else {
    DEBUG_PRINTLN("Failed to open " + gClientname + ".key.der");
  }

  if(File ca = SPIFFS.open("/ca.der", "r")){
    if(client.loadCACert(ca)){
      DEBUG_PRINTLN("ca loaded");
    } else {
      DEBUG_PRINTLN("ca failed");
    }
  } else {
    DEBUG_PRINTLN("Failed to open ca ");
  }
  DEBUG_PRINTLN("Certificates loaded");
}


BlinkLED  led(BUILTIN_LED);

String downloadFilename ="";
char  *downloadParameter = NULL;

bool mustFlashFirmware = false;

void flashFirmware(const char*filename){
  DEBUG_PRINT(F("Flash firmware ")); DEBUG_PRINTLN(filename);

  
  if(File stream = SPIFFS.open(filename, "r")){
    auto firmwareSize = stream.size();
    if(firmwareSize>0){
      static const char pubkey[] PROGMEM = R"KEY(
-----BEGIN PUBLIC KEY-----
MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAwdz/6JbNnpckZinK1/k0
1T6gOhY6dj6vVmflTBF3lDM7j0VBYi0CIQ613gME9WyiWU+Qzv9fjrBm2slDCTbt
Il8phIeQKlL2IpVulgo+c4yt1PCim1+4JsBrDz/Knkd+u3Lg9Zrd1VQ46QIObztV
RvdU9UUrR+khCcy5mthyecdhwXg6ijoLaBvS8UNmIBqbEOd31O1mviELVLu2Cc8Q
RbRIpts4HDTPNZP+vcggdmpDL+GW/KX2X6ODEMgWAuAB92J7eiCioKj076Hx5JV7
vKjE8vdv7TgyES35F94NTY0vH8Hh8hMpVuxrCJxUztVsOKp6gSesJNs+Ag8wmqcs
wQIDAQAB
-----END PUBLIC KEY-----
)KEY";
      BearSSL::PublicKey signPubKey(pubkey);
      BearSSL::HashSHA256 hash;
      BearSSL::SigningVerifier sign( &signPubKey );
      Update.clearError();
      gFlashError = "";
      Update.installSignature( &hash, &sign );
      if(Update.begin(firmwareSize)){
        auto newSize = Update.writeStream(stream);
        DEBUG_PRINT(F("write: ")); DEBUG_PRINT(firmwareSize); DEBUG_PRINT(": "); DEBUG_PRINTLN(newSize);
        if(Update.end()){
          DEBUG_PRINTLN("Flash OK");
        } 
        
        if(Update.hasError()){
          StreamString oStr;
          Update.printError(oStr);
          gFlashError = oStr.readString();
          DEBUG_PRINTLN(gFlashError);
        }
      }
    } else {
      DEBUG_PRINTLN("Empty firmwarefile");
    }
  }
}


void mqttCallback(const char *inTopic, byte* payload, unsigned int length){
  String topic = inTopic;
  DEBUG_PRINT(F("MQTT receive: ")); DEBUG_PRINTLN(inTopic);
  DEBUG_PRINT(F("Command: "));
  if(topic.endsWith("/reboot")){
    DEBUG_PRINTLN("reboot");
    ESP.reset();
  } else if(topic.endsWith("/config")){
    downloadFilename = CONFIGFILE;
    downloadParameter = NULL;
    DEBUG_PRINTLN("config");
  } else if(topic.endsWith("/cert")){
    downloadFilename = "/" + gClientname + ".crt";
    downloadParameter = NULL;
  } else if(topic.endsWith("/key")){
    downloadFilename = "/" + gClientname + ".key";
    downloadParameter = NULL;
  } else if(topic.endsWith("/ca")){
    downloadFilename = CAFILE;
  } else if(topic.endsWith("/firmware")){
    DEBUG_PRINTLN("firmware");     
    downloadFilename = "/firmware.bin";
    downloadParameter = "?curimage=" SOFTWAREIMAGE;
  } else if(topic.endsWith("/flash")){
    DEBUG_PRINTLN("flash");
    mustFlashFirmware = true;     
  }else {
    DEBUG_PRINTLN("UNKNOWN");         
  }
}


void downloadFile(const char *filename, const char *url){
  DEBUG_PRINT(F("Download: ")); DEBUG_PRINT(filename); DEBUG_PRINT(" "); DEBUG_PRINTLN(url);
  HTTPClient client;
  String uri = url;
  mqttClient.disconnect();
//  wifiClient.setInsecure();
  DEBUG_PRINT(F("Free heap: ")); DEBUG_PRINTLN(ESP.getFreeHeap());
  if(client.begin(wifiClient, url)){

    const char* headerNames[] = { "Content-Disposition", "Content-Type", "Content-Length" };
    client.collectHeaders(headerNames, sizeof(headerNames)/sizeof(headerNames[0]));
    auto httpCode = client.GET();
    DEBUG_PRINT(F("Free heap: ")); DEBUG_PRINTLN(ESP.getFreeHeap());
    if (httpCode > 0) {
      for(int i = 0; i<client.headers(); i++){
         DEBUG_PRINT(client.headerName(i)); DEBUG_PRINT(" "); DEBUG_PRINTLN(client.header(i)); 
      }
      switch(httpCode){
      case HTTP_CODE_OK: {
          DEBUG_PRINTLN(F("Downloading..."));
          if (File f = SPIFFS.open(filename, "w")) {
            client.writeToStream(&f);
            f.close();
          }
          DEBUG_PRINTLN(F("Downloaded"));
        }
      break;
      case HTTP_CODE_NOT_MODIFIED:
        DEBUG_PRINTLN(F("File not Modified"));
      break;
      default:
        DEBUG_PRINT(F("[HTTP] GET... failed, error: ")); DEBUG_PRINTLN(client.errorToString(httpCode));
      }
    }else {
      DEBUG_PRINT(F("httpCode: ")); DEBUG_PRINTLN(httpCode);
    }
    client.end();  
  }else {
    DEBUG_PRINTLN(F("HTTP-Begin-Error"));
  }
  gConnectionState = MQTTCertificate;  
}

void setup(){
  Serial.begin(115200);
//  Serial.setDebugOutput(true);
  gBootMillis = millis();
  led.setDuration(2000, 1, 2);
  delay(500);
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN(SOFTWAREIMAGE);
  setupSPIFFS();
  loadConfig();
  
    // configure WiFi in Station Mode
  WiFi.mode(WIFI_STA);
  
  gClientname = WiFi.macAddress();
  gClientname.replace(":","");
  gClientname.toLowerCase();
  gWillTopic    += gClientname;
  gStatusTopic  += gClientname;
  gCommandTopic += gClientname + "/#";
  gHostname      = "measure-" + gClientname;
  mqttClient.setServer(gMQTTServer.c_str(), gMQTTPort);
//  mqttClient.setWill(gWillTopic.c_str(), ("{\"espname\": \"" + gHostname + "\"}").c_str());
//  mqttClient.onMessage(mqttCallback);
  gTimeoutStarted = 0;
  gRetryWaitUntil = 0;
  gMQTTMillis     = 0;
  ESP.wdtDisable();

  struct station_config config;
  wifi_station_get_config(&config);
  DEBUG_PRINT("Default SSID "); DEBUG_PRINTLN((char *)config.ssid);
  gConnectionState = config.ssid[0]?WiFiNotConnected:WiFiSmartConfig;
   
  DEBUG_PRINT(F("Free Sketch: ")); DEBUG_PRINTLN(ESP.getFreeSketchSpace());
  DEBUG_PRINT(F("Sketch size: ")); DEBUG_PRINTLN(ESP.getSketchSize());
  DEBUG_PRINT(F("Chip size:   ")); DEBUG_PRINTLN(ESP.getFlashChipRealSize());
}


bool mqttSend(String &topic, String &payload){
  bool ret = false;
  DEBUG_PRINT(F("Sending payload ")); DEBUG_PRINT(topic); DEBUG_PRINT(" "); DEBUG_PRINTLN(payload);

  if(mqttClient.beginPublish(topic.c_str(), payload.length(), false)){
    mqttClient.write((const byte*)payload.c_str(), payload.length());
//    DEBUG_PRINT(F("Write state: ")); DEBUG_PRINT(mqttClient.state());
    mqttClient.endPublish();
    DEBUG_PRINT(F("Status: ")); DEBUG_PRINTLN(mqttClient.state());
  } else {
    DEBUG_PRINT(F("Publish state: ")); DEBUG_PRINTLN(mqttClient.state()); 
  }
  ret=true;
  return(ret);
}

ConnectionState checkMQTTConnection(){
  bool ret=false;
  if(gRetryWaitUntil && (millis() <gRetryWaitUntil)){
    if((millis() % DOT_TIMEOUT) == 0){
       DEBUG_PRINT("-");
       delay(1);
    }
    return(gConnectionState);
  }

  switch(gConnectionState){
    case WiFiNotConnected:
      led.setDuration(500, 1, 1);
      WiFi.begin();      
      gConnectionState= WiFiConnecting;
      DEBUG_PRINTLN(""); DEBUG_PRINTLN(F("WiFi connecting..."));
      gTimeoutStarted = millis();
    break;
    case WiFiSmartConfig:
      led.setDuration(100, 1, 1);
      WiFi.beginSmartConfig();
      gConnectionState= WiFiWaitSmartConfig;      
      DEBUG_PRINTLN(F("\r\nWiFi SmartConfig..."));
      gTimeoutStarted = millis();
    break;
    case WiFiWaitSmartConfig:
      if(WiFi.smartConfigDone()){
        WiFi.stopSmartConfig();
        DEBUG_PRINTLN(""); DEBUG_PRINT(F("WiFi SmartConfig done "));
        DEBUG_PRINTLN(WiFi.SSID());
//        WiFi.printDiag(Serial);
        gConnectionState= WiFiConnecting;        
        gTimeoutStarted = millis();
      } else {
        if((millis() % DOT_TIMEOUT) == 0){
          DEBUG_PRINT(".");
          delay(1);
        }
      }
    break;
    case WiFiConnecting:
      if (WiFi.status() == WL_CONNECTED) {
        gConnectionState= WiFiConnected;
      } else {
        if((millis() % DOT_TIMEOUT) == 0){
          DEBUG_PRINT(".");
//          DEBUG_PRINTLN(WiFi.SSID());
          delay(1);
        }
      }
    break;
    case WiFiConnected:
      gTimeoutStarted=0;
      led.setDuration(2000, 1, 1);
      DEBUG_PRINTLN(""); DEBUG_PRINT(F("WiFi connected to ")); DEBUG_PRINTLN(WiFi.SSID());
      WiFi.hostname(gHostname);
      gConnectionState= NTPStart;
    break;
    case NTPStart:
      DEBUG_PRINT(F("SNTP using ")); DEBUG_PRINTLN(gNTPServer);
//      configTime(-7.0, 0, gNTPServer);
      ntpClient.setPoolServerName(gNTPServer.c_str());
      ntpClient.begin();
      ntpClient.forceUpdate();
      DEBUG_PRINTLN(F("NTP syncing..."));
      gConnectionState= NTPSyncing;
      gTimeoutStarted = millis();
    break;
    case NTPSyncing:{
      auto now = time(nullptr);
      if (now > EPOCH_1_1_2019) {
        gConnectionState= NTPSynced;        
      } else {
        if((millis() % DOT_TIMEOUT) == 0){
          DEBUG_PRINT(".");
          delay(1);
        }
      }
    }
    break;
    case NTPSynced:{
      gTimeoutStarted=0;
      DEBUG_PRINTLN(""); DEBUG_PRINT(F("Current time: ")); DEBUG_PRINTLN(ntpClient.getFormattedTime());
      gConnectionState = MQTTStart;
    }
    break;
    case MQTTStart:
      gConnectionState = MQTTCertificate;
      loadCerts(wifiClient);
    break;
    case MQTTCertificate:
      if(gMQTTTls){
        DEBUG_PRINT(F("MQTT-Certificate ")); DEBUG_PRINTLN(gMQTTServer);

        if(verifyTLS(gMQTTServer, gMQTTPort)){
          DEBUG_PRINTLN(F("MQTT-certificate matches"));
        } else {
          DEBUG_PRINTLN(F("MQTT-certificate doesn't match"));
        }
      }
      gConnectionState = MQTTConnecting;
    break;
    case MQTTConnecting:
      DEBUG_PRINTLN(F("MQTT connecting..."));
      if (mqttClient.connect(gClientname.c_str(), gWillTopic.c_str(), 0, false, ("{\"espname\": \"" + gHostname + "\"}").c_str())) {
        gConnectionState= MQTTConnected;
        DEBUG_PRINTLN(F("MQTT connected"));
      } else {
        gConnectionState= MQTTStart;
        DEBUG_PRINT(F("Connection failed: ")); DEBUG_PRINTLN(mqttClient.state());
      }
    break;    
    case MQTTConnected:
      led.setDuration(2000, 1, 49);
      gConnectionState = MQTTSubscribe;
      gMQTTMillis      = millis();      
    break;
    case MQTTSubscribe:
      DEBUG_PRINTLN(F("MQTT subscribing..."));
      mqttClient.setCallback(mqttCallback);
      DEBUG_PRINTLN(gCommandTopic);
      if(mqttClient.subscribe(gCommandTopic.c_str())){
        DEBUG_PRINTLN(F("MQTT subscribed"));
        gConnectionState= MQTTReady;
      } else {
        DEBUG_PRINT(F("MQTT status: ")); DEBUG_PRINTLN(mqttClient.state());
//        if(mqttClient.lastError() == LWMQTT_NETWORK_FAILED_CONNECT){
          gConnectionState= MQTTStart;
          mqttClient.disconnect();
//        }else {
//          gRetryWaitUntil = millis() + gRetryTimeout;
//        }
      }
    break;
    case MQTTReady:
      if(!mqttClient.connected()){
        mqttClient.disconnect();
        DEBUG_PRINTLN(F("MQTT connection lost"));
        gConnectionState= MQTTStart;            
        if(!wifiClient.connected()){
          DEBUG_PRINTLN(F("WiWi connection lost"));
          gConnectionState= WiFiNotConnected;      
        } else {
          gConnectionState= MQTTStart;              
        }
      }
    break;
    default:
      DEBUG_PRINT(F("ConnectionState not handled: ")); DEBUG_PRINTLN(gConnectionState);
    break;
  }

  if((gConnectionState != WiFiWaitSmartConfig) && (gTimeoutStarted && (millis()- gTimeoutStarted > gWaitTimeout))){
    DEBUG_PRINTLN("\r\nTimeout, reboot");
    ESP.restart();
  }
  return(gConnectionState);
}


uint32_t gMaxMillis = 1;


bool sendStatus(){
  bool ret=false;
  DEBUG_PRINTLN("Status sending...");
  
  DynamicJsonDocument json(1024);
  json["uptimemillis"] = millis() - gBootMillis;
  json["maxmillis"]    = gMaxMillis;
  json["mqttmillis"]   = millis() - gMQTTMillis;
  json["espname"]      = gHostname;     
  json["espimage"]     = SOFTWAREIMAGE;
  json["espbuild"]     = __DATE__ " " __TIME__;
  if(gFlashError.length()){
    json["espflasherror"] = gFlashError;
  }
  json["espip"]        = WiFi.localIP().toString();
  json["rssi"]         = WiFi.RSSI();
  json["espfreeheap"]  = ESP.getFreeHeap();
  String payload;
  serializeJson(json, payload);
  ret = mqttSend(gStatusTopic, payload);
  DEBUG_PRINTLN(F("Status sended"));  
  return(ret);
}


void loop(){
  auto startMillis = millis();
  ESP.wdtFeed();
  led.loop();  
  checkMQTTConnection();
  
  if(gConnectionState==MQTTReady){
    long now = millis();
    if (now - gLastStatus > gStatusInterval) {
      if(sendStatus()){
        gMaxMillis = 1;
      } else {
        DEBUG_PRINT(F("sendStatus state: ")); DEBUG_PRINTLN(mqttClient.state());
        gConnectionState = MQTTStart;
      }
      gLastStatus = now;
    }
    mqttClient.loop();
  }

  if(mustFlashFirmware){
    mustFlashFirmware = false;
    flashFirmware(FIRMWAREFILE);
  }
  
  if(downloadFilename.length()>0){
    downloadFile(downloadFilename.c_str(), (gUpdateServerURL + "download" + downloadFilename + (downloadParameter?downloadParameter:"")).c_str());
    downloadFilename="";
  }

  auto endMillis = millis();
  if(endMillis-startMillis>gMaxMillis){
    gMaxMillis=endMillis-startMillis;
    DEBUG_PRINT(F("Loop max duration: ")); DEBUG_PRINTLN(gMaxMillis);
  }
}
