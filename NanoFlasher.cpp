#include "NanoFlasher.h"
#include "Config.h"
#include "ArduinoCommManager.h"
#include <WebServer.h>
#include <HardwareSerial.h>

WebSerialPrint WebSerial;

size_t WebSerialPrint::write(uint8_t c) {
  Serial.write(c); // תמיד מדפיס לטרמינל הפיזי
  
  if (webOutputEnabled) {
    buffer += (char)c;
    // מנגנון הגנה יעיל: אם עברנו 4KB, מוחקים רק את ההתחלה (2KB הישנים)
    if(buffer.length() > 4096) {
      buffer.remove(0, 2048); 
    }
  }
  return 1;
}

size_t WebSerialPrint::write(const uint8_t *buf, size_t size) {
  Serial.write(buf, size); // תמיד מדפיס לטרמינל הפיזי
  
  if (webOutputEnabled) {
    buffer.reserve(buffer.length() + size);
    for(size_t i = 0; i < size; i++) {
      buffer += (char)buf[i];
    }
    // מחיקת ישנים מבלי לרוקן הכל
    if (buffer.length() > 4096) {
      buffer.remove(0, buffer.length() - 2048);
    }
  }
  return size;
}

void WebSerialPrint::begin(unsigned long baud) {
  Serial.begin(baud);
}

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
    WebSerial.println("[NanoFlasher] Error: Could not sync with Bootloader!");
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
  "<!DOCTYPE html><html><head><meta charset='utf-8'><title>Solar Manager & Diagnostic v5</title>"
  "<meta name='viewport' content='width=device-width, initial-scale=1'>"
  "<style>"
  "body{font-family:Arial,sans-serif;display:flex;justify-content:center;align-items:flex-start;gap:20px;min-height:100vh;margin:0;padding:25px;background:#e9ecef;direction:rtl;flex-wrap:wrap;box-sizing:border-box;}"
  ".box{background:white;padding:25px;border-radius:12px;box-shadow:0 4px 15px rgba(0,0,0,0.15);width:350px;box-sizing:border-box;text-align:center;}"
  ".box-log{width:540px;max-width:100%;text-align:right;}"
  "input{margin:15px 0;width:100%;} "
  "button{color:white;border:none;padding:12px;border-radius:6px;cursor:pointer;font-size:15px;width:100%;font-weight:bold;margin-bottom:10px;transition:0.2s;}"
  "button:hover{opacity:0.9;}"
  "button:disabled{background:#6c757d !important;cursor:not-allowed;opacity:0.65;}"
  ".btn-uart{background:#28a745;}"
  ".btn-reset{background:#ffc107;color:#212529;} .btn-reset:hover{background:#e0a800;}"
  ".btn-esp{background:#dc3545;color:white;} .btn-esp:hover{background:#c82333;}"
  ".btn-start{background:#28a745;color:white;}"
  ".btn-stop{background:#dc3545;color:white;}"
  ".btn-save{background:#17a2b8;color:white;}"
  ".console{background:#111;color:#00ff66;font-family:Consolas,monospace;font-size:12px;padding:12px;border-radius:6px;height:340px;overflow-y:auto;white-space:pre-wrap;text-align:left;direction:ltr;margin:12px 0;border:1px solid #333;box-shadow:inset 0 0 8px rgba(0,0,0,0.8);}"
  ".status-indicator{font-size:14px;font-weight:bold;margin-bottom:10px;color:#444;}"
  "</style></head><body>"

  "<!-- מסגרת 1: צריבה ואיפוסים -->"
  "<div class='box'>"
  "<h2>צריבת Arduino Nano</h2>"
  "<p>בחר קובץ firmware.hex</p>"
  "<form method='POST' action='/upload_hex' enctype='multipart/form-data'>"
  "<input type='file' name='hex' accept='.hex' required><br>"
  "<button type='submit' class='btn-uart'>צרוב לנאנו דרך UART</button>"
  "</form><hr style='margin:20px 0;'>"
  "<button class='btn-reset' onclick=\"fetch('/reset_nano').then(r=>r.text()).then(alert)\">בצע Reset ל-Nano</button>"
  "<button class='btn-esp' onclick=\"if(confirm('לאתחל את ה-ESP32?')) fetch('/reset_esp').then(r=>r.text()).then(alert)\">אתחל ESP32</button>"
  "</div>"

  "<!-- מסגרת 2: מוניטור לוג TakeLog -->"
  "<div class='box box-log'>"
  "<h2 style='text-align:center;'>Log Monitor</h2>"
  "<div id='logStatus' class='status-indicator'>סטטוס: לוג מושבת</div>"
  "<div style='display:flex;gap:10px;'>"
  "<button class='btn-start' id='btnStart' onclick='doStart()'>התחל לוג</button>"
  "<button class='btn-stop' id='btnStop' onclick='doStop()' disabled>הפסק לוג</button>"
  "</div>"
  "<div class='console' id='consoleBox'>[SYSTEM READY] לחץ 'התחל לוג' להפעלת רישום...</div>"
  "<div style='display:flex;gap:10px;'>"
  "<button class='btn-save' onclick='doSave()'>שמור לקובץ</button>"
  "<button style='background:#343a40;width:35%;' onclick='doClear()'>נקה מסך</button>"
  "</div>"
  "</div>"

  "<!-- מסגרת 3: טרמינל סיריאלי ESP32 -->"
  "<div class='box box-log'>"
  "<h2 style='text-align:center;'>ESP32 Serial Terminal</h2>"
  "<div id='termStatus' class='status-indicator'>סטטוס: שידור לפורטל מבוטל</div>"
  "<div style='display:flex;gap:10px;'>"
  "<button class='btn-start' id='btnTermEnable' onclick='doTermToggle(true)'>אפשר שידור לפורטל</button>"
  "<button class='btn-stop' id='btnTermDisable' onclick='doTermToggle(false)' disabled>הפסק שידור</button>"
  "</div>"
  "<div class='console' id='terminalBox'>[TERMINAL READY] ממתין לנתונים...</div>"
  
  "<!-- שימוש ב-Grid מבטיח שהכפתורים לעולם לא ירדו שורה -->"
  "<div style='display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 10px; margin-top: 10px;'>"
  "<button class='btn-save' style='margin-bottom:0; width:100%;' onclick='doSaveTerminal()'>שמור לקובץ</button>"
  "<button style='background:#6c757d; color:white; margin-bottom:0; width:100%;' onclick='doCopyTerminal()'>העתק טרמינל</button>"
  "<button style='background:#343a40; margin-bottom:0; width:100%;' onclick='doClearTerminal()'>נקה מסך</button>"
  "</div>"
  
  "</div>"

  "<script>"
  "var logData = '';"
  "var termData = '';"
  "var timer = null;"
  "var termEnabled = false;"

  "function doStart() {"
  "  document.getElementById('logStatus').innerHTML = 'סטטוס: <span style=\"color:#007bff;\">מתחבר...</span>';"
  "  fetch('/log/start')"
  "    .then(function(r){ return r.text(); })"
  "    .then(function(res){"
  "      document.getElementById('logStatus').innerHTML = 'סטטוס: <span style=\"color:#28a745;\">לוג מופעל (פעיל)</span>';"
  "      document.getElementById('btnStart').disabled = true;"
  "      document.getElementById('btnStop').disabled = false;"
  "      if(!timer) timer = setInterval(poll, 1000);"
  "    })"
  "    .catch(function(err){ alert('שגיאה: ' + err); });"
  "}"

  "function doStop() {"
  "  fetch('/log/stop')"
  "    .then(function(){"
  "      document.getElementById('logStatus').innerHTML = 'סטטוס: <span style=\"color:#dc3545;\">לוג מושבת</span>';"
  "      document.getElementById('btnStart').disabled = false;"
  "      document.getElementById('btnStop').disabled = true;"
  "      if(timer) { clearInterval(timer); timer = null; }"
  "    });"
  "}"

  "function poll() {"
  "  fetch('/log/read?t=' + new Date().getTime())"
  "    .then(function(r){ return r.text(); })"
  "    .then(function(txt){"
  "      if(txt && txt.length > 0) {"
  "        logData += txt;"
  "        var c = document.getElementById('consoleBox');"
  "        if(c.textContent.indexOf('[SYSTEM READY') !== -1) c.textContent = '';"
  "        c.textContent += txt;"
  "        c.scrollTop = c.scrollHeight;"
  "      }"
  "    });"
  "}"

  "function doClear() {"
  "  logData = '';"
  "  document.getElementById('consoleBox').textContent = '';"
  "}"

  "function doSave() {"
  "  if(!logData.trim()) { alert('אין נתוני לוג לשמירה!'); return; }"
  "  var now = new Date();"
  "  var pad = function(n){ return (n<10?'0':'') + n; };"
  "  var name = 'Solar_Log_' + now.getFullYear() + '-' + pad(now.getMonth()+1) + '-' + pad(now.getDate()) + '_' + pad(now.getHours()) + '-' + pad(now.getMinutes()) + '-' + pad(now.getSeconds()) + '.txt';"
  "  var blob = new Blob([logData], { type: 'text/plain;charset=utf-8' });"
  "  var a = document.createElement('a');"
  "  a.href = URL.createObjectURL(blob);"
  "  a.download = name;"
  "  document.body.appendChild(a);"
  "  a.click();"
  "  document.body.removeChild(a);"
  "}"

  "function doTermToggle(state) {"
  "  fetch('/terminal/toggle?state=' + (state ? '1' : '0') + '&t=' + new Date().getTime())"
  "    .then(function(r){ return r.text(); })"
  "    .then(function(txt) {"
  "       termEnabled = state;"
  "       if(state) {"
  "         document.getElementById('termStatus').innerHTML = 'סטטוס: <span style=\"color:#28a745;\">שידור מופעל</span>';"
  "         document.getElementById('btnTermEnable').disabled = true;"
  "         document.getElementById('btnTermDisable').disabled = false;"
  "       } else {"
  "         document.getElementById('termStatus').innerHTML = 'סטטוס: <span style=\"color:#dc3545;\">שידור מבוטל</span>';"
  "         document.getElementById('btnTermEnable').disabled = false;"
  "         document.getElementById('btnTermDisable').disabled = true;"
  "       }"
  "    }).catch(function(e){ alert('שגיאה בתקשורת'); });"
  "}"
  
  "function pollTerminal() {"
  "  if(!termEnabled) return;"
  "  fetch('/terminal/read?t=' + new Date().getTime())"
  "    .then(function(r){ return r.text(); })"
  "    .then(function(txt){"
  "      if(txt && txt.length > 0) {"
  "        termData += txt;"
  "        var c = document.getElementById('terminalBox');"
  "        if(c.textContent.indexOf('[TERMINAL READY') !== -1) c.textContent = '';"
  "        c.textContent += txt;"
  "        c.scrollTop = c.scrollHeight;"
  "      }"
  "    }).catch(function(){});"
  "}"
  "setInterval(pollTerminal, 1200);"

  "function doSaveTerminal() {"
  "  if(!termData.trim()) { alert('אין נתונים לשמירה!'); return; }"
  "  var blob = new Blob([termData], { type: 'text/plain;charset=utf-8' });"
  "  var a = document.createElement('a');"
  "  a.href = URL.createObjectURL(blob);"
  "  a.download = 'ESP_Terminal_Dump.txt';"
  "  document.body.appendChild(a);"
  "  a.click();"
  "  document.body.removeChild(a);"
  "}"

  "function doCopyTerminal() {"
  "  if(!termData.trim()) { alert('אין נתונים להעתקה!'); return; }"
  "  if (navigator.clipboard && window.isSecureContext) {"
  "    navigator.clipboard.writeText(termData).then(function() { alert('התוכן הועתק בהצלחה ללוח!'); });"
  "  } else {"
  "    /* Fallback עבור חיבורי HTTP לא מאובטחים */"
  "    var ta = document.createElement('textarea');"
  "    ta.value = termData;"
  "    ta.style.position = 'fixed';"
  "    document.body.appendChild(ta);"
  "    ta.focus();"
  "    ta.select();"
  "    try {"
  "      document.execCommand('copy');"
  "      alert('התוכן הועתק בהצלחה ללוח!');"
  "    } catch(err) {"
  "      alert('שגיאה בהעתקה, הדפדפן חוסם זאת.');"
  "    }"
  "    document.body.removeChild(ta);"
  "  }"
  "}"

  "function doClearTerminal() {"
  "  termData = '';"
  "  document.getElementById('terminalBox').textContent = '';"
  "}"
  "</script></body></html>";

static String hexLineBuffer = "";
static bool flashSuccess = true;

void setupNanoFlasher() {
  pinMode(NANO_RESET_PIN, OUTPUT);
  digitalWrite(NANO_RESET_PIN, HIGH);

  nanoServer.on("/", HTTP_GET, []() {
    nanoServer.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    nanoServer.sendHeader("Pragma", "no-cache");
    nanoServer.sendHeader("Expires", "0");
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

  nanoServer.on("/log/start", HTTP_GET, []() {
    setNanoLogEnabled(true);
    nanoServer.sendHeader("Cache-Control", "no-cache");
    nanoServer.send(200, "text/plain", "STARTED");
  });

  nanoServer.on("/log/stop", HTTP_GET, []() {
    setNanoLogEnabled(false);
    nanoServer.sendHeader("Cache-Control", "no-cache");
    nanoServer.send(200, "text/plain", "STOPPED");
  });

  nanoServer.on("/log/read", HTTP_GET, []() {
    String logText = getLatestLogText();
    nanoServer.sendHeader("Cache-Control", "no-cache");
    nanoServer.send(200, "text/plain", logText);
  });

  // נתיב להדלקה וכיבוי השליחה לפורטל
  nanoServer.on("/terminal/toggle", HTTP_GET, []() {
    if (nanoServer.hasArg("state")) {
      WebSerial.webOutputEnabled = (nanoServer.arg("state") == "1");
      WebSerial.buffer = ""; // תמיד מנקים את החוצץ בעת שינוי מצב למניעת שאריות מהעבר
    }
    nanoServer.sendHeader("Cache-Control", "no-cache");
    nanoServer.send(200, "text/plain", WebSerial.webOutputEnabled ? "ENABLED" : "DISABLED");
  });

  // נתיב למשיכת נתוני הטרמינל
  nanoServer.on("/terminal/read", HTTP_GET, []() {
    nanoServer.sendHeader("Cache-Control", "no-cache");
    nanoServer.send(200, "text/plain", WebSerial.buffer);
    WebSerial.buffer = ""; // ניקוי החוצץ לאחר כל משיכה
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
      WebSerial.println("\n[NanoFlasher] Starting Nano Flashing Session...");
      pauseArduinoComm();
      flashSuccess = true;
      hexLineBuffer = "";
      pageHasData = false;

      HardwareSerial &serial = getNanoSerial();
      serial.begin(57600, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);

      triggerNanoReset();

      if (!stkSendSync()) {
        WebSerial.println("[NanoFlasher] Error: Could not sync with Bootloader!");
        flashSuccess = false;
      } else {
        WebSerial.println("[NanoFlasher] Bootloader Synced!");
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
      WebSerial.println("[NanoFlasher] Firmware flashed completely.");
      triggerNanoReset();
    }
  });

  nanoServer.begin();
  WebSerial.println("[NanoFlasher] Server running on port 8080");
}

void handleNanoFlasher() {
  nanoServer.handleClient();
}