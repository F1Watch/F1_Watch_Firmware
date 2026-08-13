#include <Arduino.h>
#include <M5Dial.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>

// ============================================================
// M5Dial F1 LIVE
// ============================================================
//
// 주요 기능
// 1. Wi-Fi 정보를 코드에 저장하지 않음
// 2. 최초 부팅 시 F1-Dial-Setup AP 생성
// 3. 스마트폰에서 Wi-Fi 설정
// 4. Wi-Fi 정보 NVS 저장
// 5. 다음 부팅부터 자동 연결
// 6. 빠른 NTP 동기화
// 7. 시계 1초 갱신 시 전체 화면 깜빡임 방지
// 8. Next Race 카운트다운 영역만 갱신
//
// ============================================================


// ============================================================
// Fonts
// ============================================================

#define FONT_ASCII (&fonts::FreeSans9pt7b)


// ============================================================
// Preferences
// ============================================================

Preferences prefs;

#define NVS_NS       "f1app"

#define NVS_BUZ      "buzzer"
#define NVS_BRT      "bright"
#define NVS_TMF      "timefmt"
#define NVS_PGO      "pageorder"

#define NVS_WIFI_SSID "wifi_ssid"
#define NVS_WIFI_PASS "wifi_pass"


// ============================================================
// Wi-Fi Captive Portal
// ============================================================

WebServer webServer(80);
DNSServer dnsServer;

const byte DNS_PORT = 53;

const char* AP_SSID = "F1-Dial-Setup";

bool setupPortalRunning = false;
bool wifiCredentialsSaved = false;

String wifiSSID = "";
String wifiPassword = "";


// ============================================================
// NTP
// ============================================================

const long GMT_OFFSET = 9 * 3600;
const int DST_OFFSET = 0;

const char* NTP1 = "time.google.com";
const char* NTP2 = "pool.ntp.org";
const char* NTP3 = "time.nist.gov";


// ============================================================
// API URLs
// ============================================================

const char* JOLPICA_SCHEDULE =
  "https://api.jolpi.ca/ergast/f1/2026.json";

const char* JOLPICA_STANDINGS =
  "https://api.jolpi.ca/ergast/f1/2026/driverStandings.json";

const char* OPENF1_SESSIONS =
  "https://api.openf1.org/v1/sessions?year=2026&session_key=latest";

const char* OPENF1_POSITION =
  "https://api.openf1.org/v1/position?session_key=latest";

const char* OPENF1_DRIVERS =
  "https://api.openf1.org/v1/drivers?session_key=latest";


// ============================================================
// Colors
// ============================================================

#define C_BG      0x0000
#define C_RED     0xF800
#define C_WHITE   0xFFFF
#define C_GRAY    0x7BEF
#define C_LGRAY   0xBDF7
#define C_YELLOW  0xFFE0
#define C_GREEN   0x07E0
#define C_ORANGE  0xFD20
#define C_CYAN    0x07FF
#define C_GOLD    0xFEA0
#define C_DARK    0x2104
#define C_BLUE    0x001F
#define C_PURPLE  0x780F

#define CX 120
#define CY 120


// ============================================================
// Favorite driver / team badge (clock screen)
// ------------------------------------------------------------
// Change only these two values to personalise the clock.
// TEAM_ACCENT uses RGB565, which is the colour format used by M5Dial.
// Example: Ferrari red = 0xF800, McLaren papaya = 0xFD20
// ============================================================

#define TEAM_INITIALS "VER"
#define TEAM_ACCENT   0x000B  // navy blue


// ============================================================
// Page IDs
// ============================================================

#define PID_CLOCK     0
#define PID_NEXT      1
#define PID_SCHEDULE  2
#define PID_STAND     3
#define PID_LIVE      4
#define PID_SETTINGS  5

#define PAGE_COUNT 6

int pageOrder[PAGE_COUNT] = {
  PID_CLOCK,
  PID_NEXT,
  PID_SCHEDULE,
  PID_STAND,
  PID_LIVE,
  PID_SETTINGS
};

int currentSlot = 0;


// ============================================================
// Race structures
// ============================================================

struct Race {
  String name;
  String date;
  String raceTime;
  bool isSprint;
};

struct Driver {
  int pos;
  String code;
  int points;
};

struct LiveDriver {
  int pos;
  String code;
  int driverNum;
};

struct DrvMap {
  uint8_t num;
  char code[4];
};


// ============================================================
// Data
// ============================================================

Race nextRace;

Race upcomingRaces[20];
int upcomingCount = 0;

Driver standings[20];
int standCount = 0;
int standPage = 0;
bool standScrollMode = false;

LiveDriver livePos[20];
int liveCount = 0;

String sessionLabel = "";
bool isLiveSession = false;

String liveSessionStatus = "";
String prevSessionStatus = "";

bool raceResultShown = false;


// ============================================================
// Schedule
// ============================================================

int schedScrollOff = 0;
bool schedScrollMode = false;


// ============================================================
// Timing
// ============================================================

unsigned long lastApiUpdate = 0;
unsigned long lastLiveUpdate = 0;
unsigned long lastClockDraw = 0;
unsigned long lastNextDraw = 0;

const unsigned long API_INTERVAL =
  5UL * 60UL * 1000UL;

const unsigned long LIVE_INTERVAL =
  4UL * 1000UL;

const unsigned long CLOCK_INTERVAL =
  1000UL;


// ============================================================
// State
// ============================================================

bool wifiOk = false;
bool ntpSync = false;
bool dataOk = false;


// ============================================================
// Settings
// ============================================================

bool buzzerEnabled = true;

int brightness = 200;

bool use12h = false;

// 중요:
// 기존 코드에서 빠져서 발생했던
// 'clockInverted was not declared' 오류 방지
bool clockInverted = false;

bool editingPageOrder = false;

int editingSlot = 0;


// ============================================================
// Settings menu
// ============================================================

#define SETTING_COUNT 5

#define SETTING_BUZZER       0
#define SETTING_BRIGHTNESS   1
#define SETTING_WIFI_SETUP   2
#define SETTING_WIFI_FORGET  3
#define SETTING_PAGE_ORDER   4

int settingSelIdx = 0;

bool settingEditing = false;

bool settingScrollMode = false;


// ============================================================
// Touch
// ============================================================

bool touchWasDown = false;

uint32_t touchDownAt = 0;

int touchX = 0;
int touchY = 0;


// ============================================================
// Encoder
// ============================================================

long lastEncPos = 0;


// ============================================================
// Forward declarations
// ============================================================

void loadSettings();
void saveSettings();

void loadPageOrder();
void savePageOrder();

void beep(int freq, int ms);

String formatTime(int h, int m);

String currentTimeStr();
String currentAmPm();
String currentDateStr();
String currentDayStr();

bool timeIsValid();

String todayStr();

String epochToDate(time_t t);

int daysDiff(const String& target);

String timeUntilRace(
  const String& date,
  const String& raceTime
);

String shortRaceName(
  const String& name
);

uint16_t teamColor(
  const String& code
);

String sessionEn(
  const String& raw
);

const char* lookupDriver(
  int num
);

String httpGet(
  const String& url
);

void fetchSchedule();
void fetchStandings();
void fetchSession();
void fetchLivePositions();

void refreshData();

void clearScreen();

void drawHeader(
  const String& title,
  uint16_t color
);

void drawPageDots(
  int slot
);

void drawSignalBars(
  int x,
  int y,
  int bars,
  uint16_t col
);

void drawKR(
  const String& text,
  int x,
  int y,
  uint16_t col,
  textdatum_t datum = middle_center,
  uint8_t sz = 1
);

void drawClockPage();
void drawClockTimeOnly();
void drawFavoriteBadge();

void drawNextRacePage(
  bool redrawAll = true
);

void drawSchedulePage();
void drawStandingsPage();
void drawLivePage();
void drawSettingsPage();
void drawPageOrderEditor();

void drawCurrentPage();

void showSplash();

bool connectWifiOnce();
void connectWifi();

void startWiFiSetupPortal();
void stopWiFiSetupPortal();

void handleRoot();
void handleScan();
void handleSaveWifi();
void handleNotFound();

bool loadWiFiCredentials();
void saveWiFiCredentials(
  const String& ssid,
  const String& password
);

void clearWiFiCredentials();

bool connectStoredWiFi();

bool syncTimeFast();

bool handleTouch(int pid);


// ============================================================
// Settings
// ============================================================

void loadSettings() {

  prefs.begin(NVS_NS, true);

  buzzerEnabled =
    prefs.getBool(NVS_BUZ, true);

  brightness =
    prefs.getInt(NVS_BRT, 200);

  use12h =
    prefs.getBool(NVS_TMF, false);

  prefs.end();

  brightness =
    constrain(brightness, 20, 255);

  M5Dial.Display.setBrightness(
    brightness
  );
}


void saveSettings() {

  prefs.begin(NVS_NS, false);

  prefs.putBool(
    NVS_BUZ,
    buzzerEnabled
  );

  prefs.putInt(
    NVS_BRT,
    brightness
  );

  prefs.putBool(
    NVS_TMF,
    use12h
  );

  prefs.end();
}


void loadPageOrder() {

  prefs.begin(NVS_NS, true);

  for (int i = 0; i < PAGE_COUNT; i++) {

    pageOrder[i] =
      prefs.getInt(
        (String(NVS_PGO) + i).c_str(),
        i
      );

    if (
      pageOrder[i] < 0 ||
      pageOrder[i] >= PAGE_COUNT
    ) {
      pageOrder[i] = i;
    }
  }

  prefs.end();
}


void savePageOrder() {

  prefs.begin(NVS_NS, false);

  for (int i = 0; i < PAGE_COUNT; i++) {

    prefs.putInt(
      (String(NVS_PGO) + i).c_str(),
      pageOrder[i]
    );
  }

  prefs.end();
}


void beep(
  int freq,
  int ms
) {

  if (buzzerEnabled) {
    M5Dial.Speaker.tone(
      freq,
      ms
    );
  }
}


// ============================================================
// Wi-Fi NVS
// ============================================================

bool loadWiFiCredentials() {

  prefs.begin(
    NVS_NS,
    true
  );

  wifiSSID =
    prefs.getString(
      NVS_WIFI_SSID,
      ""
    );

  wifiPassword =
    prefs.getString(
      NVS_WIFI_PASS,
      ""
    );

  prefs.end();

  wifiCredentialsSaved =
    wifiSSID.length() > 0;

  return wifiCredentialsSaved;
}


void saveWiFiCredentials(
  const String& ssid,
  const String& password
) {

  prefs.begin(
    NVS_NS,
    false
  );

  prefs.putString(
    NVS_WIFI_SSID,
    ssid
  );

  prefs.putString(
    NVS_WIFI_PASS,
    password
  );

  prefs.end();

  wifiSSID = ssid;
  wifiPassword = password;

  wifiCredentialsSaved = true;
}


void clearWiFiCredentials() {

  prefs.begin(
    NVS_NS,
    false
  );

  prefs.remove(
    NVS_WIFI_SSID
  );

  prefs.remove(
    NVS_WIFI_PASS
  );

  prefs.end();

  wifiSSID = "";
  wifiPassword = "";

  wifiCredentialsSaved = false;

  WiFi.disconnect(
    true,
    true
  );

  delay(300);
}


// ============================================================
// Time helpers
// ============================================================

bool timeIsValid() {

  time_t now = time(nullptr);

  // 2023년 이후 정도면 정상적인 NTP 시간으로 판단
  return now > 1700000000;
}


String formatTime(
  int h,
  int m
) {

  if (use12h) {

    if (h > 12)
      h -= 12;

    if (h == 0)
      h = 12;
  }

  char buf[16];

  snprintf(
    buf,
    sizeof(buf),
    "%02d : %02d",
    h,
    m
  );

  return String(buf);
}


String currentTimeStr() {

  if (
    !ntpSync ||
    !timeIsValid()
  ) {
    return "-- : --";
  }

  time_t now = time(nullptr);

  struct tm ti;

  localtime_r(
    &now,
    &ti
  );

  return formatTime(
    ti.tm_hour,
    ti.tm_min
  );
}


String currentAmPm() {

  if (
    !use12h ||
    !ntpSync ||
    !timeIsValid()
  ) {
    return "";
  }

  time_t now = time(nullptr);

  struct tm ti;

  localtime_r(
    &now,
    &ti
  );

  return
    ti.tm_hour >= 12
      ? "PM"
      : "AM";
}


String currentDateStr() {

  if (
    !ntpSync ||
    !timeIsValid()
  ) {
    return "----/--/--";
  }

  time_t now = time(nullptr);

  struct tm ti;

  localtime_r(
    &now,
    &ti
  );

  char buf[20];

  snprintf(
    buf,
    sizeof(buf),
    "%04d/%02d/%02d",
    ti.tm_year + 1900,
    ti.tm_mon + 1,
    ti.tm_mday
  );

  return String(buf);
}


String currentDayStr() {

  if (
    !ntpSync ||
    !timeIsValid()
  ) {
    return "";
  }

  time_t now = time(nullptr);

  struct tm ti;

  localtime_r(
    &now,
    &ti
  );

  const char* days[] = {
    "SUNDAY",
    "MONDAY",
    "TUESDAY",
    "WEDNESDAY",
    "THURSDAY",
    "FRIDAY",
    "SATURDAY"
  };

  return String(
    days[ti.tm_wday]
  );
}


String epochToDate(
  time_t t
) {

  struct tm ti;

  localtime_r(
    &t,
    &ti
  );

  char buf[12];

  snprintf(
    buf,
    sizeof(buf),
    "%04d-%02d-%02d",
    ti.tm_year + 1900,
    ti.tm_mon + 1,
    ti.tm_mday
  );

  return String(buf);
}


String todayStr() {

  return epochToDate(
    time(nullptr)
  );
}


int daysDiff(
  const String& target
) {

  int ty = 0;
  int tm_ = 0;
  int td = 0;

  if (
    sscanf(
      target.c_str(),
      "%d-%d-%d",
      &ty,
      &tm_,
      &td
    ) != 3
  ) {
    return 9999;
  }

  if (
    ty < 2000 ||
    ty > 2100 ||
    tm_ < 1 ||
    tm_ > 12 ||
    td < 1 ||
    td > 31
  ) {
    return 9999;
  }

  if (!timeIsValid())
    return 9999;

  struct tm t1 = {};

  t1.tm_year = ty - 1900;
  t1.tm_mon = tm_ - 1;
  t1.tm_mday = td;
  t1.tm_hour = 0;
  t1.tm_min = 0;
  t1.tm_sec = 0;
  t1.tm_isdst = -1;

  time_t tt1 =
    mktime(&t1);

  time_t now =
    time(nullptr);

  struct tm t2 = {};

  localtime_r(
    &now,
    &t2
  );

  t2.tm_hour = 0;
  t2.tm_min = 0;
  t2.tm_sec = 0;
  t2.tm_isdst = -1;

  time_t tt2 =
    mktime(&t2);

  if (
    tt1 == -1 ||
    tt2 == -1
  ) {
    return 9999;
  }

  long diffSec =
    (long)(tt1 - tt2);

  return
    (int)((diffSec + 43200) / 86400);
}


String timeUntilRace(
  const String& date,
  const String& raceTime
) {

  if (
    date.length() < 8 ||
    raceTime.length() < 5 ||
    !timeIsValid()
  ) {
    return "";
  }

  int ty = 0;
  int tm_ = 0;
  int td = 0;
  int th = 0;
  int tmm = 0;

  if (
    sscanf(
      date.c_str(),
      "%d-%d-%d",
      &ty,
      &tm_,
      &td
    ) != 3
  ) {
    return "";
  }

  if (
    sscanf(
      raceTime.c_str(),
      "%d:%d",
      &th,
      &tmm
    ) != 2
  ) {
    return "";
  }

  struct tm t1 = {};

  t1.tm_year = ty - 1900;
  t1.tm_mon = tm_ - 1;
  t1.tm_mday = td;
  t1.tm_hour = th;
  t1.tm_min = tmm;
  t1.tm_sec = 0;
  t1.tm_isdst = -1;

  time_t raceTs =
    mktime(&t1);

  time_t now =
    time(nullptr);

  long diff =
    (long)(raceTs - now);

  if (diff <= 0)
    return "STARTING NOW!";

  int hours =
    (int)(diff / 3600);

  int mins =
    (int)((diff % 3600) / 60);

  if (hours > 0) {

    char buf[24];

    snprintf(
      buf,
      sizeof(buf),
      "%dh %dm left",
      hours,
      mins
    );

    return String(buf);

  } else {

    char buf[16];

    snprintf(
      buf,
      sizeof(buf),
      "%dm left",
      mins
    );

    return String(buf);
  }
}


// ============================================================
// Race helpers
// ============================================================

String shortRaceName(
  const String& name
) {

  String result = name;

  result.replace(
    " Grand Prix",
    ""
  );

  result.replace(
    "Grand Prix",
    ""
  );

  result.trim();

  return result;
}


uint16_t teamColor(
  const String& code
) {

  if (
    code == "ANT" ||
    code == "RUS"
  )
    return C_CYAN;

  if (
    code == "HAM" ||
    code == "LEC"
  )
    return C_RED;

  if (
    code == "VER" ||
    code == "LAW"
  )
    return C_BLUE;

  if (
    code == "NOR" ||
    code == "PIA"
  )
    return 0xFF80;

  if (
    code == "ALO" ||
    code == "STR"
  )
    return 0x05FF;

  if (
    code == "GAS" ||
    code == "DOO"
  )
    return 0xF81F;

  if (
    code == "ALB" ||
    code == "SAI"
  )
    return C_BLUE;

  if (
    code == "HUL" ||
    code == "BEA"
  )
    return C_RED;

  if (
    code == "TSU" ||
    code == "HAD"
  )
    return 0xFFC0;

  if (
    code == "BOT" ||
    code == "ZHO"
  )
    return 0x03EF;

  return C_GRAY;
}


String sessionEn(
  const String& raw
) {

  if (raw == "Race")
    return "Race";

  if (raw == "Qualifying")
    return "Qualifying";

  if (raw == "Sprint")
    return "Sprint";

  if (raw == "Sprint Qualifying")
    return "Sprint Qualifying";

  if (raw == "Sprint Shootout")
    return "Sprint Shootout";

  if (raw == "Practice 1")
    return "Practice 1";

  if (raw == "Practice 2")
    return "Practice 2";

  if (raw == "Practice 3")
    return "Practice 3";

  return raw;
}


// ============================================================
// HTTP
// ============================================================

String httpGet(
  const String& url
) {

  if (
    WiFi.status() != WL_CONNECTED
  ) {
    return "";
  }

  HTTPClient http;

  http.begin(url);

  http.addHeader(
    "User-Agent",
    "M5Dial-F1/1.0"
  );

  http.setTimeout(8000);

  int code =
    http.GET();

  String body = "";

  if (code == 200)
    body = http.getString();

  http.end();

  return body;
}


// ============================================================
// Schedule
// ============================================================

void fetchSchedule() {

  String body =
    httpGet(
      JOLPICA_SCHEDULE
    );

  if (body.isEmpty())
    return;

  DynamicJsonDocument doc(
    16384
  );

  if (
    deserializeJson(
      doc,
      body
    ) != DeserializationError::Ok
  ) {
    return;
  }

  JsonArray races =
    doc["MRData"]["RaceTable"]["Races"]
      .as<JsonArray>();

  upcomingCount = 0;

  bool foundNext = false;

  for (
    JsonObject r : races
  ) {

    String date =
      r["date"] | "";

    String name =
      r["raceName"] | "";

    bool sprint =
      r.containsKey("Sprint");

    String utcTime =
      r["time"] | "";

    String kstTime = "";

    if (utcTime.length() >= 5) {

      int uh = 0;
      int um = 0;

      if (
        sscanf(
          utcTime.c_str(),
          "%d:%d",
          &uh,
          &um
        ) == 2
      ) {

        int kh =
          (uh + 9) % 24;

        char tbuf[6];

        snprintf(
          tbuf,
          sizeof(tbuf),
          "%02d:%02d",
          kh,
          um
        );

        kstTime =
          String(tbuf);
      }
    }

    if (
      daysDiff(date) >= 0
    ) {

      Race rc = {
        name,
        date,
        kstTime,
        sprint
      };

      if (!foundNext) {

        nextRace = rc;

        foundNext = true;

      } else if (
        upcomingCount < 20
      ) {

        upcomingRaces[
          upcomingCount++
        ] = rc;
      }
    }
  }

  if (!foundNext) {

    nextRace = {
      "Season Complete",
      "",
      "",
      false
    };
  }
}


// ============================================================
// Standings
// ============================================================

void fetchStandings() {

  String body =
    httpGet(
      JOLPICA_STANDINGS
    );

  if (body.isEmpty())
    return;

  DynamicJsonDocument doc(
    8192
  );

  if (
    deserializeJson(
      doc,
      body
    ) != DeserializationError::Ok
  ) {
    return;
  }

  JsonArray list =
    doc["MRData"]
       ["StandingsTable"]
       ["StandingsLists"][0]
       ["DriverStandings"]
       .as<JsonArray>();

  standCount = 0;

  for (
    JsonObject d : list
  ) {

    if (standCount >= 20)
      break;

    standings[
      standCount
    ].pos =
      String(
        d["position"] | "0"
      ).toInt();

    standings[
      standCount
    ].points =
      String(
        d["points"] | "0"
      ).toInt();

    standings[
      standCount
    ].code =
      d["Driver"]["code"] |
      "???";

    standCount++;
  }

  int maxPage =
    max(
      0,
      (standCount - 1) / 8
    );

  if (standPage > maxPage)
    standPage = 0;
}


// ============================================================
// Session
// ============================================================

void fetchSession() {

  String body =
    httpGet(
      OPENF1_SESSIONS
    );

  if (body.isEmpty()) {

    isLiveSession = false;

    return;
  }

  DynamicJsonDocument doc(
    4096
  );

  if (
    deserializeJson(
      doc,
      body
    ) != DeserializationError::Ok
  ) {

    isLiveSession = false;

    return;
  }

  JsonArray arr =
    doc.as<JsonArray>();

  if (!arr.size()) {

    isLiveSession = false;

    return;
  }

  JsonObject s =
    arr[arr.size() - 1];

  sessionLabel =
    sessionEn(
      s["session_type"] | ""
    );

  liveSessionStatus =
    s["status"] | "";

  isLiveSession =
    (
      liveSessionStatus == "started" ||
      liveSessionStatus == "active"
    );
}


// ============================================================
// Driver map
// ============================================================

static DrvMap drvCodeMap[20];

static uint8_t drvMapCount = 0;

static bool drvMapLoaded = false;


const char* lookupDriver(
  int num
) {

  for (
    int i = 0;
    i < drvMapCount;
    i++
  ) {

    if (
      drvCodeMap[i].num ==
      num
    ) {
      return drvCodeMap[i].code;
    }
  }

  return "???";
}


// ============================================================
// Live positions
// ============================================================

void fetchLivePositions() {

  if (!drvMapLoaded) {

    String body =
      httpGet(
        OPENF1_DRIVERS
      );

    if (!body.isEmpty()) {

      DynamicJsonDocument doc(
        4096
      );

      if (
        deserializeJson(
          doc,
          body
        ) == DeserializationError::Ok
      ) {

        drvMapCount = 0;

        for (
          JsonObject d :
          doc.as<JsonArray>()
        ) {

          if (
            drvMapCount >= 20
          )
            break;

          int num =
            d["driver_number"] | 0;

          const char* acr =
            d["name_acronym"] |
            "???";

          drvCodeMap[
            drvMapCount
          ].num =
            (uint8_t)num;

          strncpy(
            drvCodeMap[
              drvMapCount
            ].code,
            acr,
            3
          );

          drvCodeMap[
            drvMapCount
          ].code[3] =
            '\0';

          drvMapCount++;
        }

        drvMapLoaded = true;
      }
    }
  }

  String body =
    httpGet(
      OPENF1_POSITION
    );

  if (body.isEmpty())
    return;

  DynamicJsonDocument doc(
    12288
  );

  if (
    deserializeJson(
      doc,
      body
    ) != DeserializationError::Ok
  ) {
    return;
  }

  struct PosEntry {

    int8_t pos;

    uint8_t num;

    bool seen;
  };

  static PosEntry posMap[20];

  for (
    int i = 0;
    i < 20;
    i++
  ) {

    posMap[i] = {
      0,
      0,
      false
    };
  }

  JsonArray arr =
    doc.as<JsonArray>();

  for (
    int i = (int)arr.size() - 1;
    i >= 0;
    i--
  ) {

    int num =
      arr[i]["driver_number"] |
      -1;

    if (num < 0)
      continue;

    bool already = false;

    for (
      int j = 0;
      j < 20;
      j++
    ) {

      if (
        posMap[j].seen &&
        posMap[j].num ==
        num
      ) {

        already = true;

        break;
      }
    }

    if (already)
      continue;

    for (
      int j = 0;
      j < 20;
      j++
    ) {

      if (
        !posMap[j].seen
      ) {

        posMap[j] = {
          (int8_t)(
            (int)(
              arr[i]["position"] | 0
            )
          ),
          (uint8_t)num,
          true
        };

        break;
      }
    }
  }

  liveCount = 0;

  for (
    int p = 1;
    p <= 20 &&
    liveCount < 20;
    p++
  ) {

    for (
      int j = 0;
      j < 20;
      j++
    ) {

      if (
        posMap[j].seen &&
        posMap[j].pos == p
      ) {

        livePos[
          liveCount
        ] = {
          p,
          String(
            lookupDriver(
              posMap[j].num
            )
          ),
          (int)
            posMap[j].num
        };

        liveCount++;

        break;
      }
    }
  }
}


// ============================================================
// Refresh data
// ============================================================

void refreshData() {

  fetchSession();

  bool justStarted =
    (
      prevSessionStatus != "started" &&
      prevSessionStatus != "active"
    ) &&
    (
      liveSessionStatus == "started" ||
      liveSessionStatus == "active"
    );

  if (justStarted) {

    raceResultShown = false;

    for (
      int i = 0;
      i < 3;
      i++
    ) {

      beep(
        1400,
        120
      );

      delay(200);
    }

    for (
      int i = 0;
      i < PAGE_COUNT;
      i++
    ) {

      if (
        pageOrder[i] ==
        PID_LIVE
      ) {

        currentSlot = i;

        break;
      }
    }
  }

  bool justFinished =
    (
      prevSessionStatus == "started" ||
      prevSessionStatus == "active"
    ) &&
    (
      liveSessionStatus == "finished" ||
      liveSessionStatus == "inactive"
    );

  if (
    justFinished &&
    !raceResultShown
  ) {

    raceResultShown = true;

    beep(1000, 80);
    delay(120);

    beep(1200, 80);
    delay(120);

    beep(1500, 150);

    for (
      int i = 0;
      i < PAGE_COUNT;
      i++
    ) {

      if (
        pageOrder[i] ==
        PID_LIVE
      ) {

        currentSlot = i;

        break;
      }
    }
  }

  prevSessionStatus =
    liveSessionStatus;

  fetchSchedule();

  fetchStandings();

  if (isLiveSession)
    fetchLivePositions();

  dataOk = true;
}


// ============================================================
// Drawing helpers
// ============================================================

void clearScreen() {

  M5Dial.Display.fillCircle(
    CX,
    CY,
    120,
    C_BG
  );
}


void drawHeader(
  const String& title,
  uint16_t color
) {

  clearScreen();

  M5Dial.Display.fillRoundRect(
    42,
    36,
    156,
    24,
    7,
    color
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.setTextSize(
    1
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.drawString(
    title,
    CX,
    48
  );
}


void drawPageDots(
  int slot
) {

  int dotY = 212;

  int spacing = 16;

  int startX =
    CX -
    (PAGE_COUNT - 1) *
    spacing / 2;

  for (
    int i = 0;
    i < PAGE_COUNT;
    i++
  ) {

    uint16_t col =
      (i == slot)
        ? C_WHITE
        : C_GRAY;

    int r =
      (i == slot)
        ? 5
        : 3;

    M5Dial.Display.fillCircle(
      startX + i * spacing,
      dotY,
      r,
      col
    );
  }
}


void drawSignalBars(
  int x,
  int y,
  int bars,
  uint16_t col
) {

  for (
    int b = 0;
    b < 4;
    b++
  ) {

    int bh =
      3 + b * 3;

    uint16_t c =
      (b < bars)
        ? col
        : C_DARK;

    M5Dial.Display.fillRect(
      x + b * 6,
      y - bh,
      4,
      bh,
      c
    );
  }
}


void drawKR(
  const String& text,
  int x,
  int y,
  uint16_t col,
  textdatum_t datum,
  uint8_t sz
) {

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    col
  );

  M5Dial.Display.setTextDatum(
    datum
  );

  M5Dial.Display.setTextSize(
    sz
  );

  M5Dial.Display.drawString(
    text,
    x,
    y
  );
}


// ============================================================
// CLOCK PAGE
// ============================================================

// Vertical team-colour bar + initials, inspired by a race broadcast title.
void drawFavoriteBadge() {

  const int barY = 148;
  const int barW = 10;
  const int barH = 32;
  const int gap = 12;

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  // Match the size used for the clock time.
  M5Dial.Display.setTextSize(
    2
  );

  // Centre the colour bar and initials as one visual group.
  const int initialsW = M5Dial.Display.textWidth(
    TEAM_INITIALS
  );

  const int groupW =
    barW + gap + initialsW;

  const int barX =
    CX - groupW / 2;

  M5Dial.Display.fillRoundRect(
    barX,
    barY,
    barW,
    barH,
    3,
    TEAM_ACCENT
  );

  // Keep the initials in the foreground colour in both light and dark modes.
  M5Dial.Display.setTextColor(
    clockInverted
      ? C_BG
      : C_WHITE
  );

  M5Dial.Display.setTextDatum(
    middle_left
  );

  M5Dial.Display.drawString(
    TEAM_INITIALS,
    barX + barW + gap,
    barY + barH / 2
  );
}

void drawClockPage() {

  const uint16_t bg =
    clockInverted
      ? C_WHITE
      : C_BG;

  const uint16_t fg =
    clockInverted
      ? C_BG
      : C_WHITE;

  const uint16_t sub =
    clockInverted
      ? C_DARK
      : C_LGRAY;

  M5Dial.Display.fillScreen(
    bg
  );

  M5Dial.Display.fillCircle(
    CX,
    CY,
    120,
    bg
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  // Time
  String ts =
    currentTimeStr();

  M5Dial.Display.setTextColor(
    fg
  );

  if (use12h) {

    String suffix =
      currentAmPm();

    M5Dial.Display.setTextSize(
      2
    );

    int timeW =
      M5Dial.Display.textWidth(
        ts
      );

    M5Dial.Display.setTextSize(
      1
    );

    int suffixW =
      M5Dial.Display.textWidth(
        suffix
      );

    int gap = 6;

    int left =
      CX -
      (
        timeW +
        gap +
        suffixW
      ) / 2;

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      ts,
      left,
      104
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      suffix,
      left + timeW + gap,
      104
    );

  } else {

    M5Dial.Display.setTextDatum(
      middle_center
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      ts,
      CX,
      104
    );
  }

  M5Dial.Display.setTextSize(
    1
  );

  M5Dial.Display.setTextColor(
    sub
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.drawString(
    currentDateStr(),
    CX,
    42
  );

  if (
    ntpSync &&
    timeIsValid()
  ) {
    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextColor(
      C_WHITE
    );

    M5Dial.Display.setTextDatum(
      middle_center
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      currentDayStr(),
      CX,
      66
    );
  }

  // Bottom item: the centred team-colour bar and driver initials.
  drawFavoriteBadge();

  int dotY = 212;
  int spacing = 16;

  int startX =
    CX -
    (PAGE_COUNT - 1) *
    spacing / 2;

  for (
    int i = 0;
    i < PAGE_COUNT;
    i++
  ) {

    uint16_t col =
      (i == currentSlot)
        ? fg
        : sub;

    M5Dial.Display.fillCircle(
      startX + i * spacing,
      dotY,
      (i == currentSlot)
        ? 5
        : 3,
      col
    );
  }
}


// ------------------------------------------------------------
// Clock: ONLY time region update
// ------------------------------------------------------------

void drawClockTimeOnly() {

  const uint16_t bg =
    clockInverted
      ? C_WHITE
      : C_BG;

  const uint16_t fg =
    clockInverted
      ? C_BG
      : C_WHITE;

  // 기존 화면에서 시간 영역만 삭제
  M5Dial.Display.fillRect(
    35,
    78,
    170,
    52,
    bg
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    fg
  );

  if (use12h) {

    String ts =
      currentTimeStr();

    String suffix =
      currentAmPm();

    M5Dial.Display.setTextSize(
      2
    );

    int timeW =
      M5Dial.Display.textWidth(
        ts
      );

    M5Dial.Display.setTextSize(
      1
    );

    int suffixW =
      M5Dial.Display.textWidth(
        suffix
      );

    int gap = 6;

    int left =
      CX -
      (
        timeW +
        gap +
        suffixW
      ) / 2;

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      ts,
      left,
      104
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      suffix,
      left + timeW + gap,
      104
    );

  } else {

    M5Dial.Display.setTextDatum(
      middle_center
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      currentTimeStr(),
      CX,
      104
    );
  }
}


// ============================================================
// NEXT RACE
// ============================================================

void drawNextRacePage(
  bool redrawAll
) {

  if (redrawAll) {

    drawHeader(
      "[ Next Race ]",
      C_RED
    );

    if (!dataOk) {

      drawKR(
        "No data",
        CX,
        CY,
        C_GRAY
      );

      drawPageDots(
        currentSlot
      );

      return;
    }

    String rname =
      shortRaceName(
        nextRace.name
      );

    if (rname.length() > 16)
      rname =
        rname.substring(0, 14) +
        "..";

    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextColor(
      C_YELLOW
    );

    M5Dial.Display.setTextDatum(
      middle_center
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      rname,
      CX,
      76
    );

    String dtStr =
      nextRace.date;

    if (
      nextRace.raceTime.length() > 0
    ) {

      dtStr += "  ";
      dtStr += nextRace.raceTime;
    }

    M5Dial.Display.setTextColor(
      C_LGRAY
    );

    M5Dial.Display.drawString(
      dtStr,
      CX,
      98
    );

    if (nextRace.isSprint) {

      M5Dial.Display.fillRoundRect(
        52,
        112,
        136,
        22,
        11,
        C_ORANGE
      );

      M5Dial.Display.setTextColor(
        C_BG
      );

      M5Dial.Display.drawString(
        "SP SPRINT",
        CX,
        123
      );
    }

    if (upcomingCount > 0) {

      M5Dial.Display.setTextColor(
        C_GRAY
      );

      String nextName =
        shortRaceName(
          upcomingRaces[0].name
        );

      String preview;

      if (
        upcomingRaces[0].date.length()
        >= 7
      ) {

        preview =
          upcomingRaces[0]
            .date
            .substring(5);

        preview += " ";

        preview +=
          nextName.substring(
            0,
            min(
              10,
              (int)nextName.length()
            )
          );

      } else {

        preview =
          nextName.substring(
            0,
            min(
              14,
              (int)nextName.length()
            )
          );
      }

      M5Dial.Display.drawString(
        preview,
        CX,
        194
      );
    }

    drawPageDots(
      currentSlot
    );
  }

  if (!dataOk)
    return;

  const int countdownY =
    nextRace.isSprint
      ? 154
      : 132;

  M5Dial.Display.fillRect(
    42,
    countdownY - 18,
    156,
    42,
    C_BG
  );

  int d =
    nextRace.date.isEmpty()
      ? 9999
      : daysDiff(
          nextRace.date
        );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  if (d == 9999) {

    drawKR(
      "Time unavailable",
      CX,
      countdownY,
      C_GRAY
    );

  } else if (d == 0) {

    M5Dial.Display.setTextColor(
      C_GREEN
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      "Today!",
      CX,
      countdownY
    );

    String timer =
      timeUntilRace(
        nextRace.date,
        nextRace.raceTime
      );

    if (timer.length() > 0) {

      drawKR(
        timer,
        CX,
        countdownY + 20,
        C_CYAN
      );
    }

  } else if (d == 1) {

    drawKR(
      "Tomorrow!",
      CX,
      countdownY,
      C_CYAN
    );

  } else {

    M5Dial.Display.setTextColor(
      C_GOLD
    );

    M5Dial.Display.setTextSize(
      2
    );

    M5Dial.Display.drawString(
      "D - " + String(d),
      CX,
      countdownY
    );
  }
}


// ============================================================
// SCHEDULE
// ============================================================

void drawSchedulePage() {

  drawHeader(
    "[ Schedule ]",
    0x0343
  );

  if (
    !dataOk ||
    upcomingCount == 0
  ) {

    drawKR(
      "No upcoming races",
      CX,
      CY,
      C_GRAY
    );

    drawKR(
      schedScrollMode
        ? "Hold: back"
        : "Button: scroll",
      CX,
      192,
      C_DARK
    );

    drawPageDots(
      currentSlot
    );

    return;
  }

  int visCount =
    min(
      4,
      upcomingCount -
      schedScrollOff
    );

  int startY = 66;

  int rowH = 30;

  for (
    int i = 0;
    i < visCount;
    i++
  ) {

    int dataIdx =
      schedScrollOff + i;

    if (
      dataIdx >= upcomingCount
    )
      break;

    Race& r =
      upcomingRaces[
        dataIdx
      ];

    int y =
      startY + i * rowH;

    uint16_t rbg =
      (dataIdx % 2 == 0)
        ? 0x0841
        : C_BG;

    M5Dial.Display.fillRoundRect(
      38,
      y,
      164,
      rowH - 4,
      4,
      rbg
    );

    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextColor(
      C_GRAY
    );

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      String(dataIdx + 1) + ".",
      46,
      y + 10
    );

    String cleanName =
      shortRaceName(
        r.name
      );

    String nm =
      cleanName.length() > 14
        ? cleanName.substring(
            0,
            12
          ) + ".."
        : cleanName;

    M5Dial.Display.setTextColor(
      C_WHITE
    );

    M5Dial.Display.drawString(
      nm,
      64,
      y + 10
    );

    String dateStr =
      r.date.length() >= 7
        ? r.date.substring(5)
        : r.date;

    if (r.isSprint)
      dateStr += " SP";

    M5Dial.Display.setTextColor(
      r.isSprint
        ? C_ORANGE
        : C_LGRAY
    );

    M5Dial.Display.drawString(
      dateStr,
      64,
      y + 26
    );
  }

  M5Dial.Display.setTextDatum(
    middle_center
  );

  if (schedScrollMode) {

    drawKR(
      "Turn  Hold: back",
      CX,
      205,
      C_YELLOW
    );

  } else {

    const int firstRace =
      schedScrollOff + 1;

    const int lastRace =
      min(
        schedScrollOff + 4,
        upcomingCount
      );

    drawKR(
      "Tap: " +
      String(firstRace) +
      "-" +
      String(lastRace),
      CX,
      198,
      C_LGRAY
    );
  }

  drawPageDots(
    currentSlot
  );
}


// ============================================================
// STANDINGS
// ============================================================

void drawStandingsPage() {

  const int startIdx =
    standPage * 8;

  const int endIdx =
    min(
      startIdx + 8,
      standCount
    );

  String hdr =
    "[ Standings " +
    String(startIdx + 1) +
    "-" +
    String(
      max(
        startIdx + 1,
        endIdx
      )
    ) +
    " ]";

  drawHeader(
    hdr,
    C_DARK
  );

  if (
    !dataOk ||
    standCount == 0
  ) {

    drawKR(
      "No data",
      CX,
      CY,
      C_GRAY
    );

    drawPageDots(
      currentSlot
    );

    return;
  }

  int showCount =
    endIdx - startIdx;

  int startY = 70;

  int rowH = 15;

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  for (
    int i = 0;
    i < showCount;
    i++
  ) {

    int y =
      startY +
      i * rowH;

    Driver& d =
      standings[
        startIdx + i
      ];

    bool isFirst =
      d.pos == 1;

    uint16_t rowColor =
      isFirst
        ? C_GOLD
        : C_WHITE;

    if (isFirst) {

      M5Dial.Display.fillRoundRect(
        42,
        y - 7,
        156,
        14,
        4,
        0x2940
      );
    }

    M5Dial.Display.setTextColor(
      C_GRAY
    );

    M5Dial.Display.setTextDatum(
      middle_right
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      String(d.pos),
      66,
      y
    );

    M5Dial.Display.fillRect(
      72,
      y - 5,
      4,
      10,
      teamColor(d.code)
    );

    M5Dial.Display.setTextColor(
      rowColor
    );

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.drawString(
      d.code,
      82,
      y
    );

    M5Dial.Display.setTextColor(
      C_YELLOW
    );

    M5Dial.Display.setTextDatum(
      middle_right
    );

    M5Dial.Display.drawString(
      String(d.points) +
      "pt",
      194,
      y
    );
  }

  if (standScrollMode) {

    drawKR(
      "< >  Hold: back",
      CX,
      205,
      C_YELLOW
    );

  } else {

    drawKR(
      "Tap: next",
      CX,
      205,
      C_LGRAY
    );
  }

  drawPageDots(
    currentSlot
  );
}


// ============================================================
// LIVE
// ============================================================

void drawLivePage() {

  uint16_t hcol =
    isLiveSession
      ? C_RED
      : C_DARK;

  String htext =
    isLiveSession
      ? "* " + sessionLabel
      : "Live (No Session)";

  drawHeader(
    htext,
    hcol
  );

  if (
    !isLiveSession ||
    liveCount == 0
  ) {

    drawKR(
      "No active session",
      CX,
      100,
      C_GRAY
    );

    drawKR(
      "Auto refresh enabled",
      CX,
      122,
      C_LGRAY
    );

    if (
      nextRace.date.length() > 0
    ) {

      int d =
        daysDiff(
          nextRace.date
        );

      if (d != 9999) {

        String hint;

        if (d == 0)
          hint = "RACE DAY!";
        else
          hint =
            "D-" +
            String(d);

        drawKR(
          hint,
          CX,
          150,
          C_GRAY
        );
      }
    }

    drawPageDots(
      currentSlot
    );

    return;
  }

  int showCount =
    min(
      liveCount,
      8
    );

  int startY = 70;

  int rowH = 15;

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  for (
    int i = 0;
    i < showCount;
    i++
  ) {

    int y =
      startY +
      i * rowH;

    LiveDriver& ld =
      livePos[i];

    if (i == 0) {

      M5Dial.Display.fillRoundRect(
        38,
        y - 8,
        164,
        18,
        4,
        0x2940
      );
    }

    String posStr =
      "P" +
      String(ld.pos);

    uint16_t posColor =
      ld.pos == 1
        ? C_GOLD
        : (
          ld.pos <= 3
            ? C_YELLOW
            : C_LGRAY
        );

    M5Dial.Display.setTextColor(
      posColor
    );

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      posStr,
      46,
      y + 2
    );

    M5Dial.Display.fillRect(
      70,
      y - 5,
      4,
      12,
      teamColor(ld.code)
    );

    M5Dial.Display.setTextColor(
      C_WHITE
    );

    M5Dial.Display.drawString(
      ld.code,
      78,
      y + 2
    );

    M5Dial.Display.setTextColor(
      C_GRAY
    );

    M5Dial.Display.setTextDatum(
      middle_right
    );

    M5Dial.Display.drawString(
      "#" +
      String(ld.driverNum),
      194,
      y + 2
    );
  }

  drawPageDots(
    currentSlot
  );
}


// ============================================================
// PAGE ORDER
// ============================================================

void drawPageOrderEditor() {

  drawHeader(
    "[ Page Order ]",
    0x2945
  );

  const char* pageNames[
    PAGE_COUNT
  ] = {
    "Clock",
    "Next Race",
    "Schedule",
    "Standings",
    "Live",
    "Settings"
  };

  int startY = 64;

  int rowH = 21;

  for (
    int i = 0;
    i < PAGE_COUNT;
    i++
  ) {

    int y =
      startY +
      i * rowH;

    bool sel =
      i == editingSlot;

    uint16_t bg =
      sel
        ? 0x1A43
        : C_DARK;

    M5Dial.Display.fillRoundRect(
      38,
      y,
      164,
      rowH - 2,
      4,
      bg
    );

    if (sel) {

      M5Dial.Display.drawRoundRect(
        38,
        y,
        164,
        rowH - 2,
        4,
        C_CYAN
      );
    }

    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextColor(
      C_GRAY
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.drawString(
      String(i + 1) + ".",
      46,
      y + rowH / 2 - 1
    );

    M5Dial.Display.setTextColor(
      sel
        ? C_WHITE
        : C_LGRAY
    );

    M5Dial.Display.drawString(
      pageNames[
        pageOrder[i]
      ],
      62,
      y + rowH / 2 - 1
    );

    if (sel) {

      M5Dial.Display.setTextColor(
        C_CYAN
      );

      M5Dial.Display.setTextDatum(
        middle_right
      );

      M5Dial.Display.drawString(
        "<>",
        194,
        y + rowH / 2 - 1
      );
    }
  }
}


// ============================================================
// SETTINGS
// ============================================================

void drawSettingsPage() {

  drawHeader(
    "[ Settings ]",
    0x2945
  );

  const char* labels[
    SETTING_COUNT
  ] = {
    "Buzzer",
    "Brightness",
    "WiFi Setup",
    "WiFi Forget",
    "Page Order"
  };

  int startY = 62;

  int rowH = 27;

  for (
    int i = 0;
    i < SETTING_COUNT;
    i++
  ) {

    int y =
      startY +
      i * rowH;

    bool sel =
      settingSelIdx == i;

    bool editing =
      settingEditing &&
      sel;

    uint16_t bg =
      sel
        ? (
          editing
            ? 0x2940
            : 0x2104
        )
        : C_DARK;

    M5Dial.Display.fillRoundRect(
      18,
      y,
      204,
      rowH - 3,
      5,
      bg
    );

    if (sel) {

      M5Dial.Display.drawRoundRect(
        18,
        y,
        204,
        rowH - 3,
        5,
        editing
          ? C_YELLOW
          : C_CYAN
      );
    }

    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.setTextDatum(
      middle_left
    );

    M5Dial.Display.setTextColor(
      C_WHITE
    );

    M5Dial.Display.drawString(
      labels[i],
      28,
      y + 11
    );

    M5Dial.Display.setTextDatum(
      middle_right
    );

    if (
      i ==
      SETTING_BUZZER
    ) {

      M5Dial.Display.fillRoundRect(
        154,
        y + 5,
        54,
        17,
        5,
        buzzerEnabled
          ? C_GREEN
          : C_GRAY
      );

      M5Dial.Display.setTextColor(
        C_BG
      );

      M5Dial.Display.drawString(
        buzzerEnabled
          ? "ON"
          : "OFF",
        195,
        y + 13
      );

    } else if (
      i ==
      SETTING_BRIGHTNESS
    ) {

      int brtPct =
        map(
          brightness,
          20,
          255,
          1,
          100
        );

      M5Dial.Display.setTextColor(
        editing
          ? C_YELLOW
          : C_LGRAY
      );

      M5Dial.Display.drawString(
        String(brtPct) + "%",
        200,
        y + 13
      );

    } else if (
      i ==
      SETTING_WIFI_SETUP
    ) {

      M5Dial.Display.setTextColor(
        C_CYAN
      );

      M5Dial.Display.drawString(
        "Open >",
        200,
        y + 13
      );

    } else if (
      i ==
      SETTING_WIFI_FORGET
    ) {

      M5Dial.Display.setTextColor(
        C_ORANGE
      );

      M5Dial.Display.drawString(
        "Clear >",
        200,
        y + 13
      );

    } else {

      M5Dial.Display.setTextColor(
        C_LGRAY
      );

      M5Dial.Display.drawString(
        "Edit >",
        200,
        y + 13
      );
    }
  }

  if (settingScrollMode) {

    drawKR(
      "Hold: back",
      CX,
      205,
      C_YELLOW
    );

  } else {

    drawKR(
      "Button: select",
      CX,
      205,
      C_LGRAY
    );
  }

  drawPageDots(
    currentSlot
  );
}


// ============================================================
// Current page
// ============================================================

void drawCurrentPage() {

  int pid =
    pageOrder[
      currentSlot
    ];

  switch (pid) {

    case PID_CLOCK:
      drawClockPage();
      break;

    case PID_NEXT:
      drawNextRacePage(true);
      break;

    case PID_SCHEDULE:
      drawSchedulePage();
      break;

    case PID_STAND:
      drawStandingsPage();
      break;

    case PID_LIVE:
      drawLivePage();
      break;

    case PID_SETTINGS:
      drawSettingsPage();
      break;
  }
}


// ============================================================
// Splash
// ============================================================

void showSplash() {

  clearScreen();

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.setTextColor(
    C_RED
  );

  M5Dial.Display.setTextSize(
    2
  );

  M5Dial.Display.drawString(
    "F1 LIVE",
    CX,
    108
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.setTextSize(
    1
  );

  M5Dial.Display.drawString(
    "Race Tracker",
    CX,
    136
  );

  M5Dial.Display.drawCircle(
    CX,
    CY,
    114,
    C_RED
  );

  M5Dial.Display.drawCircle(
    CX,
    CY,
    110,
    0x4000
  );

  delay(1500);
}


// ============================================================
// Fast NTP synchronization
// ============================================================

bool syncTimeFast() {

  ntpSync = false;

  // 여러 서버를 지정
  configTime(
    GMT_OFFSET,
    DST_OFFSET,
    NTP1,
    NTP2,
    NTP3
  );

  // 최대 약 8초
  // 일반적인 환경에서는 1~3초 정도에 완료되는 경우가 많음
  for (
    int i = 0;
    i < 16;
    i++
  ) {

    if (timeIsValid()) {

      ntpSync = true;

      return true;
    }

    delay(500);
  }

  ntpSync =
    timeIsValid();

  return ntpSync;
}


// ============================================================
// WiFi connect
// ============================================================

bool connectStoredWiFi() {

  if (
    wifiSSID.length() == 0
  ) {

    wifiOk = false;

    return false;
  }

  M5Dial.Display.fillScreen(
    C_BG
  );

  clearScreen();

  M5Dial.Display.fillRoundRect(
    30,
    4,
    180,
    34,
    8,
    C_BLUE
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.setTextSize(
    1
  );

  M5Dial.Display.drawString(
    "Connecting WiFi",
    CX,
    21
  );

  String ssidDisp =
    wifiSSID;

  if (
    ssidDisp.length() > 16
  ) {

    ssidDisp =
      ssidDisp.substring(
        0,
        14
      ) + "..";
  }

  M5Dial.Display.setTextColor(
    C_CYAN
  );

  M5Dial.Display.drawString(
    ssidDisp,
    CX,
    80
  );

  WiFi.mode(
    WIFI_STA
  );

  WiFi.setAutoReconnect(
    true
  );

  WiFi.begin(
    wifiSSID.c_str(),
    wifiPassword.c_str()
  );

  int tries = 0;

  while (
    WiFi.status() != WL_CONNECTED &&
    tries < 20
  ) {

    delay(400);

    tries++;

    M5Dial.Display.fillRect(
      46,
      106,
      148,
      12,
      C_BG
    );

    int bw =
      (
        tries * 148
      ) / 20;

    M5Dial.Display.fillRoundRect(
      46,
      106,
      bw,
      12,
      4,
      C_BLUE
    );

    M5Dial.Display.drawRoundRect(
      46,
      106,
      148,
      12,
      4,
      C_GRAY
    );
  }

  if (
    WiFi.status() != WL_CONNECTED
  ) {

    wifiOk = false;

    beep(
      400,
      150
    );

    return false;
  }

  wifiOk = true;

  M5Dial.Display.fillRect(
    45,
    135,
    150,
    22,
    C_BG
  );

  drawKR(
    "Connected!",
    CX,
    145,
    C_GREEN
  );

  beep(
    1200,
    80
  );

  delay(500);

  // ----------------------------------------------------------
  // NTP
  // ----------------------------------------------------------

  M5Dial.Display.fillScreen(
    C_BG
  );

  clearScreen();

  drawKR(
    "Syncing time...",
    CX,
    CY,
    C_CYAN
  );

  syncTimeFast();

  // ----------------------------------------------------------
  // API
  // ----------------------------------------------------------

  M5Dial.Display.fillScreen(
    C_BG
  );

  clearScreen();

  drawKR(
    "Loading F1 data...",
    CX,
    CY,
    C_YELLOW
  );

  refreshData();

  lastApiUpdate =
    millis();

  lastLiveUpdate =
    millis();

  return true;
}


bool connectWifiOnce() {

  if (
    !loadWiFiCredentials()
  ) {

    wifiOk = false;

    return false;
  }

  return connectStoredWiFi();
}


// ============================================================
// WiFi setup portal
// ============================================================

String htmlEscape(
  const String& input
) {

  String s = input;

  s.replace(
    "&",
    "&amp;"
  );

  s.replace(
    "<",
    "&lt;"
  );

  s.replace(
    ">",
    "&gt;"
  );

  s.replace(
    "\"",
    "&quot;"
  );

  return s;
}


void handleRoot() {

  int n =
    WiFi.scanNetworks(
      false,
      true
    );

  String html;

  html.reserve(
    8000
  );

  html +=
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta charset='UTF-8'>"
    "<title>F1 Dial WiFi Setup</title>"
    "<style>"
    "body{font-family:Arial;background:#101010;color:white;padding:20px;}"
    "h1{color:#e10600;}"
    "select,input{width:100%;padding:13px;margin:8px 0;"
    "box-sizing:border-box;font-size:16px;border-radius:8px;"
    "border:1px solid #555;background:#222;color:white;}"
    "button{width:100%;padding:14px;margin-top:12px;"
    "font-size:17px;border:0;border-radius:8px;"
    "background:#e10600;color:white;}"
    ".box{max-width:500px;margin:auto;}"
    ".info{background:#222;padding:12px;border-radius:8px;"
    "margin:12px 0;color:#bbb;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class='box'>"
    "<h1>F1 Dial</h1>"
    "<h2>Wi-Fi Setup</h2>"
    "<div class='info'>"
    "Select your Wi-Fi network and enter the password."
    "</div>"
    "<form action='/save' method='POST'>"
    "<label>Wi-Fi Network</label>"
    "<select name='ssid'>";

  if (n <= 0) {

    html +=
      "<option value=''>"
      "No networks found"
      "</option>";

  } else {

    for (
      int i = 0;
      i < n;
      i++
    ) {

      String ssid =
        WiFi.SSID(i);

      if (ssid.length() == 0)
        continue;

      html +=
        "<option value='" +
        htmlEscape(ssid) +
        "'>";

      html +=
        htmlEscape(ssid);

      html +=
        " (" +
        String(
          WiFi.RSSI(i)
        ) +
        " dBm)";

      html +=
        "</option>";
    }
  }

  html +=
    "</select>"
    "<label>Password</label>"
    "<input type='password' "
    "name='password' "
    "placeholder='Wi-Fi password'>"
    "<button type='submit'>Save & Connect</button>"
    "</form>"
    "</div>"
    "</body>"
    "</html>";

  WiFi.scanDelete();

  webServer.send(
    200,
    "text/html; charset=utf-8",
    html
  );
}


void handleScan() {

  handleRoot();
}


void handleSaveWifi() {

  String ssid =
    webServer.arg(
      "ssid"
    );

  String password =
    webServer.arg(
      "password"
    );

  ssid.trim();

  if (
    ssid.length() == 0
  ) {

    webServer.send(
      400,
      "text/html",
      "<h2>SSID is empty.</h2>"
      "<a href='/'>Back</a>"
    );

    return;
  }

  saveWiFiCredentials(
    ssid,
    password
  );

  String html =
    "<!DOCTYPE html>"
    "<html>"
    "<head>"
    "<meta name='viewport' "
    "content='width=device-width,initial-scale=1'>"
    "<style>"
    "body{font-family:Arial;background:#101010;"
    "color:white;text-align:center;padding:30px;}"
    "h1{color:#00ff88;}"
    "</style>"
    "</head>"
    "<body>"
    "<h1>Saved!</h1>"
    "<p>F1 Dial will restart its Wi-Fi connection.</p>"
    "<p>You can now close this page.</p>"
    "</body>"
    "</html>";

  webServer.send(
    200,
    "text/html",
    html
  );

  delay(800);

  stopWiFiSetupPortal();

  WiFi.mode(
    WIFI_STA
  );

  delay(200);

  if (connectStoredWiFi()) {

    setupPortalRunning =
      false;

    drawCurrentPage();

  } else {

    setupPortalRunning =
      false;

    startWiFiSetupPortal();
  }
}


void handleNotFound() {

  // Captive portal 대응
  webServer.sendHeader(
    "Location",
    "http://192.168.4.1/",
    true
  );

  webServer.send(
    302,
    "text/plain",
    ""
  );
}


// ============================================================
// Start WiFi AP
// ============================================================

void startWiFiSetupPortal() {

  if (setupPortalRunning)
    return;

  setupPortalRunning =
    true;

  WiFi.disconnect(
    true,
    true
  );

  delay(300);

  WiFi.mode(
    WIFI_AP
  );

  WiFi.softAP(
    AP_SSID
  );

  IPAddress apIP =
    WiFi.softAPIP();

  dnsServer.start(
    DNS_PORT,
    "*",
    apIP
  );

  webServer.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  webServer.on(
    "/scan",
    HTTP_GET,
    handleScan
  );

  webServer.on(
    "/save",
    HTTP_POST,
    handleSaveWifi
  );

  webServer.onNotFound(
    handleNotFound
  );

  webServer.begin();

  M5Dial.Display.fillScreen(
    C_BG
  );

  clearScreen();

  M5Dial.Display.fillRoundRect(
    24,
    25,
    192,
    34,
    8,
    C_RED
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.setTextSize(
    1
  );

  M5Dial.Display.drawString(
    "WiFi Setup",
    CX,
    42
  );

  M5Dial.Display.setTextColor(
    C_CYAN
  );

  M5Dial.Display.drawString(
    AP_SSID,
    CX,
    82
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.drawString(
    "Connect with phone",
    CX,
    108
  );

  M5Dial.Display.setTextColor(
    C_YELLOW
  );

  M5Dial.Display.drawString(
    "192.168.4.1",
    CX,
    134
  );

  M5Dial.Display.setTextColor(
    C_LGRAY
  );

  M5Dial.Display.drawString(
    "Select WiFi + password",
    CX,
    164
  );

  M5Dial.Display.setTextColor(
    C_GRAY
  );

  M5Dial.Display.drawString(
    "Button: retry",
    CX,
    194
  );
}


// ============================================================
// Stop WiFi portal
// ============================================================

void stopWiFiSetupPortal() {

  if (!setupPortalRunning)
    return;

  webServer.stop();

  dnsServer.stop();

  WiFi.softAPdisconnect(
    true
  );

  setupPortalRunning =
    false;
}


// ============================================================
// WiFi main connect
// ============================================================

void connectWifi() {

  loadWiFiCredentials();

  // ----------------------------------------------------------
  // 저장된 Wi-Fi가 없으면 바로 AP 설정 모드
  // ----------------------------------------------------------

  if (
    !wifiCredentialsSaved
  ) {

    startWiFiSetupPortal();

    while (
      setupPortalRunning
    ) {

      M5Dial.update();

      dnsServer.processNextRequest();

      webServer.handleClient();

      delay(5);
    }

    return;
  }

  // ----------------------------------------------------------
  // 저장된 Wi-Fi로 연결
  // ----------------------------------------------------------

  if (
    connectStoredWiFi()
  ) {
    return;
  }

  // ----------------------------------------------------------
  // 연결 실패
  // ----------------------------------------------------------

  M5Dial.Display.fillScreen(
    C_BG
  );

  clearScreen();

  M5Dial.Display.fillRoundRect(
    30,
    4,
    180,
    34,
    8,
    C_RED
  );

  M5Dial.Display.setFont(
    FONT_ASCII
  );

  M5Dial.Display.setTextColor(
    C_WHITE
  );

  M5Dial.Display.setTextDatum(
    middle_center
  );

  M5Dial.Display.drawString(
    "WiFi Failed",
    CX,
    21
  );

  drawKR(
    "Press button",
    CX,
    100,
    C_YELLOW
  );

  drawKR(
    "for WiFi Setup",
    CX,
    125,
    C_LGRAY
  );

  drawKR(
    "or retry",
    CX,
    150,
    C_GRAY
  );

  while (true) {

    M5Dial.update();

    if (
      M5Dial.BtnA.wasPressed()
    ) {

      beep(
        900,
        50
      );

      startWiFiSetupPortal();

      while (
        setupPortalRunning
      ) {

        M5Dial.update();

        dnsServer.processNextRequest();

        webServer.handleClient();

        delay(5);
      }

      // 설정 완료 후 다시 연결
      if (
        wifiCredentialsSaved
      ) {

        if (
          connectStoredWiFi()
        ) {
          return;
        }
      }

      // 계속 실패하면 설정 AP 재시작
      startWiFiSetupPortal();
    }

    delay(20);
  }
}


// ============================================================
// Touch
// ============================================================

bool handleTouch(
  int pid
) {

  const auto touch =
    M5Dial.Touch.getDetail();

  if (
    touch.wasPressed()
  ) {

    touchWasDown = true;

    touchDownAt =
      millis();

    touchX =
      touch.x;

    touchY =
      touch.y;

    return false;
  }

  if (
    touch.isPressed()
  ) {

    touchX =
      touch.x;

    touchY =
      touch.y;

    return false;
  }

  if (
    !touchWasDown ||
    !(
      touch.wasClicked() ||
      touch.wasReleased()
    )
  ) {
    return false;
  }

  touchWasDown = false;

  const bool longPress =
    millis() -
    touchDownAt >=
    700;


  // ----------------------------------------------------------
  // Clock
  // ----------------------------------------------------------

  if (
    pid == PID_CLOCK &&
    !longPress
  ) {

    use12h =
      !use12h;

    saveSettings();

    beep(
      1000,
      30
    );

    drawClockPage();

    return true;
  }


  // ----------------------------------------------------------
  // Standings
  // ----------------------------------------------------------

  if (
    pid == PID_STAND &&
    !longPress
  ) {

    const int maxPage =
      max(
        0,
        (standCount - 1) / 8
      );

    standPage =
      (
        standPage >= maxPage
      )
        ? 0
        : standPage + 1;

    beep(
      1000,
      20
    );

    drawStandingsPage();

    return true;
  }


  // ----------------------------------------------------------
  // Schedule
  // ----------------------------------------------------------

  if (
    pid == PID_SCHEDULE &&
    !longPress
  ) {

    if (
      upcomingCount > 4
    ) {

      schedScrollOff += 4;

      if (
        schedScrollOff >=
        upcomingCount
      ) {
        schedScrollOff = 0;
      }

      beep(
        1000,
        20
      );

      drawSchedulePage();

      return true;
    }
  }


  // ----------------------------------------------------------
  // Settings touch
  // ----------------------------------------------------------

  if (
    pid == PID_SETTINGS &&
    !longPress &&
    touchX >= 18 &&
    touchX <= 222
  ) {

    int row =
      (
        touchY - 62
      ) / 27;

    if (
      row >= 0 &&
      row < SETTING_COUNT &&
      touchY >=
        62 + row * 27 &&
      touchY <
        86 + row * 27
    ) {

      settingSelIdx =
        row;

      switch (row) {

        case SETTING_BUZZER:

          buzzerEnabled =
            !buzzerEnabled;

          saveSettings();

          beep(
            buzzerEnabled
              ? 1300
              : 500,
            60
          );

          break;


        case SETTING_BRIGHTNESS:

          settingScrollMode =
            true;

          settingEditing =
            true;

          beep(
            900,
            30
          );

          break;


        case SETTING_WIFI_SETUP:

          beep(
            900,
            50
          );

          startWiFiSetupPortal();

          return true;


        case SETTING_WIFI_FORGET:

          clearWiFiCredentials();

          beep(
            600,
            120
          );

          startWiFiSetupPortal();

          return true;


        case SETTING_PAGE_ORDER:

          editingPageOrder =
            true;

          editingSlot = 0;

          beep(
            900,
            30
          );

          drawPageOrderEditor();

          return true;
      }

      drawSettingsPage();

      return true;
    }
  }

  return false;
}


// ============================================================
// SETUP
// ============================================================

void setup() {

  auto cfg =
    M5.config();

  M5Dial.begin(
    cfg,
    true,
    false
  );

  M5Dial.Display.setRotation(
    0
  );

  M5Dial.Display.setTextFont(
    FONT_ASCII
  );

  loadSettings();

  loadPageOrder();

  loadWiFiCredentials();

  showSplash();

  connectWifi();

  M5Dial.Encoder.readAndReset();

  lastEncPos = 0;

  lastClockDraw =
    millis();

  lastNextDraw =
    millis();

  drawCurrentPage();
}


// ============================================================
// LOOP
// ============================================================

void loop() {

  M5Dial.update();

  // ----------------------------------------------------------
  // WiFi setup portal
  // ----------------------------------------------------------

  if (
    setupPortalRunning
  ) {

    dnsServer.processNextRequest();

    webServer.handleClient();

    delay(5);

    return;
  }


  // ----------------------------------------------------------
  // Encoder
  // ----------------------------------------------------------

  long encPos =
    M5Dial.Encoder.read();

  long delta =
    encPos -
    lastEncPos;

  bool stepped =
    abs(delta) >= 4;

  int steps =
    stepped
      ? (int)(delta / 4)
      : 0;

  int pid =
    pageOrder[
      currentSlot
    ];


  // ----------------------------------------------------------
  // Touch
  // ----------------------------------------------------------

  if (
    handleTouch(pid)
  ) {

    delay(20);

    return;
  }


  // ----------------------------------------------------------
  // Page Order Editor
  // ----------------------------------------------------------

  if (
    editingPageOrder
  ) {

    if (stepped) {

      lastEncPos =
        encPos;

      int newSlot =
        editingSlot +
        (
          steps > 0
            ? 1
            : -1
        );

      newSlot =
        constrain(
          newSlot,
          0,
          PAGE_COUNT - 1
        );

      if (
        newSlot !=
        editingSlot
      ) {

        int tmp =
          pageOrder[
            editingSlot
          ];

        pageOrder[
          editingSlot
        ] =
          pageOrder[
            newSlot
          ];

        pageOrder[
          newSlot
        ] =
          tmp;

        editingSlot =
          newSlot;

        beep(
          1000,
          15
        );
      }

      drawPageOrderEditor();
    }

    if (
      M5Dial.BtnA.wasPressed()
    ) {

      savePageOrder();

      editingPageOrder =
        false;

      beep(
        1200,
        50
      );

      drawSettingsPage();
    }

    delay(20);

    return;
  }


  // ==========================================================
  // SETTINGS PAGE
  // ==========================================================

  if (
    pid == PID_SETTINGS
  ) {

    // --------------------------------------------------------
    // Brightness editing
    // --------------------------------------------------------

    if (
      settingEditing
    ) {

      if (stepped) {

        lastEncPos =
          encPos;

        brightness =
          constrain(
            brightness +
            steps * 12,
            20,
            255
          );

        M5Dial.Display.setBrightness(
          brightness
        );

        drawSettingsPage();
      }

      if (
        M5Dial.BtnA.wasPressed()
      ) {

        saveSettings();

        settingEditing =
          false;

        beep(
          1200,
          40
        );

        drawSettingsPage();
      }

      delay(20);

      return;
    }


    // --------------------------------------------------------
    // Settings selection mode
    // --------------------------------------------------------

    if (
      settingScrollMode
    ) {

      if (stepped) {

        lastEncPos =
          encPos;

        settingSelIdx =
          (
            settingSelIdx +
            steps +
            SETTING_COUNT
          ) %
          SETTING_COUNT;

        beep(
          1000,
          15
        );

        drawSettingsPage();
      }


      if (
        M5Dial.BtnA.wasPressed()
      ) {

        switch (
          settingSelIdx
        ) {

          case SETTING_BUZZER:

            buzzerEnabled =
              !buzzerEnabled;

            saveSettings();

            beep(
              buzzerEnabled
                ? 1300
                : 500,
              60
            );

            drawSettingsPage();

            break;


          case SETTING_BRIGHTNESS:

            settingEditing =
              true;

            beep(
              900,
              30
            );

            drawSettingsPage();

            break;


          case SETTING_WIFI_SETUP:

            startWiFiSetupPortal();

            break;


          case SETTING_WIFI_FORGET:

            clearWiFiCredentials();

            beep(
              600,
              120
            );

            startWiFiSetupPortal();

            break;


          case SETTING_PAGE_ORDER:

            editingPageOrder =
              true;

            editingSlot = 0;

            beep(
              900,
              30
            );

            drawPageOrderEditor();

            break;
        }
      }


      if (
        M5Dial.BtnA.pressedFor(
          700
        )
      ) {

        while (
          M5Dial.BtnA.isPressed()
        ) {

          M5Dial.update();

          delay(10);
        }

        settingScrollMode =
          false;

        settingEditing =
          false;

        beep(
          800,
          40
        );

        drawSettingsPage();
      }

      delay(20);

      return;
    }


    // --------------------------------------------------------
    // Normal settings page
    // --------------------------------------------------------

    if (stepped) {

      lastEncPos =
        encPos;

      currentSlot =
        (
          currentSlot +
          steps +
          PAGE_COUNT
        ) %
        PAGE_COUNT;

      beep(
        1200,
        30
      );

      drawCurrentPage();

      delay(20);

      return;
    }


    if (
      M5Dial.BtnA.wasPressed()
    ) {

      settingScrollMode =
        true;

      beep(
        900,
        30
      );

      drawSettingsPage();
    }

    delay(20);

    return;
  }


  // ==========================================================
  // STANDINGS PAGE
  // ==========================================================

  if (
    pid == PID_STAND
  ) {

    if (
      standScrollMode
    ) {

      if (stepped) {

        lastEncPos =
          encPos;

        int maxPage =
          max(
            0,
            (standCount - 1) / 8
          );

        standPage =
          constrain(
            standPage +
            (
              steps > 0
                ? 1
                : -1
            ),
            0,
            maxPage
          );

        beep(
          1000,
          15
        );

        drawStandingsPage();
      }

      if (
        M5Dial.BtnA.pressedFor(
          700
        )
      ) {

        while (
          M5Dial.BtnA.isPressed()
        ) {

          M5Dial.update();

          delay(10);
        }

        standScrollMode =
          false;

        beep(
          800,
          40
        );

        drawStandingsPage();
      }

      delay(20);

      return;
    }


    if (stepped) {

      lastEncPos =
        encPos;

      currentSlot =
        (
          currentSlot +
          steps +
          PAGE_COUNT
        ) %
        PAGE_COUNT;

      beep(
        1200,
        30
      );

      drawCurrentPage();

      delay(20);

      return;
    }


    if (
      M5Dial.BtnA.wasPressed()
    ) {

      standScrollMode =
        true;

      beep(
        900,
        30
      );

      drawStandingsPage();
    }

    delay(20);

    return;
  }


  // ==========================================================
  // SCHEDULE PAGE
  // ==========================================================

  if (
    pid == PID_SCHEDULE
  ) {

    if (
      schedScrollMode
    ) {

      if (stepped) {

        lastEncPos =
          encPos;

        int dir =
          steps > 0
            ? 1
            : -1;

        int newOff =
          schedScrollOff +
          dir;

        if (
          newOff >= 0 &&
          newOff <
            upcomingCount
        ) {

          schedScrollOff =
            newOff;

          beep(
            1000,
            15
          );

          drawSchedulePage();
        }
      }


      if (
        M5Dial.BtnA.pressedFor(
          700
        )
      ) {

        while (
          M5Dial.BtnA.isPressed()
        ) {

          M5Dial.update();

          delay(10);
        }

        schedScrollMode =
          false;

        beep(
          800,
          40
        );

        drawSchedulePage();
      }

      delay(20);

      return;
    }


    if (stepped) {

      lastEncPos =
        encPos;

      currentSlot =
        (
          currentSlot +
          steps +
          PAGE_COUNT
        ) %
        PAGE_COUNT;

      beep(
        1200,
        30
      );

      drawCurrentPage();

      delay(20);

      return;
    }


    if (
      M5Dial.BtnA.wasPressed()
    ) {

      schedScrollMode =
        true;

      beep(
        900,
        30
      );

      drawSchedulePage();
    }

    delay(20);

    return;
  }


  // ==========================================================
  // NORMAL PAGE NAVIGATION
  // ==========================================================

  if (stepped) {

    lastEncPos =
      encPos;

    currentSlot =
      (
        currentSlot +
        steps +
        PAGE_COUNT
      ) %
      PAGE_COUNT;

    settingEditing =
      false;

    beep(
      1200,
      30
    );

    drawCurrentPage();
  }


  // ==========================================================
  // Manual refresh
  // ==========================================================

  if (
    M5Dial.BtnA.wasPressed()
  ) {

    beep(
      800,
      50
    );

    M5Dial.Display.fillRect(
      55,
      105,
      130,
      30,
      C_BG
    );

    M5Dial.Display.setFont(
      FONT_ASCII
    );

    M5Dial.Display.setTextDatum(
      middle_center
    );

    M5Dial.Display.setTextSize(
      1
    );

    M5Dial.Display.setTextColor(
      C_CYAN
    );

    M5Dial.Display.drawString(
      "Refreshing...",
      CX,
      CY
    );

    refreshData();

    lastApiUpdate =
      millis();

    lastLiveUpdate =
      millis();

    drawCurrentPage();
  }


  // ==========================================================
  // CLOCK
  //
  // 중요:
  // 기존 drawCurrentPage()를 1초마다 호출하지 않는다.
  // 시간 숫자 영역만 갱신한다.
  // ==========================================================

  if (
    pid == PID_CLOCK &&
    millis() -
      lastClockDraw >=
      CLOCK_INTERVAL
  ) {

    lastClockDraw =
      millis();

    drawClockTimeOnly();
  }


  // ==========================================================
  // NEXT RACE
  //
  // 전체 화면은 처음 진입할 때만 그리고
  // 1초마다 countdown 영역만 갱신한다.
  // ==========================================================

  static int lastFetchDay =
    -2;

  if (
    ntpSync &&
    timeIsValid() &&
    lastFetchDay == -2
  ) {

    time_t now =
      time(nullptr);

    struct tm ti;

    localtime_r(
      &now,
      &ti
    );

    lastFetchDay =
      ti.tm_yday;
  }


  if (
    pid == PID_NEXT &&
    millis() -
      lastNextDraw >=
      1000UL
  ) {

    lastNextDraw =
      millis();

    bool fullRedraw =
      false;

    if (
      ntpSync &&
      timeIsValid()
    ) {

      time_t now =
        time(nullptr);

      struct tm ti;

      localtime_r(
        &now,
        &ti
      );

      int todayDay =
        ti.tm_yday;

      if (
        ti.tm_hour == 0 &&
        ti.tm_min == 0 &&
        ti.tm_sec < 2 &&
        lastFetchDay !=
          todayDay
      ) {

        lastFetchDay =
          todayDay;

        fetchSchedule();

        fetchStandings();

        fullRedraw =
          true;
      }
    }

    drawNextRacePage(
      fullRedraw
    );
  }


  // ==========================================================
  // LIVE
  // ==========================================================

  if (
    isLiveSession &&
    wifiOk &&
    millis() -
      lastLiveUpdate >=
      LIVE_INTERVAL
  ) {

    fetchLivePositions();

    lastLiveUpdate =
      millis();

    if (
      pid == PID_LIVE
    ) {

      drawLivePage();
    }
  }


  // ==========================================================
  // API refresh
  // ==========================================================

  if (
    wifiOk &&
    millis() -
      lastApiUpdate >=
      API_INTERVAL
  ) {

    refreshData();

    lastApiUpdate =
      millis();

    drawCurrentPage();
  }


  delay(20);
}
