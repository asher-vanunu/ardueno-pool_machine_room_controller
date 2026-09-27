#include "NanoFlasher.h"
#include "Config.h"
#include "ArduinoCommManager.h"
#include <WebServer.h>
#include <HardwareSerial.h>

static WebServer nanoServer(8080);

#define STK_OK              0x10
#define STK_INSYNC          0x14
#define STK_GET_SYNC        0x30
#define STK_LOAD_ADDRESS    0x55
#define STK_PROG_PAGE       0x64
#define CRC_EOP             0x20

#define NANO_PAGE_SIZE      128

static uint8_t pageBuffer[NANO_PAGE_SIZE];
static uint16_t currentPageStart = 0;
static bool pageHasData = false;

void triggerNanoReset() {
  pinMode(NANO_RESET_PIN, OUTPUT);
  digitalWrite(NANO_RESET_PIN, HIGH);
  delay(50);
  digitalWrite(NANO_RESET_PIN, LOW);
  delay(50);
  digitalWrite(NANO_RESET_PIN, HIGH);
  delay(100);
}

static bool stkSendSync() {
  HardwareSerial &serial = getNanoSerial();
  for (int i = 0; i < 25; i++) {
    while (serial.available()) serial.read();
    serial.write(STK_GET_SYNC);
    serial.write(CRC_EOP);
    serial.flush();

    delay(20);
    if (serial.available() >= 2) {
      uint8_t resp1 = serial.read();
      uint8_t resp2 = serial.read();
      if (resp1 == STK_INSYNC && resp2 == STK_OK) {
        return true;
      }
    }
  }
  return false;
}

static bool stkLoadAddress(uint16_t addrWords) {
  HardwareSerial &serial = getNanoSerial();
  while (serial.available()) serial.read();
  serial.write(STK_LOAD_ADDRESS);
  serial.write(addrWords & 0xFF);
  serial.write((addrWords >> 8) & 0xFF);
  serial.write(CRC_EOP);
  serial.flush();

  delay(5);
  return (serial.read() == STK_INSYNC && serial.read() == STK_OK);
}

static bool stkWritePage(uint16_t addressBytes, uint8_t *data, uint16_t len) {
  HardwareSerial &serial = getNanoSerial();
  if (!stkLoadAddress(addressBytes / 2)) {
    return false;
  }

  while (serial.available()) serial.read();
  serial.write(STK_PROG_PAGE);
  serial.write((len >> 8) & 0xFF);
  serial.write(len & 0xFF);
  serial.write('F');

  for (uint16_t i = 0; i < len; i++) {
    serial.write(data[i]);
  }
  serial.write(CRC_EOP);
  serial.flush();

  unsigned long startWait = millis();
  while (serial.available() < 2 && (millis() - startWait < 500)) delay(1);

  if (serial.available() >= 2) {
    return (serial.read() == STK_INSYNC && serial.read() == STK_OK);
  }
  return false;
}

static uint8_t hex2byte(const char *src) {
  char buf[3] = {src[0], src[1], 0};
  return (uint8_t)strtol(buf, NULL, 16);
}

static bool parseAndFlashRecord(const String &line) {
  if (line.charAt(0) != ':') return true;

  const char *raw = line.c_str();
  uint8_t len = hex2byte(raw + 1);
  uint16_t addr = (hex2byte(raw + 3) << 8) | hex2byte(raw + 5);
  uint8_t type = hex2byte(raw + 7);

  if (type == 0x01) {
    if (pageHasData) {
      stkWritePage(currentPageStart, pageBuffer, NANO_PAGE_SIZE);
      pageHasData = false;
    }
    return true;
  }

  if (type == 0x00) {
    for (int i = 0; i < len; i++) {
      uint8_t byteVal = hex2byte(raw + 9 + (i * 2));
      uint16_t byteAddr = addr + i;
      uint16_t targetPage = (byteAddr / NANO_PAGE_SIZE) * NANO_PAGE_SIZE;

      if (targetPage != currentPageStart && pageHasData) {
        if (!stkWritePage(currentPageStart, pageBuffer, NANO_PAGE_SIZE)) return false;
        memset(pageBuffer, 0xFF, NANO_PAGE_SIZE);
        currentPageStart = targetPage;
      } else if (!pageHasData) {
        memset(pageBuffer, 0xFF, NANO_PAGE_SIZE);
        currentPageStart = targetPage;
        pageHasData = true;
      }

      pageBuffer[byteAddr % NANO_PAGE_SIZE] = byteVal;
    }
  }
  return true;
}

bool flashNanoHex(const uint8_t* hexData, size_t hexLen) {
  if (hexData == nullptr || hexLen == 0) return false;

  pauseArduinoComm();
  pageHasData = false;
  memset(pageBuffer, 0xFF, NANO_PAGE_SIZE);

  HardwareSerial &serial = getNanoSerial();
  serial.begin(57600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

  triggerNanoReset();

  if (!stkSendSync()) {
    Serial.println("[NanoFlasher] Error: Could not sync with Bootloader!");
    resumeArduinoComm();
    return false;
  }

  String currentLine = "";
  for (size_t i = 0; i < hexLen; i++) {
    char c = (char)hexData[i];
    if (c == '\r' || c == '\n') {
      if (currentLine.length() > 0) {
        if (!parseAndFlashRecord(currentLine)) {
          resumeArduinoComm();
          return false;
        }
        currentLine = "";
      }
    } else {
      currentLine += c;
    }
  }

  if (currentLine.length() > 0) {
    parseAndFlashRecord(currentLine);
  }

  if (pageHasData) {
    stkWritePage(currentPageStart, pageBuffer, NANO_PAGE_SIZE);
    pageHasData = false;
  }

  triggerNanoReset();
  resumeArduinoComm();
  return true;
}

static const char* nanoUploadPage = 
  "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Arduino Nano OTA Flasher</title>"
  "<style>body{font-family:Arial;display:flex;justify-content:center;align-items:center;min-height:100vh;margin:0;background:#e9ecef;direction:rtl;}"
  ".box{background:white;padding:30px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.15);text-align:center;width:340px;}"
  "input{margin:20px 0;width:100%;} button{background:#28a745;color:white;border:none;padding:12px;border-radius:6px;cursor:pointer;font-size:16px;width:100%;font-weight:bold;margin-bottom:10px;}"
  "button:hover{background:#218838;} .btn-reset{background:#ffc107;color:#212529;} .btn-reset:hover{background:#e0a800;}"
  ".btn-esp{background:#dc3545;color:white;} .btn-esp:hover{background:#c82333;}</style></head><body>"
  "<div class='box'><h2>צריבת Arduino Nano</h2>"
  "<p>בחר קובץ firmware.hex</p>"
  "<form method='POST' action='/upload_hex' enctype='multipart/form-data'>"
  "<input type='file' name='hex' accept='.hex' required><br>"
  "<button type='submit'>צרוב לנאנו דרך UART</button>"
  "</form><hr style='margin:20px 0;'>"
  "<button class='btn-reset' onclick=\"fetch('/reset_nano').then(r=>r.text()).then(alert)\">בצע Reset ל-Nano</button>"
  "<button class='btn-esp' onclick=\"if(confirm('לאתחל את ה-ESP32?')) fetch('/reset_esp').then(r=>r.text()).then(alert)\">אתחל ESP32</button>"
  "</div></body></html>";

static String hexLineBuffer = "";
static bool flashSuccess = true;

void setupNanoFlasher() {
  pinMode(NANO_RESET_PIN, OUTPUT);
  digitalWrite(NANO_RESET_PIN, HIGH);

  nanoServer.on("/", HTTP_GET, []() {
    nanoServer.send(200, "text/html", nanoUploadPage);
  });

  nanoServer.on("/reset_nano", HTTP_GET, []() {
    triggerNanoReset();
    nanoServer.send(200, "text/plain", "Nano Reset Triggered!");
  });

  nanoServer.on("/reset_esp", HTTP_GET, []() {
    nanoServer.send(200, "text/plain", "ESP32 Rebooting...");
    delay(500);
    ESP.restart();
  });

  nanoServer.on("/upload_hex", HTTP_POST, []() {
    resumeArduinoComm();
    if (flashSuccess) {
      nanoServer.send(200, "text/plain", "SUCCESS! Nano flashed successfully. Resuming communication...");
    } else {
      nanoServer.send(500, "text/plain", "FLASH FAILED! Check wiring, sync or bootloader.");
    }
  }, []() {
    HTTPUpload& upload = nanoServer.upload();

    if (upload.status == UPLOAD_FILE_START) {
      Serial.println("\n[NanoFlasher] Starting Nano Flashing Session...");
      pauseArduinoComm();
      flashSuccess = true;
      hexLineBuffer = "";
      pageHasData = false;

      HardwareSerial &serial = getNanoSerial();
      serial.begin(57600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

      triggerNanoReset();

      if (!stkSendSync()) {
        Serial.println("[NanoFlasher] Error: Could not sync with Bootloader!");
        flashSuccess = false;
      } else {
        Serial.println("[NanoFlasher] Bootloader Synced!");
      }
    } 
    else if (upload.status == UPLOAD_FILE_WRITE && flashSuccess) {
      for (size_t i = 0; i < upload.currentSize; i++) {
        char c = (char)upload.buf[i];
        if (c == '\r' || c == '\n') {
          if (hexLineBuffer.length() > 0) {
            if (!parseAndFlashRecord(hexLineBuffer)) {
              flashSuccess = false;
              break;
            }
            hexLineBuffer = "";
          }
        } else {
          hexLineBuffer += c;
        }
      }
    } 
    else if (upload.status == UPLOAD_FILE_END && flashSuccess) {
      if (hexLineBuffer.length() > 0) {
        parseAndFlashRecord(hexLineBuffer);
      }
      if (pageHasData) {
        stkWritePage(currentPageStart, pageBuffer, NANO_PAGE_SIZE);
      }
      Serial.println("[NanoFlasher] Firmware flashed completely.");
      triggerNanoReset();
    }
  });

  nanoServer.begin();
  Serial.println("[NanoFlasher] Server running on port 8080");
}

void handleNanoFlasher() {
  nanoServer.handleClient();
}