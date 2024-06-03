# SDWebServer con Actualización OTA para ESP32

Este proyecto implementa un servidor web con soporte para tarjetas SD en un ESP32. El servidor web permite listar, subir, eliminar y renombrar archivos almacenados en la tarjeta SD. Además, incluye una función de actualización OTA (Over-the-Air) que permite actualizar el firmware del ESP32 a través de un archivo `update.bin` almacenado en la tarjeta SD.

## Requisitos

- ESP32
- Tarjeta SD formateada en FAT
- Librerías Arduino:
  - `WiFi.h`
  - `WebServer.h`
  - `U8g2lib.h`
  - `ESPmDNS.h`
  - `SPI.h`
  - `SD.h`
  - `Update.h`

## Instalación

1. **Configurar el ESP32:**
   - Conectar la tarjeta SD al ESP32.
   - Descargar e instalar las librerías necesarias desde el Administrador de Librerías de Arduino.

2. **Subir los archivos necesarios a la tarjeta SD:**
   - `index.htm`: Archivo HTML principal.
   - Carpeta `lib`: Incluir cualquier archivo JavaScript, CSS, o fuentes necesarios para el funcionamiento de la interfaz web.

## Uso

### Inicialización del Sistema

El sistema se inicializa automáticamente al encender el ESP32. Se creará un punto de acceso con un SSID basado en la dirección MAC del ESP32 y la contraseña será la misma dirección MAC sin los dos puntos.

### Funcionalidades del Servidor Web

- **Listar Archivos:** 
  Para listar los archivos en la tarjeta SD, navegue a `http://esp32sd.local/list?dir=/`.

- **Subir Archivos:**
  Utilice el formulario en la interfaz web para subir archivos a la tarjeta SD.

- **Eliminar Archivos:**
  Haga clic en el botón "Delete" en la interfaz web para eliminar archivos.

- **Renombrar Archivos:**
  Utilice la opción "Rename" en la interfaz web para renombrar archivos.

### Actualización OTA

Para actualizar el firmware del ESP32:

1. Asegúrese de que el archivo de firmware se llame `update.bin` y súbalo a la tarjeta SD.
2. El sistema buscará automáticamente el archivo `update.bin` al iniciar y procederá con la actualización si se encuentra el archivo.

## Ejemplos de Uso

### Activar el Sistema

```cpp
void setup() {
  Serial.begin(115200);
  Serial.println("\n");
  
  // Inicializa la pantalla
  u8g2.begin();
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 10, "System Ready V2");
  activateSystem();
  u8g2.sendBuffer();
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);  // Configura el pin del botón como entrada con pull-up
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress, FALLING);  // Configura la interrupción
}

void loop() {
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

  if (systemActive) {
    server.handleClient();
  }

  // Verifica si hay algún cliente conectado
  clientConnected = (WiFi.softAPgetStationNum() > 0);

  // Actualiza la pantalla con el estado de la conexión del cliente
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

  delay(2000);  // Añade un pequeño retardo para no saturar el loop
}
