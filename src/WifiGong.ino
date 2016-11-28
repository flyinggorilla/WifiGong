/*
   Requires Arduino IDE 1.6.8 or later
   ESP8266 Arduino library v2.3.0 or later
   Adafruit Huzzah ESP8266-E12, 4MB flash, uploads with 3MB SPIFFS (3MB filesystem of total 4MB) -- note that SPIFFS upload packages up everything from the "data" folder and uploads it via serial (same procedure as uploading sketch) or OTA. however, OTA is disabled by default
   Use Termite serial terminal software for debugging

   NOTE
    ESP8266 stores last set SSID and PWD in reserved flash area
    connect to SSID huzzah, and call http://192.168.4.1/api?ssid=<ssid>&pwd=<password> to set new SSID/PASSWORD

    ESP pin   - I2S signal
    ----------------------
    GPIO2/TX1   - LRCK (white)
    GPIO3/RX0   - DATA (green) - note, on the Huzzah/ESP-12, this is the pin on the chip, not on the breakout as the breakout pin is blocked through a diode!!
    GPIO15      - BCLK (yellow)
    GPIO4       - SD blue (set to GND for turning amp off)

    I2S Amplifier MAX98357A (NOTE: the Amp is not compatible with I2S left-shifted protocol which the ESP8266 is using. Thus we cannot use bit 15 from the data we put on the wire)
    LRCLK ONLY supports 8kHz, 16kHz, 32kHz, 44.1kHz, 48kHz, 88.2kHz and 96kHz frequencies.
    LRCLK clocks at 11.025kHz, 12kHz, 22.05kHz and 24kHz are NOT supported.
    Do not remove LRCLK while BCLK is present. Removing LRCLK while BCLK is present can cause unexpected output behavior including a large DC output voltage

   NOTE;
      press the onboard GIO0 button to active the Access Point mode

*/

boolean debug = true;

#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <Wire.h>
#include <EEPROM.h>
#include <FS.h>
#include <i2s.h>
#include <StreamString.h>
#include "urldecode.h"
//#include "AudioPlayer.h"
#include "AudioPlayerInterruptDriven.h"

#define FIRMWARE_VERSION __DATE__ " " __TIME__

#define DEFAULT_HOSTNAME "newgong"
#define DEFAULT_APSSID "newgong"
#define DEFAULT_SSID "gong"
#define DEFAULT_PWD "gong"

#define EEPROM_MAXSIZE 64

// ESP8266 Huzzah Pin usage
#define PIN_FACTORYRESET 0 // use onboard GPIO0
#define PIN_AMP_SD 4
/*#define PIN_LED_R 12
#define PIN_LED_G 13
#define PIN_LED_B 14*/
#define PIN_LED 14

ESP8266WebServer        httpServer ( 80 );

String playfile;
boolean volatile playgong = false;

boolean wifiStationOK = false;
boolean wifiAPisConnected = false;
boolean wifiConfigMode = true;
boolean httpClientOnlyMode = false;
long uploadSize = 0;


//format bytes
String formatBytes(size_t bytes) {
  if (bytes < 1024) {
    return String(bytes) + "B";
  } else if (bytes < (1024 * 1024)) {
    return String(bytes / 1024.0) + "KB";
  } else if (bytes < (1024 * 1024 * 1024)) {
    return String(bytes / 1024.0 / 1024.0) + "MB";
  } else {
    return String(bytes / 1024.0 / 1024.0 / 1024.0) + "GB";
  }
}




void eepromSet(String content) {
  EEPROM.begin(EEPROM_MAXSIZE); // note this allocates a buffer in RAM

  // write string content
  int len = content.length();
  if (len >= EEPROM_MAXSIZE) {
    len = EEPROM_MAXSIZE-1;
  }
  int addr = 0;
  while (addr < len) {
    EEPROM.write(addr, content.charAt(addr));
    addr++;
  }

  // fill the remaining eeprom buffer with zeros
  while (addr < EEPROM_MAXSIZE) {
    EEPROM.write(addr, 0);
    addr++;
  }
  EEPROM.end(); // commits EEPROM contents to flash if changed/written; releases allocated memory
}


String eepromRead() {
  //EEPROM access to retreive the hostname
  EEPROM.begin(EEPROM_MAXSIZE); // note this allocates a buffer in RAM
  int address = 0;
  //char eepromcontent[EEPROM_MAXSIZE];
  String eepromcontent;
  while (address < EEPROM_MAXSIZE) {
    char val = EEPROM.read(address);
    //eepromcontent[address] = val;
    if (!val) {
      break;
    }
    eepromcontent += val;
    address++;
  }
  EEPROM.end(); // commits EEPROM contents to flash if changed/written; releases allocated memory

  if (debug) {
    Serial.println("EEPROM bytes: " + String(address) + " data: " + eepromcontent);
  }

  if (address >= EEPROM_MAXSIZE) { // no term zero found, so reset the EEPROM to the default
    eepromcontent = DEFAULT_HOSTNAME;
    eepromSet(eepromcontent);
  }
  return eepromcontent;
}






// Handle Factory reset and enable Access Point mode to enable configuration
void handleFactoryReset() {
  if (digitalRead(PIN_FACTORYRESET) == LOW) {
    if (millis() < 10000) {  // ignore reset button during first 10 seconds of reboot to avoid looping resets due to fast booting of ESP8266
      return;
    }
    if (debug) {
      Serial.println(F("*********FACTORYRESET***********"));
      //WiFi.printDiag(Serial);
    }
    WiFi.disconnect(false); //disconnect and disable station mode; delete old config
    // default IP address for Access Point is 192.168.4.1
    //WiFi.softAPConfig(IPAddress(192,168,4,1), IPAddress(192,168,4,1), IPAddress(255,255,255,0)); // IP, gateway, netmask -- NOT STORED TO FLASH!!
    WiFi.softAP(DEFAULT_APSSID); // default is open Wifi
    //WiFi.mode(WIFI_AP);
    if (debug) {
      Serial.println("Wifi reset to SSID: " + WiFi.SSID() + " pass: " + WiFi.psk());
      Serial.println("Wifi config mode enabled: Access point enabled at open Wifi SSID: " DEFAULT_APSSID);
      Serial.println("Restarting....");
      //WiFi.printDiag(Serial);
      Serial.flush();
    }
    ESP.restart();

  }
}

// send a HTML response about reboot progress
// reboot device therafter.
void httpReboot(String message) {
  String response = F("<!DOCTYPE html>"
                    "<html>"
                    "<head>"
                    "<title>Dynatrace UFO configuration changed. Rebooting now... </title>"
                    "<META http-equiv='refresh' content='10;URL=/'>"
                    "</head>"
                    "<body>"
                    "<center>");
  response +=             message;
  response +=            F("Device is being rebooted. Redirecting to homepage in 10 seconds...</center>"
                         "</body>"
                         "</html>");

  httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
  httpServer.send(200, F("text/html"), response);
  ESP.restart();
}

// send a HTML response about reboot progress
// reboot device therafter.
void httpUploadFinished(String message) {
  String response = F("<!DOCTYPE html>"
                    "<html>"
                    "<head>"
                    "<title>Dynatrace UFO file uploaded... </title>"
                    "<META http-equiv='refresh' content='5;URL=/'>"
                    "</head>"
                    "<body>"
                    "<center>");
  response +=             message;
  response +=            F("You will be redirected to the homepage shortly....</center>"
                         "</body>"
                         "</html>");

  httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
  httpServer.send(200, F("text/html"), response);
}

// HTTP not found response
void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += httpServer.uri();
  message += "\nMethod: ";
  message += ( httpServer.method() == HTTP_GET ) ? "GET" : "POST";
  message += "\nArguments: ";
  message += httpServer.args();
  message += "\n";

  for ( uint8_t i = 0; i < httpServer.args(); i++ ) {
    message += " " + httpServer.argName ( i ) + ": " + httpServer.arg ( i ) + "\n";
  }

  httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
  httpServer.send ( 404, F("text/plain"), message );
}

void infoHandler() {

  String json = "{";
  json += "\"heap\":\"" + String(ESP.getFreeHeap()) + "\"";
  json += ", \"ssid\":\"" + String(WiFi.SSID()) + "\"";
  json += ", \"ipaddress\":\"" + WiFi.localIP().toString() + "\"";
  json += ", \"ipgateway\":\"" + WiFi.gatewayIP().toString() + "\"";
  json += ", \"ipdns\":\"" + WiFi.dnsIP().toString() + "\"";
  json += ", \"ipsubnetmask\":\"" + WiFi.subnetMask().toString() + "\"";
  json += ", \"macaddress\":\"" + WiFi.macAddress() + "\"";
  json += ", \"hostname\":\"" + WiFi.hostname() + "\"";
  json += ", \"apipaddress\":\"" + WiFi.softAPIP().toString() + "\"";
  json += ", \"apconnectedstations\":\"" + String(WiFi.softAPgetStationNum()) + "\"";
  json += ", \"wifiautoconnect\":\"" + String(WiFi.getAutoConnect()) + "\"";
  json += ", \"firmwareversion\":\"" + String(FIRMWARE_VERSION) + "\"";
  json += "}";
  httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
  httpServer.send(200, "text/json", json);
  json = String();
}



void fileHandler() {
  if (httpServer.hasArg(F("play"))) {
    if (!playgong) {
      playfile = urldecode(httpServer.arg("play"));
      Serial.println("initiating playing of: " + playfile);
      playgong = true;
    }
  }

/*  if (httpServer.hasArg(F("synth"))) {
    if (httpServer.arg(F("synth")) == "8k")
      playGeneratorGong(8000);
    else if (httpServer.arg(F("synth")) == "16k")
      playGeneratorGong(16000);
    else if (httpServer.arg(F("synth")) == "44k")
      playGeneratorGong(44100);
  }*/


  if (httpServer.hasArg(F("delete"))) {
    SPIFFS.remove(httpServer.arg(F("delete")));
  }

  String response = F("<!DOCTYPE html>"
                    "<html>"
                    "<head>"
                    "<title>WIFIGONG Files</title>"
                    "</head>"
                    "<body>"
                    "<a href=\"/\">home</a>"
                    "<ul>");

  String fileList;
  Dir dir = SPIFFS.openDir("/");
  while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      fileList += "<li><a href=\"files?play=" + fileName /*urlencode(fileName)*/ + "\">" + fileName + "</a> size: " + formatBytes(fileSize);
      fileList += "  <a href=\"files?delete=" + fileName /*urlencode(fileName)*/ + "\">delete</a>";
      fileList += "  <a href=\"files?show=" + fileName /*urlencode(fileName)*/ + "\">show</a></li>";
  }
  response +=             fileList + F("</ul>");
  response +=  F("<ul><li><a href=\"files?synth=8k\">8kHz</a> <a href=\"files?synth=16k\">16kHz</a> <a href=\"files?synth=44k\">44.1kHz</a></li></ul>");
  FSInfo info;
  if (SPIFFS.info(info)) {
    response +=        "<p> disk size: " + formatBytes(info.totalBytes) + " disk free: " + formatBytes(info.totalBytes - info.usedBytes) + "</p>";

  }
  response += "<p> last file played: " + playfile + "</p";
  /*if (httpServer.hasArg(F("show"))) {
    if (initAudioFile(httpServer.arg(F("show")))) {
      String dump = F("<br>***DUMP***<br>");

      for (unsigned int i = 0; i < 32; i++) {
        if (!getAudioFileSample()) break;
        dump += "<br>"+String(i)+": "+String(audioSample)+" | " + String((int16_t)audioSample) + " | " + String(convertSinged2UnsignedSample(audioSample));
      }

      response+= dump;
    }
    audioFile.close();
  }*/

  response +=            F("</body>"
                           "</html>");
  httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
  httpServer.send(200, F("text/html"), response);

}

// convert a signed 16bit PCM value to an unsigned value 0 to 65535
uint16_t convertSinged2UnsignedSample(uint16_t signedSample) {
  if (signedSample & 0b1000000000000000) {  // negative value
    //return ~signedSample + 0x0001; //
    return 0x7FFF + (int16_t)signedSample;
  }
  return signedSample + 0x7FFF;
}


void apiHandler() {
  if (httpServer.hasArg(F("gong"))) {
    Serial.println("GONG" + httpServer.arg("gong"));
    playgong = true;
  }

  if (httpServer.hasArg(F("hostname"))) {
    String newWifiHostname = httpServer.arg(F("hostname"));
    eepromSet(newWifiHostname);
  }

  // note its required to provide both arguments SSID and PWD
  if (httpServer.hasArg("ssid") && httpServer.hasArg("pwd")) {
    String newWifiSSID = httpServer.arg("ssid");
    String newWifiPwd = httpServer.arg("pwd");

    // if SSID is given, also update wifi credentials
    if (newWifiSSID.length()) {
      WiFi.mode(WIFI_STA);
      WiFi.begin(newWifiSSID.c_str(), newWifiPwd.c_str() );
    }

    if (debug) {
      Serial.println("New Wifi settings: " + newWifiSSID + " / " + newWifiPwd);
      Serial.println("Restarting....");
      Serial.flush();
    }

    httpReboot("New WIFI settings accepted. Mac address: " + WiFi.macAddress() + "<p/>");

  }

  httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
  httpServer.send(200);
}


void generateHandler() {
  if (httpServer.hasArg("size")) {
    Serial.println("size arg found" + httpServer.arg("size"));
    long bytes = httpServer.arg("size").toInt();
    String top = "<html><header>Generator</header><body>sending " + String(bytes) + " bytes of additional payload.<p>";
    String end = F("</body></html>");
    httpServer.setContentLength(bytes + top.length() + end.length());
    httpServer.sendHeader(F("cache-control"), F("private, max-age=0, no-cache, no-store"));
    httpServer.send(200);
    String chunk = "";
    httpServer.sendContent(top);
    String a = String("a");
    while (bytes > 0) {
      chunk = String("");
      long chunklen = bytes < 4096 ? bytes : 4096;
      while (chunk.length() <= chunklen) {
        chunk += a;
      }
      httpServer.sendContent(chunk);
      bytes -= chunklen;
    }
    httpServer.sendContent(end);
  }
}

bool rebootAfterUpload = false;
void updatePostHandler() {
  // handler for the /update form POST (once file upload finishes)
  //httpServer.sendHeader("Connection", "close");
  //httpServer.sendHeader("Access-Control-Allow-Origin", "*");
  StreamString error;
  if (Update.hasError()) {
    Update.printError(error);
  }
  if (rebootAfterUpload) {
    httpReboot((Update.hasError()) ? error : "Upload succeeded! " + String(uploadSize) + " bytes transferred<p>");
  } else {
    httpUploadFinished("Upload succeeded! " + String(uploadSize) + " bytes transferred<p>");
  }
}

String parseFileName(String &path) {
  String filename;
  int lastIndex = path.lastIndexOf('\\');
  if (lastIndex < 0) {
    lastIndex = path.lastIndexOf('/');
  }
  if (lastIndex > 0) {
    filename = path.substring(lastIndex + 1);
  } else {
    filename = path;
  }

  filename.toLowerCase();
  return filename;
}

File uploadFile;
void updatePostUploadHandler() {
  // handler for the file upload, get's the sketch bytes, and writes
  // them through the Update object
  HTTPUpload& upload = httpServer.upload();
  String filename = parseFileName(upload.filename);

  rebootAfterUpload = false;

  if (filename.endsWith(".bin")) { // handle firmware upload
    rebootAfterUpload = true;
    if (upload.status == UPLOAD_FILE_START) {
      //WiFiUDP::stopAll(); needed for MDNS or the like?
      if (debug) Serial.println("Update: " + upload.filename);
      uint32_t maxSketchSpace = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
      if (!Update.begin(maxSketchSpace)) { //start with max available size
        if (debug) Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (debug) Serial.print(".");
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        if (debug) Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      uploadSize = Update.size();
      if (Update.end(true)) { //true to set the size to the current progress
        if (debug) Serial.println("Update Success - uploaded: " + String(upload.totalSize) + ".... rebooting now!");
      } else {
        if (debug) Update.printError(Serial);
      }
      if (debug) Serial.setDebugOutput(false);
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      uploadSize = Update.size();
      Update.end();
      if (debug) Serial.println("Update was aborted");
    }
  } else { // handle file upload
    if (upload.status == UPLOAD_FILE_START) {
      if (debug) Serial.println("uploading to SPIFFS: /" + filename);
      uploadFile = SPIFFS.open("/" + filename, "w");
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (debug) Serial.print(".");
      if (uploadFile.write(upload.buf, upload.currentSize) != upload.currentSize) {
        if (debug) Serial.println("ERROR writing file " + String(uploadFile.name()) + "to SPIFFS.");
        uploadFile.close();
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      uploadSize = upload.totalSize;
      if (uploadFile.size() == upload.totalSize) {
        if (debug) Serial.println("Upload to SPIFFS Succeeded - uploaded: " + String(upload.totalSize));
      } else {
        if (debug) Serial.println("Upload to SPIFFS FAILED: " + String(uploadFile.size()) + " bytes of " + String(upload.totalSize));
      }
      uploadFile.close();
    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      uploadSize = upload.totalSize;
      uploadFile.close();
      if (debug) Serial.println("Upload to SPIFFS was aborted");
    }
  }

  yield();
}

/*
  void indexHtmlHandler() {
    httpServer.send(200, "text/html", String(indexHtml));
    httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
  }*/

// handles GET request to "/update" and redirects to "index.html/#!pagefirmwareupdate"
void updateHandler() {
  httpServer.sendHeader("cache-control", "private, max-age=0, no-cache, no-store");
  httpServer.send(301, "text/html", "/#!pagefirmwareupdate");
}


void WiFiEvent(WiFiEvent_t event) {
    switch (event) {
      case WIFI_EVENT_STAMODE_GOT_IP:
        if (debug) Serial.println("WiFi connected. IP address: " + String(WiFi.localIP().toString()) + " hostname: " + WiFi.hostname() + "  SSID: " + WiFi.SSID());
        wifiStationOK = true;
#ifdef ENABLE_SDDP
        startSSDP();
#endif
        break;
      case WIFI_EVENT_STAMODE_DISCONNECTED:
        if (debug) Serial.println(F("WiFi client lost connection"));
        wifiStationOK = false;
        break;
      case WIFI_EVENT_STAMODE_CONNECTED:
        if (debug) Serial.println(F("WiFi client connected"));
        break;
      case WIFI_EVENT_STAMODE_AUTHMODE_CHANGE:
        if (debug) Serial.println(F("WiFi client authentication mode changed."));
        break;
      //case WIFI_STAMODE_DHCP_TIMEOUT:                             THIS IS A NEW CONSTANT ENABLE WITH UPDATED SDK
      //  Serial.println("WiFi client DHCP timeout reached.");
      //break;
      case WIFI_EVENT_SOFTAPMODE_STACONNECTED:
        if (debug) Serial.println("WiFi accesspoint: new client connected. Clients: "  + String(WiFi.softAPgetStationNum()));
        if (WiFi.softAPgetStationNum() > 0) {
          wifiAPisConnected = true;
        }
        break;
      case WIFI_EVENT_SOFTAPMODE_STADISCONNECTED:
        if (debug) Serial.println("WiFi accesspoint: client disconnected. Clients: " + String(WiFi.softAPgetStationNum()));
        if (WiFi.softAPgetStationNum() > 0) {
          wifiAPisConnected = true;
        } else {
          wifiAPisConnected = false;
        }
        break;
      case WIFI_EVENT_SOFTAPMODE_PROBEREQRECVED:
        //Serial.println("WiFi accesspoint: probe request received.");
        break;
    }

}

void printSpiffsContents() {
  if (debug)
  {
    Dir dir = SPIFFS.openDir("/");
    while (dir.next()) {
      String fileName = dir.fileName();
      size_t fileSize = dir.fileSize();
      Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
    Serial.printf("\n");
  }
}

void setupSerial() {
  // checking availability of serial connection
  if (debug){
    int serialtimeout = 5000; //ms
    Serial.begin ( 115200 );
    while (!Serial) {
      if (serialtimeout > 0) {
        serialtimeout -= 50;
      } else {
        debug = false;
        break;
      }
      delay(50);
    }
  }

  if (debug) {
    Serial.println("");
    Serial.println("");
    Serial.println("");
    Serial.println(F("Welcome to Bernd's Wifi Gong!"));
    Serial.println(F("GONG Firmware Version: " FIRMWARE_VERSION));
    Serial.println("ESP8266 Bootversion: " + String(ESP.getBootVersion()));
    Serial.println("ESP8266 SDK Version: " + String(ESP.getSdkVersion()));
    Serial.println("Resetinfo: " + ESP.getResetInfo());
  }
}


// initialization routines
void setup ( void ) {
  setupSerial();

  pinMode(PIN_FACTORYRESET, INPUT_PULLUP); //, INPUT_PULLUP); use INPUT_PULLUP in case we put reset to ground; currently reset is doing a 3V signal
  pinMode(PIN_AMP_SD, OUTPUT); // turn amp on/off; (particulariyl off - to ground - to save power and to avoid noise)
  //digitalWrite(PIN_AMP, LOW);
  /*pinMode(PIN_LED_R, OUTPUT);
  pinMode(PIN_LED_G, OUTPUT);
  pinMode(PIN_LED_B, OUTPUT);

  digitalWrite(PIN_LED_R, HIGH);
  digitalWrite(PIN_LED_G, HIGH);
  digitalWrite(PIN_LED_B, HIGH);*/

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);


  // retrieves the hostname
  String hostname = eepromRead();

  // initialize ESP8266 file system
  SPIFFS.begin();
  printSpiffsContents();

  // initialize Wifi based on stored config
  WiFi.onEvent(WiFiEvent);
  WiFi.hostname(hostname); // note, the hostname is not persisted in the ESP config like the SSID. so it needs to be set every time WiFi is started
  wifiConfigMode = WiFi.getMode() & WIFI_AP;

  if (debug) {
    Serial.println("Connecting to Wifi SSID: " + WiFi.SSID() + " as host " + WiFi.hostname());
    if (wifiConfigMode) {
      Serial.println("WiFi Configuration Mode - Access Point IP address: " + WiFi.softAPIP().toString());
    }
    //WiFi.printDiag(Serial);
  }


  // setup all web server routes; make sure to use / last
  #define STATICFILES_CACHECONTROL "private, max-age=0, no-cache, no-store"
  httpServer.on ( "/api", apiHandler );
  httpServer.on ( "/info", infoHandler );
  httpServer.on ( "/files", fileHandler);
  httpServer.on ("/gen", generateHandler);
  httpServer.serveStatic("/font.woff", SPIFFS, "/font.woff");
  httpServer.serveStatic("/font.eot", SPIFFS, "/font.eot");
  httpServer.serveStatic("/font.svg", SPIFFS, "/font.svg");
  httpServer.serveStatic("/font.ttf", SPIFFS, "/font.ttf");

  httpServer.serveStatic("/", SPIFFS, "/index.html", STATICFILES_CACHECONTROL);
  httpServer.serveStatic("/index.html", SPIFFS, "/index.html", STATICFILES_CACHECONTROL);
  httpServer.onNotFound ( handleNotFound );

  // register firmware update HTTP server:
  //    To upload through terminal you can use: curl -F "image=@ufo.ino.bin" ufo.local/update
  //    Windows power shell use DOESNT WORK YET: wget -Method POST -InFile ufo.ino.bin -Uri ufo.local/update
  httpServer.on("/update", HTTP_GET, updateHandler);
  httpServer.on("/update", HTTP_POST, updatePostHandler, updatePostUploadHandler);

  httpServer.begin();



}

/*
class AudioPlayerWithLEDlight : public AudioPlayer {
public:
    AudioPlayerWithLEDlight(int pinApmplifierSD) : AudioPlayer(pinApmplifierSD) { };
    bool playFile(String);
    bool nextSample();
    unsigned long millisecs;
    unsigned long lastmillis;
    unsigned int slowtick;

};

bool AudioPlayerWithLEDlight::playFile(String filename) {
   millisecs = lastmillis = millis();
   slowtick = 0;
   analogWrite(PIN_LED_R, HIGH);
   bool ret = AudioPlayer::playFile(filename);
   digitalWrite(PIN_LED_R, HIGH);
   digitalWrite(PIN_LED_G, HIGH);
   analogWrite(PIN_LED_B, 900);
   return ret;
}


bool AudioPlayerWithLEDlight::nextSample() {
  millisecs = millis();
  slowtick ++;
  if (slowtick < 1000) {
      digitalWrite(PIN_LED_R, 0);
      digitalWrite(PIN_LED_G, 0);
      digitalWrite(PIN_LED_B, 0);
  } else if (slowtick < 16000*5) {
    digitalWrite(PIN_LED_R, 0);
    digitalWrite(PIN_LED_G, 1);
    digitalWrite(PIN_LED_B, 0);
  } else {
    digitalWrite(PIN_LED_R, 1);
    digitalWrite(PIN_LED_G, 1);
    digitalWrite(PIN_LED_B, 0);
  }

  if ((millisecs - lastmillis) > 1000) {
    lastmillis = millisecs;
    slowtick++;
    unsigned int t = slowtick % 3;
    Serial.println("slowtick: " + String(slowtick) + "slowtick module: " + String(t));

    if (t = 0) {
      digitalWrite(PIN_LED_R, 0); // max 1023 ---- but note that 1023 means LED OFF!!!
    } else {
      digitalWrite(PIN_LED_R, 255);
    }

    if (t = 1) {
      digitalWrite(PIN_LED_G, 0); // max 1023 ---- but note that 1023 means LED OFF!!!
    } else {
      digitalWrite(PIN_LED_G, 255);
    }

    if (t = 3) {
      digitalWrite(PIN_LED_B, 0); // max 1023 ---- but note that 1023 means LED OFF!!!
    } else {
      digitalWrite(PIN_LED_B, 255);
    }

  }
  analogWrite(PIN_LED_R, 1023 - (millisecs % 900));

  return AudioPlayer::nextSample();
}*/


 AudioPlayer audioPlayer(PIN_AMP_SD);
//AudioPlayerInterruptDriven audioPlayer(PIN_AMP_SD);
 //AudioPlayer audioPlayer(PIN_AMP_SD);

void loop ( void ) {
  handleFactoryReset();
  httpServer.handleClient();
  if (playgong) {
    delay(100); // hack to allow Webserver to respond
    digitalWrite(PIN_LED, HIGH);
    audioPlayer.playFile(playfile);
    digitalWrite(PIN_LED, LOW);
    playgong = false;
  }
}
