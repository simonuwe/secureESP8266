# secureESP8266

Framework für "secure" ESP8266 sketches with smartconfig, OTA, MQTT with SSL and check of certificates

## Features

### Configuration in SPIFFs

All configuration data is stored in files located in directory data. This directory is uploaded to the ESP8266 together with the sketch.

### Initial WiFi-connect with SmartConfig

To bring the ESP into your WiFI you have to configure the login credentials to your WiFi with an Android or iPhone App. I used the the Android App (make sure you are connected with 2.4Ghz)

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

``` JSON
{
  "downloadserver":  "[DOWLOADSERVER]",
  "downloadport":    "[DOWLOADPORT]",
  "downloadpath":    "[DOWLOADPATH]",
  "mqttserver":      "[MQTTSERVER]",
  "mqtttls":         true,
  "mqttport":        8883,
  "ntpserver":       "pool.ntp.org",
  "statusinterval":  60000,
  "waittimeout":     20000,
  "retrytimeout":    3000
}
```

At least you have to adopt the 2 servers to your environment. The **[UPDATESERVER_PORT_PATH]** is the baseurl where all downloads/updates are located. The **[MQTTSERVER]** should use TLS (**mqtttls = true**)and typically it listens on port 8883.

### Generating SSL certificates

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

Signing of your code is active when the compiler output contains the line

    ...
    Enabling binary signing
    ...
    Signed binary: /full/path/to/sketch.ino.bin.signed
    ...

It seens that Arduino-IDE does not sign binaries correctly when you do

    Sketch->export compiled binary

You have to sign it manually with

    python3 ~/.arduino15/packages/esp8266/hardware/esp8266/2.6.3/tools/signing.py --mode sign --privatekey ./private.key --bin ./secureESP8266.ino.d1_mini.bin --out secureESP8266.ino.d1_mini_signed.bin

## Include own code into the sketch

Put your code in the function

```C++
void customSetup(){
  // put your setup code here
}


void customLoop(){
  // put your loop code here
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
    /command/84f3eb012345/[COMMAND]
    /will/84f3eb012345/will

 Possible values for **[COMMAND]** are are

- **firmware**: Check for a new firmware and downloads it when a new one exists on the update server.
- **reboot**: reboot the ESP
- **flash**: Flash the firmware with the last download
- **ca**: download a new public key of the CA
- **cert**: download a new public key of this ESP
- **key**: download a new private key of this ESP

With the **status** topic the EPSs send their current status every **statusinterval** milliseconds (from config.json).
The message is a JSON like

```json
{
  "uptimemillis": 270015,
  "maxmillis":    74,
  "mqttmillis":   259618,
  "espname":      "measure-84f3eb0dbf7e",
  "espimage":     "ESP8266_WEMOS_D1MINIPRO-00.00.08.h",
  "espbuild":     "Mar  8 2020 10:19:19",
  "espip":        "192.168.1.182",
  "rssi":         -65,
  "espfreeheap":  13512,
  "errormessage": "Updateserver-certificate doesn't match"
}
```
Content of the attributes

- **uptimemillis**: Uptime of the ESP since last reboot (in msec)
- **maxmillis**: Maximal time (in msec) an execution ot eh loop() took since last reboot
- **mqttmillis**: Connection time (in msec) to MQTT-server
- **espname**:  Name of the ESP (default "measure-" + MAC address)
- **espimage**:  Name of the current running firmware image    
- **espbuild**: Build time of current firmware
- **espip**: IP-address of the device,
- **rssi**:  The last RSSI of the wiFi-connection
- **espfreeheap**: The free space in heap-memory (in bytes)
- **errormessage**: The errormessage of the last unsuccessful command


## backend

### MQTT-server
### Download-server

Downloads are triggered by commands via MQTT (as described above).

A web-server with configured HTTPS is required for downloading configurations, certificates and firmware images. To secure this connection the ESP will first check the CA of the server's SSL-certificate, to make sure that the ESP communicates with a "trusted backend".

Downloads of the configuration and certificates are only activated by a reboot of the ESP.

The EPSs will call URLs like

    https://DOWNLOADSERVER:DOWNLOADPORT/DOWNLOADPATH/FILENAME

The URL **https://DOWNLOADSERVER:DOWNLOADPORT/DOWNLOADPATH** is the prefix which is configured in the

    config.json

in attribute **downloadserver, downloadport, downloadpath**.

#### Configuration download

The EPSs will call the URL

    https://DOWNLOADSERVER:DOWNLOADPORT/DOWNLOADPATH/config.json

The server has to return HTTP 200 and the contents of the configuration file.
The file is stored as

    /config.json

on the SPIFFS

#### Firmware download

The EPSs will call an URL like

    https://DOWNLOADSERVER:DOWNLOADPORT/DOWNLOADPATH/firmware.bin?curimage=ESP8266_WEMOS_D1MINIPRO-00.00.08.g

The URL-parameter **curimage** contains the current version of the running image. The webserver has to make sure that it delivers the next version that is possible to install on the ESP and a HTPP 200 (OK).
When the **curimage** is the latest version it has to return a HTTP 304 (not modified).

**Caution:**

When you sign the images make sure that you do not skip image versions. To be able to check the signature the current public signing key is included in the images. So you have to change the key in the image of version **n** and in the signing skript in version **n+1** (one version before the new signature is used for signing).

## Todos

- Initial download of ESP certificate (so same image could be used for all devices).
- Optimize initial setup.
- Better error messages via MQTT
- Function to easily change WiFi-Passwords after initial connection
- Separate servers for MQTT and Update, current version requires both on same server.
- Automize the include of the signing key into th sketch.
- Better documentation.
- Documentation of the serverpart.
- Server-GUI to manage devices.
