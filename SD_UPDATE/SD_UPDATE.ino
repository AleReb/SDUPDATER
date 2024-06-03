/*
  SDWebServer - Example WebServer with SD Card backend for ESP32

  Copyright (c) 2015 Hristo Gochkov. All rights reserved.
  This file is part of the WebServer library for the Arduino environment.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

  Description:
  This code implements a WebServer with SD Card backend for ESP32. The root of the web server is the root folder of the SD Card.
  The SD card should be formatted with FAT, and file extensions longer than 3 characters are not supported by the SD Library.
  File names longer than 8 characters will be truncated by the SD library, so keep filenames shorter.
  The default index file is index.htm, and it works on subfolders as well.

  To retrieve the contents of the SD card, visit http://esp32sd.local/list?dir=/
  The 'dir' parameter needs to be passed to the PrintDirectory function via an HTTP GET request.

  Modifications by Alejandro Rebolledo:
  - Integrated an OTA (Over-the-Air) update system.
  - Added a button to activate and deactivate the system.
  - The system operates as an Access Point (AP) without internet, making it necessary to include all required files (e.g., JavaScript, fonts) on the SD card.
  - The update file for the ESP32 must be named 'update.bin'.
  - There is no version control implemented, but this basic function is sufficient to avoid having to connect the system frequently, as long as the file order and naming conventions are followed.

  Special Considerations:
  - Since the system runs as an AP without internet, any additional resources (e.g., JavaScript files, fonts) must be stored on the SD card.
  - Ensure the SD card contains all necessary files for the web interface to function correctly.
  - The system automatically checks for an 'update.bin' file on the SD card during startup and initiates the update process if found.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <ESPmDNS.h>
#include <SPI.h>
#include <SD.h>
#include <Update.h>

#define SD_MISO 2
#define SD_MOSI 15
#define SD_SCLK 14
#define SD_CS_PIN 13
#define BUTTON_PIN 39  // Define el pin del botón
SPIClass spiSD(HSPI);  // HSPI
String ssid;
String password;  // Password for AP
String myIPG;
String ver = "VER: 4";
WebServer server(80);

static bool hasSD = false;
File uploadFile;
bool systemActive = false;
volatile bool buttonPressed = false;
bool activateSystemFlag = false;
bool deactivateSystemFlag = false;
// Configura la pantalla
U8G2_SH1107_SEEED_128X128_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);


bool clientConnected = false;                 // Variable para indicar si hay una conexión activa
unsigned long lastActivityTime = 0;           // Tiempo de la última actividad
const unsigned long timeoutInterval = 30000;  // Intervalo de tiempo de inactividad (30 segundos)

void returnOK() {
  server.send(200, "text/plain", "");
}

void returnFail(String msg) {
  server.send(500, "text/plain", msg + "\r\n");
}

bool loadFromSdCard(String path) {
  String dataType = "text/plain";
  if (path.endsWith("/")) {
    path += "index.htm";
  }

  if (path.endsWith(".src")) {
    path = path.substring(0, path.lastIndexOf("."));
  } else if (path.endsWith(".htm")) {
    dataType = "text/html";
  } else if (path.endsWith(".css")) {
    dataType = "text/css";
  } else if (path.endsWith(".js")) {
    dataType = "application/javascript";
  } else if (path.endsWith(".png")) {
    dataType = "image/png";
  } else if (path.endsWith(".gif")) {
    dataType = "image/gif";
  } else if (path.endsWith(".jpg")) {
    dataType = "image/jpeg";
  } else if (path.endsWith(".ico")) {
    dataType = "image/x-icon";
  } else if (path.endsWith(".xml")) {
    dataType = "text/xml";
  } else if (path.endsWith(".pdf")) {
    dataType = "application/pdf";
  } else if (path.endsWith(".zip")) {
    dataType = "application/zip";
  } else if (path.endsWith(".csv")) {
    dataType = "text/csv";
  }

  File dataFile = SD.open(path.c_str());
  if (dataFile.isDirectory()) {
    path += "/index.htm";
    dataType = "text/html";
    dataFile = SD.open(path.c_str());
  }

  if (!dataFile) {
    return false;
  }

  if (server.hasArg("download")) {
    dataType = "application/octet-stream";
  }

  if (server.streamFile(dataFile, dataType) != dataFile.size()) {
    Serial.println("Sent less data than expected!");
  }

  dataFile.close();
  return true;
}

void handleFileUpload() {
  if (server.uri() != "/edit") {
    return;
  }
  HTTPUpload &upload = server.upload();
  static size_t totalUploadSize = 0;
  static size_t currentUploadSize = 0;

  if (upload.status == UPLOAD_FILE_START) {
    if (SD.exists((char *)upload.filename.c_str())) {
      SD.remove((char *)upload.filename.c_str());
    }
    uploadFile = SD.open(upload.filename.c_str(), FILE_WRITE);
    totalUploadSize = upload.totalSize;
    currentUploadSize = 0;

    Serial.print("Upload: START, filename: ");
    Serial.println(upload.filename);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Upload: START");
    u8g2.drawStr(0, 30, upload.filename.c_str());
    u8g2.sendBuffer();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadFile) {
      uploadFile.write(upload.buf, upload.currentSize);
      currentUploadSize += upload.currentSize;

      Serial.print("Upload: WRITE, Bytes: ");
      Serial.println(upload.currentSize);
      u8g2.clearBuffer();
      u8g2.drawStr(0, 10, "Uploading...");
      u8g2.drawStr(0, 30, ("Uploaded: " + String(currentUploadSize)).c_str());
      u8g2.drawStr(0, 50, ("Total: " + String(totalUploadSize)).c_str());
      u8g2.sendBuffer();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadFile) {
      uploadFile.close();
    }
    Serial.print("Upload: END, Size: ");
    Serial.println(upload.totalSize);
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Upload: END");
    u8g2.drawStr(0, 30, ("Total: " + String(totalUploadSize)).c_str());
    u8g2.sendBuffer();
    delay(3000);
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "System Activated");
    u8g2.drawStr(0, 30, ("SSID: " + ssid).c_str());
    u8g2.drawStr(0, 50, ("IP: " + myIPG).c_str());
    u8g2.drawStr(0, 70, ("PASS: " + password).c_str());
    u8g2.drawStr(0, 85, (ver).c_str());
    u8g2.sendBuffer();
  }
}

void deleteRecursive(String path) {
  File file = SD.open((char *)path.c_str());
  if (!file.isDirectory()) {
    file.close();
    SD.remove((char *)path.c_str());
    return;
  }

  file.rewindDirectory();
  while (true) {
    File entry = file.openNextFile();
    if (!entry) {
      break;
    }
    String entryPath = path + "/" + entry.name();
    if (entry.isDirectory()) {
      entry.close();
      deleteRecursive(entryPath);
    } else {
      entry.close();
      SD.remove((char *)entryPath.c_str());
    }
    yield();
  }

  SD.rmdir((char *)path.c_str());
  file.close();
}

void handleDelete() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || !SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }
  deleteRecursive(path);
  returnOK();
}

void handleCreate() {
  if (server.args() == 0) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg(0);
  if (path == "/" || SD.exists((char *)path.c_str())) {
    returnFail("BAD PATH");
    return;
  }

  if (path.indexOf('.') > 0) {
    File file = SD.open((char *)path.c_str(), FILE_WRITE);
    if (file) {
      file.write(0);
      file.close();
    }
  } else {
    SD.mkdir((char *)path.c_str());
  }
  returnOK();
}

void handleRename() {
  if (server.args() < 2) {
    return returnFail("BAD ARGS");
  }
  String oldName = server.arg(0);
  String newName = server.arg(1);
  if (!SD.exists((char *)oldName.c_str())) {
    returnFail("File does not exist");
    return;
  }
  if (SD.exists((char *)newName.c_str())) {
    returnFail("New name already exists");
    return;
  }
  if (SD.rename(oldName.c_str(), newName.c_str())) {
    returnOK();
  } else {
    returnFail("Rename failed");
  }
}

void printDirectory() {
  if (!server.hasArg("dir")) {
    return returnFail("BAD ARGS");
  }
  String path = server.arg("dir");
  if (path != "/" && !SD.exists((char *)path.c_str())) {
    return returnFail("BAD PATH");
  }
  File dir = SD.open((char *)path.c_str());
  path = String();
  if (!dir.isDirectory()) {
    dir.close();
    return returnFail("NOT DIR");
  }
  dir.rewindDirectory();
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/json", "");
  WiFiClient client = server.client();

  server.sendContent("[");
  for (int cnt = 0; true; ++cnt) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }

    String output;
    if (cnt > 0) {
      output = ',';
    }

    output += "{\"type\":\"";
    output += (entry.isDirectory()) ? "dir" : "file";
    output += "\",\"name\":\"";
    output += entry.path();
    output += "\",\"size\":\"";
    output += entry.size();
    output += "\"}";
    server.sendContent(output);
    entry.close();
  }
  server.sendContent("]");
  dir.close();
}

void handleNotFound() {
  if (hasSD && loadFromSdCard(server.uri())) {
    return;
  }
  String message = "SDCARD Not Detected\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " NAME:" + server.argName(i) + "\n VALUE:" + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
  Serial.print(message);
}

void performUpdate(Stream &updateSource, size_t updateSize) {
  if (Update.begin(updateSize)) {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize) {
      Serial.println("Written : " + String(written) + " successfully");
    } else {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    if (Update.end()) {
      Serial.println("OTA done!");
      if (Update.isFinished()) {
        Serial.println("Update successfully completed. Rebooting.");
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Update completed!");
        u8g2.sendBuffer();
        delay(2000);

        if (SD.rename("/update.bin", "/update.bak")) {
          Serial.println("Firmware renamed successfully!");
          u8g2.clearBuffer();
          u8g2.drawStr(0, 10, "Firmware renamed");
          u8g2.sendBuffer();
        } else {
          Serial.println("Firmware rename error!");
          u8g2.clearBuffer();
          u8g2.drawStr(0, 10, "Rename error!");
          u8g2.sendBuffer();
        }

        ESP.restart();
      } else {
        Serial.println("Update not finished? Something went wrong!");
        u8g2.clearBuffer();
        u8g2.drawStr(0, 10, "Update not finished!");
        u8g2.sendBuffer();
      }
    } else {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
      u8g2.clearBuffer();
      u8g2.drawStr(0, 10, ("Update Error #: " + String(Update.getError())).c_str());
      u8g2.sendBuffer();
    }
  } else {
    Serial.println("Not enough space to begin OTA");
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Not enough space");
    u8g2.sendBuffer();
  }
}

void updateFromFS(fs::FS &fs) {
  File updateBin = fs.open("/update.bin");
  if (updateBin) {
    if (updateBin.isDirectory()) {
      Serial.println("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0) {
      Serial.println("Try to start update");
      performUpdate(updateBin, updateSize);
    } else {
      Serial.println("Error, file is empty");
      u8g2.clearBuffer();
      u8g2.drawStr(0, 10, "File is empty");
      u8g2.sendBuffer();
    }

    updateBin.close();

    if (SD.rename("/update.bin", "/update.bak")) {
      Serial.println("Firmware renamed successfully!");
      u8g2.clearBuffer();
      u8g2.drawStr(0, 10, "Firmware renamed");
      u8g2.sendBuffer();
    } else {
      Serial.println("Firmware rename error!");
      u8g2.clearBuffer();
      u8g2.drawStr(0, 10, "Rename error!");
      u8g2.sendBuffer();
    }
  } else {
    Serial.println("Could not load update.bin from sd root");
    u8g2.clearBuffer();
    u8g2.drawStr(0, 10, "Firmware not found!");
    u8g2.sendBuffer();
  }
}

void activateSystem() {
  Serial.println("Activating system...");

  // Configura el AP con el SSID basado en la dirección MAC
  String macAddress = WiFi.macAddress();
  macAddress.replace(":", "");  // Elimina los dos puntos de la dirección MAC
  ssid = "ESP32-" + macAddress;
  password = macAddress;  // Establece la contraseña como la dirección MAC

  WiFi.softAP(ssid.c_str(), password.c_str());
  IPAddress myIP = WiFi.softAPIP();
  myIPG = myIP.toString();
  Serial.print("AP IP address: ");
  Serial.println(myIP);
  Serial.print("SSID: ");
  Serial.println(ssid);
  Serial.println("PASS: " + password);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "System Activated");
  u8g2.drawStr(0, 30, ("SSID: " + ssid).c_str());
  u8g2.drawStr(0, 50, ("IP: " + myIPG).c_str());
  u8g2.drawStr(0, 70, ("PASS: " + password).c_str());
  u8g2.drawStr(0, 85, (ver).c_str());
  u8g2.sendBuffer();

  if (MDNS.begin("esp32sd")) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("MDNS responder started");
    Serial.println("You can now connect to http://esp32sd.local");
  }

  server.on("/list", HTTP_GET, printDirectory);
  server.on("/edit", HTTP_DELETE, handleDelete);
  server.on("/edit", HTTP_PUT, handleCreate);
  server.on(
    "/edit", HTTP_POST, []() {
      returnOK();
    },
    handleFileUpload);
  server.on("/updateFirmware", HTTP_GET, []() {
    updateFromFS(SD);
    server.send(200, "text/plain", "Update initiated");
  });
  server.on("/rename", HTTP_POST, handleRename);
  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");

  spiSD.begin(SD_SCLK, SD_MISO, SD_MOSI, SD_CS_PIN);

  if (!SD.begin(SD_CS_PIN, spiSD)) {
    Serial.println("Card Mount Failed");
  } else {
    Serial.println("SD Card initialized.");
    hasSD = true;
  }
  if (SD.exists("/update.bin") == true) {
    Serial.println("archivo para actualizar... actualizando");
    updateFromFS(SD);
  } else {
    Serial.println("sin update");
  }
  systemActive = true;
}

void deactivateSystem() {
  Serial.println("Deactivating system...");
  server.close();
  SD.end();
  MDNS.end();
  WiFi.softAPdisconnect(true);

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "System Deactivated");
  u8g2.sendBuffer();

  systemActive = false;
}

void IRAM_ATTR handleButtonPress() {
  buttonPressed = true;
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("\n");

  // Inicializa la pantalla
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "System Ready V2");
  activateSystem();
  u8g2.sendBuffer();
  pinMode(BUTTON_PIN, INPUT_PULLUP);                                               // Configura el pin del botón como entrada con pull-up
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);  // Configura la interrupción
}

void loop(void) {
  if (buttonPressed) {
    buttonPressed = false;
    if (systemActive) {
      deactivateSystemFlag = true;
    } else {
      activateSystemFlag = true;
    }
  }

  if (activateSystemFlag) {
    activateSystem();
    activateSystemFlag = false;
  }

  if (deactivateSystemFlag) {
    deactivateSystem();
    deactivateSystemFlag = false;
  }

  // Verifica si hay algún cliente conectado
  clientConnected = (WiFi.softAPgetStationNum() > 0);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "System Activated");
  u8g2.drawStr(0, 30, ("SSID: " + ssid).c_str());
  u8g2.drawStr(0, 50, ("IP: " + myIPG).c_str());
  u8g2.drawStr(0, 70, ("PASS: " + password).c_str());
  u8g2.drawStr(0, 85, (ver).c_str());
  if (clientConnected) {
    u8g2.drawStr(0, 100, "Client Connected");
  } else {
    u8g2.drawStr(0, 100, "No Client Connected");
  }
  u8g2.sendBuffer();
}
