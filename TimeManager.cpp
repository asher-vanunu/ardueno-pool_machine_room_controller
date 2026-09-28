#include "TimeManager.h"
#include "Config.h"
#include <Wire.h>
#include <RTClib.h>
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include "esp_sntp.h"

static RTC_DS3231 rtc;
static bool rtcFound = false;

// משתנים למעקב אחר הסנכרון
static bool initialNtpSynced = false;
static unsigned long lastNtpSyncTime = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000; // סנכרון פעם בשעה

// הגדרת מחרוזת אזור זמן רשמית עבור ישראל
const char* TIMEZONE_ISRAEL = "IST-2IDT,M3.4.4/26,M10.5.0";
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";

// פונקציית מעקב מובנית שמופעלת ברגע שהתקבל זמן מהרשת
void timeAvailableCallback(struct timeval *t) {
  Serial.println("\n---------------------------------------------");
  Serial.println("[NTP Event] Network Time Protocol sync received successfully!");

  struct tm timeinfo;
  
  // הוספת הגבלת זמן ל-10 אלפיות שנייה כדי לא לתקוע את התוכנית בזמן ניתוק!
  if (getLocalTime(&timeinfo, 10)) {
    Serial.printf("[NTP Event] Local Israel Time: %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    // עדכון מודול הסוללה בשעה המקומית המדויקת של ישראל
    if (rtcFound && timeinfo.tm_year > (2020 - 1900)) {
      rtc.adjust(DateTime(timeinfo.tm_year + 1900,
                          timeinfo.tm_mon + 1,
                          timeinfo.tm_mday,
                          timeinfo.tm_hour,
                          timeinfo.tm_min,
                          timeinfo.tm_sec));
      Serial.println("[RTC] DS3231 aligned with local Israel time.");
    }
  }
  Serial.println("---------------------------------------------\n");
}

void setupRTC() {
  Wire.begin(RTC_SDA_PIN, RTC_SCL_PIN);

  if (!rtc.begin()) {
    Serial.println("[RTC] ERROR: Could not find DS3231 module! Check wiring on SDA/SCL.");
    rtcFound = false;
  } else {
    rtcFound = true;
    Serial.println("[RTC] DS3231 initialized successfully.");

    if (rtc.lostPower()) {
      Serial.println("[RTC] DS3231 lost power, initializing time from build date.");
      rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    }

    // סנכרון ראשוני לשעון המערכת מתוך מודול הסוללה
    syncSystemTimeWithRTC();
  }

  // הגדרת אזור זמן ישראל רשמי (ללא תוספת היסט ידנית כפולה)
  configTzTime(TIMEZONE_ISRAEL, ntpServer1, ntpServer2);
  sntp_set_time_sync_notification_cb(timeAvailableCallback);
  Serial.println("[Time] NTP client configured with Israel Timezone.");
}

void syncSystemTimeWithRTC() {
  if (!rtcFound) return;

  DateTime now = rtc.now();

  struct tm tm_time;
  tm_time.tm_year = now.year() - 1900;
  tm_time.tm_mon  = now.month() - 1;
  tm_time.tm_mday = now.day();
  tm_time.tm_hour = now.hour();
  tm_time.tm_min  = now.minute();
  tm_time.tm_sec  = now.second();

  time_t t = mktime(&tm_time);
  struct timeval tv = { .tv_sec = t, .tv_usec = 0 };
  settimeofday(&tv, NULL);

  Serial.printf("[RTC] Synced system time from DS3231: %02d/%02d/%04d %02d:%02d:%02d\n",
                now.day(), now.month(), now.year(),
                now.hour(), now.minute(), now.second());
}

void updateRTCFromNTP() {
  time_t nowSecs;
  struct tm timeinfo;
  time(&nowSecs);

  // הוספת הגבלת זמן ל-10 אלפיות שנייה (מקור הבעיה היה עיכוב של 5 שניות לפקודה זו ללא רשת)
  if (!getLocalTime(&timeinfo, 10)) {
    return;
  }

  if (timeinfo.tm_year > (2020 - 1900) && rtcFound) {
    rtc.adjust(DateTime(timeinfo.tm_year + 1900,
                        timeinfo.tm_mon + 1,
                        timeinfo.tm_mday,
                        timeinfo.tm_hour,
                        timeinfo.tm_min,
                        timeinfo.tm_sec));
    
    if (!initialNtpSynced) {
      Serial.println("[NTP Sync] Initial sync: DS3231 aligned.");
      initialNtpSynced = true; 
    }
  }
}

void handleTime() {
  static bool lastWifiState = false;
  bool currentWifiState = (WiFi.status() == WL_CONNECTED);

  // התאוששות מניתוק: אם החיבור חזר, מאלצים סנכרון זמן מיידי
  if (currentWifiState && !lastWifiState) {
    lastNtpSyncTime = millis() - NTP_SYNC_INTERVAL; 
    initialNtpSynced = false; 
  }
  lastWifiState = currentWifiState;

  if (!currentWifiState) {
    return;
  }

  unsigned long currentMillis = millis();

  if (!initialNtpSynced || (currentMillis - lastNtpSyncTime >= NTP_SYNC_INTERVAL)) {
    lastNtpSyncTime = currentMillis;
    updateRTCFromNTP();
  }
}

void printCurrentTime() {
  if (rtcFound) {
    DateTime rtcNow = rtc.now();
    Serial.printf("[RTC Hardware] %02d/%02d/%04d %02d:%02d:%02d\n",
                  rtcNow.day(), rtcNow.month(), rtcNow.year(),
                  rtcNow.hour(), rtcNow.minute(), rtcNow.second());
  } else {
    Serial.println("[RTC Hardware] Module not detected!");
  }

  time_t now;
  struct tm timeinfo;
  time(&now);
  
  // גם פה נוסיף הגבלת זמן כדי למנוע השהיות
  if (getLocalTime(&timeinfo, 10)) {
    Serial.printf("[System NTP]   %02d/%02d/%04d %02d:%02d:%02d\n",
                  timeinfo.tm_mday, timeinfo.tm_mon + 1, timeinfo.tm_year + 1900,
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    Serial.println("[System NTP]   Not synced yet.");
  }
}