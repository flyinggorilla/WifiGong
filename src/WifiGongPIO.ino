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

    I2S Amplifier MAX98357A
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
#include <FS.h>
#include <i2s.h>
#include <StreamString.h>
#include "urldecode.h"
#include "AudioPlayer.h"

#define FIRMWARE_VERSION __DATE__ " " __TIME__

#define DEFAULT_HOSTNAME "wifigong"
#define DEFAULT_APSSID "newgong"
#define DEFAULT_SSID "gong"
#define DEFAULT_PWD "gong"

// ESP8266 Huzzah Pin usage
#define PIN_FACTORYRESET 0 // use onboard GPIO0
#define PIN_AMP_SD 4

ESP8266WebServer        httpServer ( 80 );
boolean playgong = false;

boolean wifiStationOK = false;
boolean wifiAPisConnected = false;
boolean wifiConfigMode = true;
boolean httpClientOnlyMode = false;
long uploadSize = 0;


// AUDIO STUFF
uint16_t audioSample;
unsigned int audioSampleRate;
unsigned int bitsPerSample;
File audioFile = File();




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

  if (httpServer.hasArg(F("synth"))) {
    if (httpServer.arg(F("synth")) == "8k")
      playGeneratorGong(8000);
    else if (httpServer.arg(F("synth")) == "16k")
      playGeneratorGong(16000);
    else if (httpServer.arg(F("synth")) == "44k")
      playGeneratorGong(44100);
  }


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
  if (httpServer.hasArg(F("show"))) {
    if (initAudioFile(httpServer.arg(F("show")))) {
      String dump = F("<br>***DUMP***<br>");

      for (unsigned int i = 0; i < 32; i++) {
        if (!getAudioFileSample()) break;
        dump += "<br>"+String(i)+": "+String(audioSample)+" | " + String((int16_t)audioSample) + " | " + String(convertSinged2UnsignedSample(audioSample));
      }

      response+= dump;
    }
    audioFile.close();
  }

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
  if (httpServer.hasArg("hostname")) {
    String newWifiHostname = httpServer.arg("hostname");
    //TODO##################################################################
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

int readWav();

// initialization routines
void setup ( void ) {
  setupSerial();

  pinMode(PIN_FACTORYRESET, INPUT_PULLUP); //, INPUT_PULLUP); use INPUT_PULLUP in case we put reset to ground; currently reset is doing a 3V signal
  pinMode(PIN_AMP_SD, OUTPUT); // turn amp on/off; (particulariyl off - to ground - to save power and to avoid noise)
  //digitalWrite(PIN_AMP, LOW);

  // initialize ESP8266 file system
  SPIFFS.begin();
  printSpiffsContents();

  // initialize Wifi based on stored config
  WiFi.onEvent(WiFiEvent);
  WiFi.hostname(DEFAULT_HOSTNAME); // note, the hostname is not persisted in the ESP config like the SSID. so it needs to be set every time WiFi is started
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




// WAV mono 8kHz = 125microseconds per sample
// 8bit sample devided by 29 to convert to 9 possible PWM states 0.....8 HIGH for 8 pwm ticks
#define PWMFREQUENCY 96000

#include "pins_arduino.h"

void t1IntHandler();


bool initAudioFile(String fileName) {
    // WAV format spec: http://soundfile.sapp.org/doc/WaveFormat/

    audioFile = SPIFFS.open(fileName, "r");
    if (!audioFile) {
      Serial.println("Could not open : " + fileName);
    }
    size_t size = audioFile.size();
    Serial.println("Opening file: " + fileName + " size: " + String(size));

    uint32_t uint32buf = 0;
    // read ChunkID "RIFF" header
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading header of file.");
      return false;
    }
    if (uint32buf != 0x46464952) {
      Serial.println("No RIFF format header ");
      Serial.print("header: "); Serial.println(uint32buf, HEX);
      return false;
    }
    // read Chunksize: remaining file size
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading size header.");
      return false;
    }
    Serial.println("Chunksize from here: " + String(uint32buf));

    // read Format header: "WAVE"
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error format header.");
      return false;
    }
    if (uint32buf != 0x45564157 ) {
      Serial.println("No WAVE format header");
      return false;
    }

    // read Subchunk1ID: Format header: "fmt "
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error subchunk header.");
      return false;
    }
    if (uint32buf != 0x20746d66  ) {
      Serial.println("No 'fmt ' format header");
      return false;
    }

    // read subChunk1size
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading subchunk1 size.");
      return false;
    }
    Serial.println("subChunk1size: " + String(uint32buf));

    uint16_t uint16buf;

    // read AudioFormat
    if (!audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading audioformat.");
      return false;
    }
    if (uint16buf != 1  ) {
      Serial.println("Invalid audio format");
      return false;
    }

    // read NumChannels
    if (!audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading NumChannels.");
      return false;
    }
    if (uint16buf != 1  ) {
      Serial.println("Too many channels. Only MONO files accepted. No Stereo.");
      return false;
    }


    // read sample rate
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading sample rate");
      return false;
    }
    audioSampleRate = uint32buf;
    Serial.println("Sample rate: " + String(audioSampleRate));

    // read byte rate
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading byte rate");
      return false;
    }
    Serial.println("Byte rate: " + String(uint32buf));

   // read BlockAlign
    if (!audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading block align.");
      return false;
    }
    Serial.println("Block align: " + String(uint16buf));

   // read BitsPerSample
    if (!audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading bits per sample.");
      return false;
    }
    bitsPerSample = uint16buf;
    Serial.println("Bits per sample: " + String(bitsPerSample) + " bits");

    // read Subchunk2ID: Format header: "data"
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error data header.");
      return false;
    }
    if (uint32buf != 0x61746164   ) {
      Serial.println("No 'data' header");
      return false;
    }

    // read subChunk1size
    if (!audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading subchunk2/data size.");
      return false;
    }
    Serial.println("data size / subChunk2size: " + String(uint32buf));

    return true;
}

bool getAudioFileSample() {
    if (bitsPerSample == 8) {
      uint8_t eightbitsample;
      if (!audioFile.read(&eightbitsample, 1)) {
        return false;
      }
      audioSample = eightbitsample;
      //audioSample = audioSample << 6; // scale 8 bit audio to 16 bit
      audioSample = (audioSample - 128)*128; // convert to signed and scale
    } else {

      if (!audioFile.read((uint8_t*)&audioSample, 2)) {
        return false;
      }
      //audioSample = audioSample + 0x7FFF; // convert signed to unsigned
      //audioSample = convertSinged2UnsignedSample(audioSample)/2;


    }

}



/*
ESP pin   - I2S signal
----------------------
GPIO2/TX1   - LRCK
GPIO3/RX0   - DATA - note, on the Huzzah/ESP-12, this is the pin on the chip, not on the breakout as the breakout pin is blocked through a diode!!
GPIO15      - BCLK

I2S Amplifier MAX98357A
LRCLK ONLY supports 8kHz, 16kHz, 32kHz, 44.1kHz, 48kHz, 88.2kHz and 96kHz frequencies.
LRCLK clocks at 11.025kHz, 12kHz, 22.05kHz and 24kHz are NOT supported.
Do not remove LRCLK while BCLK is present. Removing LRCLK while BCLK is present can cause unexpected output behavior including a large DC output voltage

*/





// taken from source: core_esp8266_i2s.c
#define SLC_BUF_CNT (8) //Number of buffers in the I2S circular buffer
#define SLC_BUF_LEN (64) //Length of one buffer, in 32-bit words.
//--> 512 the buffer can hold 512 samples. we want it always at least 75% full, so we must more than 500 fills per second for 44khz

void t1I2SIntHandler();

unsigned int bufferFillsPerSecond;
unsigned int statisticDMAfull;
unsigned int statisticsSamples;

void playI2SGong(String audioFileName) {
    if (!initAudioFile(audioFileName))
      return;
    bufferFillsPerSecond = audioSampleRate * 32 / (SLC_BUF_CNT*SLC_BUF_LEN) ;
    WiFi.enableSTA(false);
    digitalWrite(PIN_I2SAMP, HIGH); // turn amp on
    i2s_begin();
    i2s_set_rate(audioSampleRate);
    audioEnded = false;

    timer1_disable();
    timer1_isr_init();
    timer1_attachInterrupt(t1I2SIntHandler);
    timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
    timer1_write((clockCyclesPerMicrosecond() * 1000000) / bufferFillsPerSecond);
}

ICACHE_RAM_ATTR void t1I2SIntHandler() {
  while (!i2s_is_full()) {
    if (getAudioFileSample()) {
      i2s_write_sample(audioSample); // BLOCKING if FULL!
      statisticsSamples++;
    } else {
      audioEnded = true;
      digitalWrite(PIN_I2SAMP, LOW); // turn amp off
      i2s_end();
      timer1_disable();
      audioFile.close();
      Serial.println("Statistics -- total samples: " + String(statisticsSamples) + " DMA full: " + String(statisticDMAfull));
      WiFi.enableSTA(true);
      return;
    }
  }
  statisticDMAfull++;
}

void generatorIntHandler();

void playGeneratorGong(uint32_t sampleRate) {
    audioSampleRate = sampleRate;


    bufferFillsPerSecond = audioSampleRate * 8 / (SLC_BUF_CNT*SLC_BUF_LEN) ;
    audioTick = 0; // for generator only needed

    digitalWrite(PIN_I2SAMP, HIGH); // turn amp on
    i2s_begin();
    i2s_set_rate(audioSampleRate);
    audioEnded = false;

    timer1_disable();
    timer1_isr_init();
    timer1_attachInterrupt(generatorIntHandler);
    timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
    timer1_write((clockCyclesPerMicrosecond() * 1000000) / bufferFillsPerSecond);
}

boolean getGeneratorSample() {
  float v = sin((float)(audioTick*440*2)*PI/audioSampleRate);
  if (audioTick > audioSampleRate*5)
    audioSample = v*0x7FFE + 0x7FFF;
  else
    audioSample = v*1024 + 1024;

  audioSample = convertSinged2UnsignedSample(audioSample);

  if (audioTick > audioSampleRate*10)
    return false;

  audioTick++;
  return true;
}
ICACHE_RAM_ATTR void generatorIntHandler() {
  while (!i2s_is_full()) {
    if (getGeneratorSample()) {
      i2s_write_sample(audioSample);
      //i2s_write_lr(audioSample, audioSample);

    } else {
      audioEnded = true;
      digitalWrite(PIN_I2SAMP, LOW); // turn amp off
      i2s_end();
      timer1_disable();
      audioFile.close();
      return;
    }
  }
}

void playDirect(String audioFileName) {
  if (!initAudioFile(audioFileName))
    return;

  digitalWrite(PIN_I2SAMP, HIGH); // turn amp on
  i2s_begin();
  i2s_set_rate(audioSampleRate);

  wdt_enable(1000*60);


  while (true) {
    if (getAudioFileSample()) {
      wdt_disable();
      i2s_write_sample(audioSample); // BLOCKING if FULL!
    } else {
      digitalWrite(PIN_I2SAMP, LOW); // turn amp off
      i2s_end();
      audioFile.close();
      wdt_enable(1000*60);
      return;
    }
  }

}


void loop ( void ) {
  handleFactoryReset();
  httpServer.handleClient();
  if (playgong) {
    delay(100);
    playDirect(playfile);
    playgong = false;
  }

}
