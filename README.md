# secureESP8266

Framework für "secure" ESP8266 sketches with smartconfig, OTA, MQTT with SSL and check of certificates

## Features

### Configuration in SPIFFs

All configuration data is stored in files located in directory data. This directory is uploaded to the ESP8266 together with the sketch.

### Initial WiFi-connect with SmartConfig

To bring the ESP into your WiFI you have to configure the login credentials to your WiFi with an Android or iPhone App. 
I used the the Android App (make sure you are connected with 2.4Ghz)

    ESP touch SmartConfig ESP8266, ESP32

During setup the builtin led is flashing 10 times a second.
When the device is connected there will be a short flash every 2 seconds.

### Watchdog

The function 

    loop()

is watch by a watchdog, which reboots the ESP when the function is not entered after approx. 6 seconds again. Additionally it will reboot when the connection to the backend is lost and it could not be established in a configurable interval.


### Secure communication

All communication is secured with SSL. The CA of the server certificates is compared to a known one. Firmware-images are signed the signature is checked when flashing the images on the EPSs.

## Initial Setup

The ESP-sketch will output debug messages on Serial Monitor with 115200 baud.

### Configuration of the servers

Configururation of the connectivity is done in file

    data/config.json

The configuration parameters are
```json
{
  "updateserverurl": "https://<UPDATESERVER_PORT_PATH>",
  "mqttserver":      "<MQTTSERVER>",
  "mqtttls":         true,
  "mqttport":        8883,
  "ntpserver":       "pool.ntp.org",
  "statusinterval":  60000,
  "waittimeout":     20000,
  "retrytimeout":    3000
}
```
At least you have to adopt the 2 servers to your environment. The **<UPDATESERVER_PORT_PATH>** is the baseurl where all downloads/updates are located. The **<MQTTSERVER>** should use TLS (**mqtttls = true**)and typically it listens on port 8883. 

### Generating SSL certificates.

To guarantee a secure communication between your ESPs and the backend servers all communication is done via HTTP/MQTTS. You need SSL-certificates for the MQTT-Server, the Firmware-Updateserver and each ESP. All certificates have to be signed by the same CA. The cA is used to check whether the systems are allowed to communicate twith each other. It makes sure, that only known device could communicate.

Each ESP needs his own public/private key an the public key of the CA. The filenames of the ESP files is the MAC-address of the device.

    84f3eb012345.key
    84f3eb012345.crt
    ca.crt

In the current version you have to starte the ESP and monitor it via the SerialMonitor. After Startup it shows

    ESP8266_WEMOS_D1MINIPRO-00.00.08.f
    MAC:        84:F3:EB:01:23:45
    CLIENTNAME: 84f3eb0dbf7e
So the 2 ESP certificate files have to be 84f3eb012345.key and 84f3eb012345.crt

### Generate certificate to sign sketch
Generate a public/private key pair to sign your code.
Put the 2 files with the names 

    private.key
    public.key

in the sketch directory beside the *.ino
Additionally the contents of the file public.key has to be included in the sketch

```C++
static const char pubkey[] PROGMEM = R"KEY(
-----BEGIN PUBLIC KEY-----
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX
XXXXXXXX
-----END PUBLIC KEY-----
)KEY";
```

## Include own code into the sketch

Put your code in the function

```C++
   void customLoop(){
         // Put your code here
   }
```

## Upload contents of directory data

When using the Arduino-IDE you need for uploading data into the SPIFFS the arduino-esp8266fs-plugin from 

    https://github.com/esp8266/arduino-esp8266fs-plugin 

first. When it is installed you can see in menu 

    Tools

the Entriy 

    ESP8266 Sketch Data Upload

During the boot phase of the ESP it will show on the SerialMonitor all files it has in the SPIFFs.

### MQTT topics
There are 3 predefined topics 
 - status: The status of the ESP, is send every **statusinterval** miliseconds
 - command: Command for the ESPs. 
 - will: Message send when ESP is disconnected or swiched off
 
The MQTT topics all contain with the MAC-address of the devices
    
    /status/84f3eb012345
    /command/84f3eb012345/<COMMAND>
    /will/84f3eb012345/will

 Possible values for <COMMAND> are are 
 - **firmware**: Check for a new firmware and downloads it when a new one exists on the update server.
 - **reboot**: reboot the ESP
 - **flash**: Flash the firmware with the last download
 - **ca**: download a new public key of the CA
 - **cert**: download a new public key of this ESP
 - **key**: download a new private key of this ESP

## Todos
- Initial download of ESP certificate (so same image could be used for all devices)  
- Optimize initial setup 
- Function to easyly change WiFi-Passwords after initial connection
- seperate servers for MQTT and Update, current version requires both on same server.
- Automize the include of the signing key into th sketch
- Better documentation
- Documentation of the serverpart.
 -Server-GUI to manage devices.