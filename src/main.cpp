#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <mbedtls/sha256.h>
#include <time.h>

#include "secrets.h"

TFT_eSPI tft = TFT_eSPI();

static const unsigned long FETCH_INTERVAL_MS = 60000;
static const int MAX_HISTORY_POINTS = 64;
static const char* UI_THEME_VERSION = "standalone-libre-v2";
static const byte DNS_PORT = 53;
static const char* LIBRE_LINK_UP_VERSION = "4.16.0";
static const char* LIBRE_LINK_UP_PRODUCT = "llu.ios";
static const char* LIBRE_USER_AGENT =
    "Mozilla/5.0 (iPhone; CPU OS 17_4_1 like Mac OS X) "
    "AppleWebKit/605.1.15 (KHTML, like Gecko) "
    "Version/17.4.1 Mobile/15E148 Safari/604.1";

struct GlucosePoint {
    int value;
    String timestamp;
};

struct LibreAuthTicket {
    String token;
    String accountId;
    unsigned long expiresAtMillis = 0;
};

struct LibrePatientSnapshot {
    String patientId;
    String firstName;
    String lastName;
    int glucose = 0;
    int trend = 3;
    int sensorDaysLeft = 0;
    String timestampRaw = "--";
    bool valid = false;
};

struct RegionHost {
    const char* region;
    const char* host;
};

enum UiThemeMode {
    THEME_DARK = 0,
    THEME_LIGHT = 1,
};

static const RegionHost REGION_HOSTS[] = {
    {"AE", "api-ae.libreview.io"},
    {"AP", "api-ap.libreview.io"},
    {"AU", "api-au.libreview.io"},
    {"CA", "api-ca.libreview.io"},
    {"DE", "api-de.libreview.io"},
    {"EU", "api-eu.libreview.io"},
    {"EU2", "api-eu2.libreview.io"},
    {"FR", "api-fr.libreview.io"},
    {"JP", "api-jp.libreview.io"},
    {"LA", "api-la.libreview.io"},
    {"RU", "api.libreview.ru"},
    {"US", "api-us.libreview.io"},
};

#ifndef CONFIG_AP_SSID
#define CONFIG_AP_SSID "Schugaa-Setup"
#endif

#ifndef CONFIG_AP_PASSWORD
#define CONFIG_AP_PASSWORD "schugaa123"
#endif

GlucosePoint history[MAX_HISTORY_POINTS];
int historyCount = 0;
int currentGlucose = 0;
int currentTrend = 3;
int sensorDaysLeft = 0;
String currentTimestamp = "--";
String connectionStatus = "BOOT";
String lastUpdatedStr = "--";
String libreEmail = LIBRE_EMAIL;
String librePassword = LIBRE_PASSWORD;
String libreRegion = LIBRE_REGION;
String selectedPatientId = LIBRE_PATIENT_ID;
String configMessage = "";
bool configMessageSuccess = false;
LibreAuthTicket authTicket;
unsigned long lastFetchTime = 0;
UiThemeMode currentThemeMode = THEME_DARK;
bool themeOverrideActive = false;
bool lastTouchPressed = false;
bool clockSynced = false;
int lastAutoThemePeriod = -1;
unsigned long lastTouchToggleMs = 0;

uint16_t COLOR_BG;
uint16_t COLOR_PANEL;
uint16_t COLOR_PANEL_BORDER;
uint16_t COLOR_GRID;
uint16_t COLOR_LIMIT;
uint16_t COLOR_SAFE_FILL;
uint16_t COLOR_STATUS_BAR;
uint16_t COLOR_STATUS_TEXT;
uint16_t COLOR_TEXT_MUTED;
uint16_t COLOR_GRAPH_LINE;
uint16_t COLOR_RED;
uint16_t COLOR_YELLOW;
uint16_t COLOR_GREEN;
uint16_t COLOR_ORANGE;

WebServer configServer(80);
DNSServer dnsServer;
Preferences prefs;
bool configPortalStarted = false;

void connectWiFi();
bool fetchGlucoseData();
void startConfigPortal();
void handleConfigPortal();
void setupConfigRoutes();
void loadConfigFromPreferences();
void saveConfigToPreferences();
void sendConfigPage(const String& message = "", bool success = false);
void handleConfigRoot();
void handleConfigSave();
void handleConfigSecretsDownload();
String buildRegionOptions(const String& selectedRegion);
String htmlEscape(const String& input);
bool isKnownRegion(const String& region);
bool ensureLibreAuthentication();
bool loginToLibre(bool allowRedirect = true);
bool fetchLibreConnections(LibrePatientSnapshot& snapshot);
bool fetchLibreGraph(const String& patientId);
void renderUI();
void drawChart();
void drawStatusBar();
void drawStartupScreen(const char* title, const char* subtitle);
bool syncClock();
void applyTheme(UiThemeMode themeMode);
void updateThemeMode(bool forceRender = false);
void handleTouchToggle();
bool isTouchPressedRaw();
void drawBoldText(const String& text, int x, int y, int font, uint16_t fg, uint16_t bg, uint8_t spread = 1);
bool isAutoLightThemeNow();
void clearAuthTicket();
bool hasValidAuthTicket();
String formatTimeLabel(const String& raw);
String sha256Hex(const String& value);
String libreBaseUrl();
int glucoseValueFromJson(JsonVariantConst variant);
String timestampFromJson(JsonVariantConst variant);
time_t parseLibreTimeValue(JsonVariantConst value);
String readHttpResponse(HTTPClient& http);
void addLibreHeaders(HTTPClient& http);
void addAuthHeaders(HTTPClient& http);
const char* libreHostForRegion(const String& region);
int sensorDaysLeftFromJson(JsonVariantConst variant);
uint16_t glucoseColor(int value);
const char* trendArrow(int trend);
int mapGraphX(int index, int total, int left, int width);
int mapGraphY(int value, int minValue, int maxValue, int top, int height);

int mapGraphX(int index, int total, int left, int width) {
    if (total <= 1) {
        return left;
    }
    const float step = static_cast<float>(width) / static_cast<float>(total - 1);
    return left + static_cast<int>(index * step);
}

int mapGraphY(int value, int minValue, int maxValue, int top, int height) {
    const int constrainedValue = constrain(value, minValue, maxValue);
    const float normalized =
        static_cast<float>(constrainedValue - minValue) / static_cast<float>(maxValue - minValue);
    return top + height - static_cast<int>(normalized * height);
}

String formatTimeLabel(const String& raw) {
    if (raw.length() == 5 && raw.charAt(2) == ':') {
        return raw;
    }

    if (raw.indexOf('T') > 0 && raw.indexOf(':') > 0) {
        int timeStart = raw.indexOf('T') + 1;
        if (timeStart > 0 && timeStart + 5 <= raw.length()) {
            return raw.substring(timeStart, timeStart + 5);
        }
    }

    const int firstSpace = raw.indexOf(' ');
    const int secondSpace = raw.indexOf(' ', firstSpace + 1);
    if (firstSpace <= 0 || secondSpace <= firstSpace) {
        return raw;
    }

    const String timePart = raw.substring(firstSpace + 1, secondSpace);
    const String ampm = raw.substring(secondSpace + 1);
    const int firstColon = timePart.indexOf(':');
    const int lastColon = timePart.lastIndexOf(':');

    if (firstColon <= 0 || lastColon <= firstColon) {
        return raw;
    }

    int hour = timePart.substring(0, firstColon).toInt();
    const String mins = timePart.substring(firstColon + 1, lastColon);
    const bool isPM = (ampm == "PM");
    const bool isAM = (ampm == "AM");

    if (isPM && hour != 12) {
        hour += 12;
    }
    if (isAM && hour == 12) {
        hour = 0;
    }

    return (hour < 10 ? "0" : "") + String(hour) + ":" + mins;
}

void drawStartupScreen(const char* title, const char* subtitle) {
    tft.fillScreen(COLOR_BG);
    tft.setTextColor(currentThemeMode == THEME_LIGHT ? COLOR_STATUS_TEXT : TFT_WHITE, COLOR_BG);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(title, 160, 94, 4);
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_BG);
    tft.drawString(subtitle, 160, 128, 2);
}

void drawBoldText(const String& text, int x, int y, int font, uint16_t fg, uint16_t bg, uint8_t spread) {
    tft.setTextColor(fg, bg);
    for (int dx = -spread; dx <= 0; dx++) {
        for (int dy = -spread; dy <= 0; dy++) {
            tft.drawString(text, x + dx, y + dy, font);
        }
    }
}

String htmlEscape(const String& input) {
    String out = input;
    out.replace("&", "&amp;");
    out.replace("<", "&lt;");
    out.replace(">", "&gt;");
    out.replace("\"", "&quot;");
    out.replace("'", "&#39;");
    return out;
}

bool isKnownRegion(const String& region) {
    for (const auto& entry : REGION_HOSTS) {
        if (region.equalsIgnoreCase(entry.region)) {
            return true;
        }
    }
    return false;
}

String buildRegionOptions(const String& selectedRegion) {
    String options = "";
    for (const auto& entry : REGION_HOSTS) {
        options += "<option value='";
        options += entry.region;
        options += "'";
        if (selectedRegion.equalsIgnoreCase(entry.region)) {
            options += " selected";
        }
        options += ">";
        options += entry.region;
        options += "</option>";
    }
    return options;
}

void loadConfigFromPreferences() {
    if (!prefs.begin("schugaa", true)) {
        Serial.println("Preferences read open failed, using compiled defaults.");
        return;
    }

    libreEmail = prefs.getString("libre_email", libreEmail);
    librePassword = prefs.getString("libre_pass", librePassword);
    libreRegion = prefs.getString("libre_region", libreRegion);
    selectedPatientId = prefs.getString("patient_id", selectedPatientId);
    prefs.end();

    if (!isKnownRegion(libreRegion)) {
        libreRegion = LIBRE_REGION;
    }

    Serial.printf("Loaded config. Region=%s, Email set=%s\n",
                  libreRegion.c_str(), libreEmail.length() > 0 ? "yes" : "no");
}

void saveConfigToPreferences() {
    if (!prefs.begin("schugaa", false)) {
        Serial.println("Preferences write open failed.");
        return;
    }
    prefs.putString("libre_email", libreEmail);
    prefs.putString("libre_pass", librePassword);
    prefs.putString("libre_region", libreRegion);
    prefs.putString("patient_id", selectedPatientId);
    prefs.end();
}

void sendConfigPage(const String& message, bool success) {
    String banner = "";
    if (message.length() > 0) {
        banner += "<div class='status ";
        banner += success ? "ok" : "err";
        banner += "'>";
        banner += htmlEscape(message);
        banner += "</div>";
    }

    String html;
    html.reserve(8500);
    html += "<!doctype html><html><head><meta charset='utf-8'>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Schugaa Setup</title>";
    html += "<style>";
    html += "body{margin:0;font-family:system-ui,-apple-system,Segoe UI,Roboto,sans-serif;";
    html += "background:linear-gradient(160deg,#07111a,#12303f);color:#eaf6ff;padding:18px;}";
    html += ".card{max-width:560px;margin:0 auto;background:rgba(8,18,24,.85);border:1px solid #2d5a70;";
    html += "border-radius:16px;padding:18px;box-shadow:0 18px 40px rgba(0,0,0,.35);}";
    html += "h1{margin:0 0 8px;font-size:1.35rem;}p{margin:0 0 12px;color:#b9d7e8;}";
    html += "label{display:block;font-size:.9rem;margin:10px 0 6px;color:#bfe9ff;}";
    html += "input,select{width:100%;box-sizing:border-box;padding:11px;border-radius:10px;";
    html += "border:1px solid #426879;background:#0c2430;color:#fff;font-size:1rem;}";
    html += "button{margin-top:14px;width:100%;padding:12px;border:0;border-radius:10px;";
    html += "font-weight:700;font-size:1rem;cursor:pointer;background:#2ee880;color:#083118;}";
    html += ".small{margin-top:10px;font-size:.85rem;color:#9ec2d2;}";
    html += ".status{margin:10px 0;padding:10px;border-radius:10px;font-size:.92rem;}";
    html += ".ok{background:#153f2a;border:1px solid #3ec074;color:#d8ffe8;}";
    html += ".err{background:#4a1f24;border:1px solid #d96f78;color:#ffe2e6;}";
    html += ".row{display:grid;grid-template-columns:1fr 1fr;gap:10px;}";
    html += "@media (max-width:560px){.row{grid-template-columns:1fr;}}";
    html += "a{color:#8dd4ff;text-decoration:none;}";
    html += "</style></head><body><div class='card'>";
    html += "<h1>Schugaa Libre Setup</h1>";
    html += "<p>Hotspot stays active. You can update credentials anytime.</p>";
    html += banner;
    html += "<form method='POST' action='/save'>";
    html += "<label>Libre Email</label>";
    html += "<input name='email' type='email' required value='";
    html += htmlEscape(libreEmail);
    html += "'>";
    html += "<label>Libre Password</label>";
    html += "<input name='password' type='password' required value='";
    html += htmlEscape(librePassword);
    html += "'>";
    html += "<div class='row'><div>";
    html += "<label>Region</label><select name='region'>";
    html += buildRegionOptions(libreRegion);
    html += "</select></div><div>";
    html += "<label>Patient ID (optional)</label>";
    html += "<input name='patientId' type='text' value='";
    html += htmlEscape(selectedPatientId);
    html += "'></div></div>";
    html += "<button type='submit'>Login & Save</button></form>";
    html += "<p class='small'>AP: <strong>";
    html += CONFIG_AP_SSID;
    html += "</strong> &middot; Portal: <a href='http://192.168.4.1'>192.168.4.1</a></p>";
    html += "<p class='small'><a href='/secrets.h'>Download generated secrets.h snippet</a></p>";
    html += "</div></body></html>";

    configServer.send(200, "text/html", html);
}

void handleConfigRoot() {
    sendConfigPage(configMessage, configMessageSuccess);
    configMessage = "";
}

void handleConfigSecretsDownload() {
    String generated = "#ifndef SECRETS_H\n#define SECRETS_H\n\n";
    generated += "#define WIFI_SSID     \"" + String(WIFI_SSID) + "\"\n";
    generated += "#define WIFI_PASSWORD \"" + String(WIFI_PASSWORD) + "\"\n";
    generated += "#define LIBRE_EMAIL      \"" + libreEmail + "\"\n";
    generated += "#define LIBRE_PASSWORD   \"" + librePassword + "\"\n";
    generated += "#define LIBRE_REGION     \"" + libreRegion + "\"\n";
    generated += "#define LIBRE_PATIENT_ID \"" + selectedPatientId + "\"\n";
    generated += "#define UI_TIMEZONE_TZ   \"" + String(UI_TIMEZONE_TZ) + "\"\n";
    generated += "\n#endif // SECRETS_H\n";

    configServer.sendHeader("Content-Disposition", "attachment; filename=\"secrets.generated.h\"");
    configServer.send(200, "text/plain", generated);
}

void handleConfigSave() {
    String email = configServer.arg("email");
    String password = configServer.arg("password");
    String region = configServer.arg("region");
    String patientId = configServer.arg("patientId");
    email.trim();
    password.trim();
    region.trim();
    region.toUpperCase();
    patientId.trim();

    if (email.length() == 0 || password.length() == 0) {
        sendConfigPage("Email and password are required.", false);
        return;
    }
    if (!isKnownRegion(region)) {
        sendConfigPage("Selected region is invalid.", false);
        return;
    }

    String oldEmail = libreEmail;
    String oldPassword = librePassword;
    String oldRegion = libreRegion;
    String oldPatient = selectedPatientId;

    libreEmail = email;
    librePassword = password;
    libreRegion = region;
    selectedPatientId = patientId;
    clearAuthTicket();

    bool loginOk = loginToLibre(true);
    if (loginOk) {
        saveConfigToPreferences();
        configMessage = "Login successful. Credentials saved on device.";
        configMessageSuccess = true;
        fetchGlucoseData();
        renderUI();
        sendConfigPage(configMessage, true);
        return;
    }

    libreEmail = oldEmail;
    librePassword = oldPassword;
    libreRegion = oldRegion;
    selectedPatientId = oldPatient;
    clearAuthTicket();
    String err = "Login failed. Check email/password/region and try again.";
    if (connectionStatus.length() > 0) {
        err += " (" + connectionStatus + ")";
    }
    sendConfigPage(err, false);
}

void setupConfigRoutes() {
    configServer.on("/", HTTP_GET, handleConfigRoot);
    configServer.on("/save", HTTP_POST, handleConfigSave);
    configServer.on("/secrets.h", HTTP_GET, handleConfigSecretsDownload);
    configServer.on("/generate_204", HTTP_GET, handleConfigRoot);
    configServer.on("/hotspot-detect.html", HTTP_GET, handleConfigRoot);
    configServer.on("/ncsi.txt", HTTP_GET, handleConfigRoot);
    configServer.on("/connecttest.txt", HTTP_GET, handleConfigRoot);
    configServer.onNotFound(handleConfigRoot);
}

void startConfigPortal() {
    if (configPortalStarted) {
        return;
    }

    WiFi.mode(WIFI_AP_STA);
    bool apOk = WiFi.softAP(CONFIG_AP_SSID, CONFIG_AP_PASSWORD);
    if (apOk) {
        dnsServer.start(DNS_PORT, "*", WiFi.softAPIP());
        setupConfigRoutes();
        configServer.begin();
        configPortalStarted = true;
        Serial.print("Config AP started: ");
        Serial.println(CONFIG_AP_SSID);
        Serial.print("Portal IP: ");
        Serial.println(WiFi.softAPIP());
    } else {
        Serial.println("Failed to start config AP.");
    }
}

void handleConfigPortal() {
    if (!configPortalStarted) {
        return;
    }
    dnsServer.processNextRequest();
    configServer.handleClient();
}

bool syncClock() {
    configTzTime(UI_TIMEZONE_TZ, "pool.ntp.org", "time.nist.gov", "time.google.com");

    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 5000)) {
        Serial.println("Clock sync failed.");
        clockSynced = false;
        return false;
    }

    clockSynced = true;
    Serial.printf("Clock synced: %02d:%02d\n", timeInfo.tm_hour, timeInfo.tm_min);
    return true;
}

void applyTheme(UiThemeMode themeMode) {
    currentThemeMode = themeMode;

    if (themeMode == THEME_LIGHT) {
        COLOR_BG = tft.color565(233, 240, 228);
        COLOR_PANEL = tft.color565(249, 251, 246);
        COLOR_PANEL_BORDER = tft.color565(136, 160, 139);
        COLOR_GRID = tft.color565(160, 172, 160);
        COLOR_LIMIT = tft.color565(208, 72, 72);
        COLOR_SAFE_FILL = tft.color565(192, 230, 186);
        COLOR_STATUS_BAR = tft.color565(251, 244, 204);
        COLOR_STATUS_TEXT = tft.color565(46, 54, 46);
        COLOR_TEXT_MUTED = tft.color565(94, 104, 94);
        COLOR_GRAPH_LINE = tft.color565(16, 156, 47);
    } else {
        COLOR_BG = tft.color565(8, 12, 18);
        COLOR_PANEL = tft.color565(14, 17, 18);
        COLOR_PANEL_BORDER = tft.color565(42, 78, 46);
        COLOR_GRID = tft.color565(78, 95, 80);
        COLOR_LIMIT = tft.color565(208, 96, 96);
        COLOR_SAFE_FILL = tft.color565(19, 58, 24);
        COLOR_STATUS_BAR = tft.color565(255, 248, 204);
        COLOR_STATUS_TEXT = tft.color565(62, 62, 62);
        COLOR_TEXT_MUTED = tft.color565(196, 204, 196);
        COLOR_GRAPH_LINE = tft.color565(58, 235, 92);
    }

    COLOR_RED = tft.color565(255, 64, 64);
    COLOR_YELLOW = tft.color565(255, 210, 64);
    COLOR_GREEN = tft.color565(42, 230, 70);
    COLOR_ORANGE = tft.color565(255, 145, 32);
}

bool isAutoLightThemeNow() {
    if (!clockSynced) {
        return false;
    }

    struct tm timeInfo;
    if (!getLocalTime(&timeInfo, 100)) {
        return false;
    }

    return timeInfo.tm_hour >= 7 && timeInfo.tm_hour < 19;
}

void updateThemeMode(bool forceRender) {
    bool autoLight = isAutoLightThemeNow();
    int autoPeriod = autoLight ? 1 : 0;

    if (clockSynced && autoPeriod != lastAutoThemePeriod) {
        lastAutoThemePeriod = autoPeriod;
        themeOverrideActive = false;
    }

    if (!themeOverrideActive) {
        UiThemeMode nextTheme = autoLight ? THEME_LIGHT : THEME_DARK;
        if (nextTheme != currentThemeMode) {
            applyTheme(nextTheme);
            if (forceRender) {
                renderUI();
            }
        }
    }
}

void handleTouchToggle() {
    bool touched = isTouchPressedRaw();

    if (touched && !lastTouchPressed && millis() - lastTouchToggleMs > 350) {
        themeOverrideActive = true;
        lastTouchToggleMs = millis();
        applyTheme(currentThemeMode == THEME_DARK ? THEME_LIGHT : THEME_DARK);
        Serial.printf("Touch theme toggle -> %s\n", currentThemeMode == THEME_LIGHT ? "LIGHT" : "DARK");
        renderUI();
    }

    lastTouchPressed = touched;
}

bool isTouchPressedRaw() {
    // Raw pressure-based touch detection works even without a calibration map.
    const uint16_t rawThreshold = 170;
    uint16_t z1 = tft.getTouchRawZ();
    if (z1 <= rawThreshold) {
        return false;
    }
    delay(2);
    uint16_t z2 = tft.getTouchRawZ();
    return z2 > rawThreshold;
}

void clearAuthTicket() {
    authTicket.token = "";
    authTicket.accountId = "";
    authTicket.expiresAtMillis = 0;
}

bool hasValidAuthTicket() {
    if (authTicket.token.isEmpty() || authTicket.accountId.isEmpty()) {
        return false;
    }
    return static_cast<long>(authTicket.expiresAtMillis - millis()) > 60000;
}

String sha256Hex(const String& value) {
    unsigned char hash[32];
    mbedtls_sha256(reinterpret_cast<const unsigned char*>(value.c_str()), value.length(), hash, 0);

    String encoded = "";
    encoded.reserve(64);
    for (int i = 0; i < 32; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", hash[i]);
        encoded += buf;
    }
    return encoded;
}

const char* libreHostForRegion(const String& region) {
    for (const auto& entry : REGION_HOSTS) {
        if (region.equalsIgnoreCase(entry.region)) {
            return entry.host;
        }
    }
    return nullptr;
}

String libreBaseUrl() {
    const char* host = libreHostForRegion(libreRegion);
    if (host == nullptr) {
        return "";
    }
    return String("https://") + host;
}

void addLibreHeaders(HTTPClient& http) {
    http.addHeader("User-Agent", LIBRE_USER_AGENT);
    http.addHeader("Content-Type", "application/json;charset=UTF-8");
    http.addHeader("version", LIBRE_LINK_UP_VERSION);
    http.addHeader("product", LIBRE_LINK_UP_PRODUCT);
    http.addHeader("accept-encoding", "gzip");
    http.addHeader("cache-control", "no-cache");
    http.addHeader("connection", "Keep-Alive");
}

void addAuthHeaders(HTTPClient& http) {
    addLibreHeaders(http);
    http.addHeader("Authorization", "Bearer " + authTicket.token);
    http.addHeader("account-id", sha256Hex(authTicket.accountId));
}

String readHttpResponse(HTTPClient& http) {
    String response = http.getString();
    response.trim();
    return response;
}

int glucoseValueFromJson(JsonVariantConst variant) {
    if (!variant.is<JsonObjectConst>()) {
        return 0;
    }

    JsonObjectConst obj = variant.as<JsonObjectConst>();
    if (!obj["ValueInMgPerDl"].isNull()) {
        return obj["ValueInMgPerDl"].as<int>();
    }
    if (!obj["Value"].isNull()) {
        return obj["Value"].as<int>();
    }
    if (!obj["valueInMgPerDl"].isNull()) {
        return obj["valueInMgPerDl"].as<int>();
    }
    if (!obj["value"].isNull()) {
        return obj["value"].as<int>();
    }
    return 0;
}

String timestampFromJson(JsonVariantConst variant) {
    if (!variant.is<JsonObjectConst>()) {
        return "--";
    }

    JsonObjectConst obj = variant.as<JsonObjectConst>();
    if (!obj["FactoryTimestamp"].isNull()) {
        return obj["FactoryTimestamp"].as<String>();
    }
    if (!obj["Timestamp"].isNull()) {
        return obj["Timestamp"].as<String>();
    }
    if (!obj["timestamp"].isNull()) {
        return obj["timestamp"].as<String>();
    }
    if (!obj["factoryTimestamp"].isNull()) {
        return obj["factoryTimestamp"].as<String>();
    }
    return "--";
}

time_t parseLibreTimeValue(JsonVariantConst value) {
    if (value.isNull()) {
        return 0;
    }

    if (value.is<int>() || value.is<long>() || value.is<long long>() || value.is<float>() || value.is<double>()) {
        double numericValue = value.as<double>();
        if (numericValue > 20000000000.0) {
            numericValue /= 1000.0;
        }
        if (numericValue > 100000.0) {
            return static_cast<time_t>(numericValue);
        }
        return 0;
    }

    String raw = value.as<String>();
    raw.trim();
    if (raw.length() == 0) {
        return 0;
    }

    if (raw.indexOf('T') > 0) {
        struct tm tmValue = {};
        int year = raw.substring(0, 4).toInt();
        int month = raw.substring(5, 7).toInt();
        int day = raw.substring(8, 10).toInt();
        int hour = raw.substring(11, 13).toInt();
        int minute = raw.substring(14, 16).toInt();
        int second = raw.substring(17, 19).toInt();

        tmValue.tm_year = year - 1900;
        tmValue.tm_mon = month - 1;
        tmValue.tm_mday = day;
        tmValue.tm_hour = hour;
        tmValue.tm_min = minute;
        tmValue.tm_sec = second;
        return mktime(&tmValue);
    }

    const int firstSpace = raw.indexOf(' ');
    const int secondSpace = raw.indexOf(' ', firstSpace + 1);
    const int firstSlash = raw.indexOf('/');
    const int secondSlash = raw.indexOf('/', firstSlash + 1);
    if (firstSpace > 0 && secondSpace > firstSpace && firstSlash > 0 && secondSlash > firstSlash) {
        struct tm tmValue = {};
        tmValue.tm_mon = raw.substring(0, firstSlash).toInt() - 1;
        tmValue.tm_mday = raw.substring(firstSlash + 1, secondSlash).toInt();
        tmValue.tm_year = raw.substring(secondSlash + 1, firstSpace).toInt() - 1900;

        String timePart = raw.substring(firstSpace + 1, secondSpace);
        String ampm = raw.substring(secondSpace + 1);
        int firstColon = timePart.indexOf(':');
        int lastColon = timePart.lastIndexOf(':');
        int hour = timePart.substring(0, firstColon).toInt();
        int minute = timePart.substring(firstColon + 1, lastColon).toInt();
        int second = timePart.substring(lastColon + 1).toInt();

        if (ampm == "PM" && hour < 12) {
            hour += 12;
        } else if (ampm == "AM" && hour == 12) {
            hour = 0;
        }

        tmValue.tm_hour = hour;
        tmValue.tm_min = minute;
        tmValue.tm_sec = second;
        return mktime(&tmValue);
    }

    return 0;
}

int sensorDaysLeftFromJson(JsonVariantConst variant) {
    if (!variant.is<JsonObjectConst>()) {
        return 0;
    }

    JsonObjectConst obj = variant.as<JsonObjectConst>();
    time_t nowTs = time(nullptr);
    if (nowTs < 100000) {
        Serial.println("Sensor days left skipped: clock not synced.");
        return 0;
    }

    time_t endTs = parseLibreTimeValue(obj["e"]);
    if (endTs == 0) {
        endTs = parseLibreTimeValue(obj["expirationTimestamp"]);
    }
    if (endTs == 0) {
        endTs = parseLibreTimeValue(obj["endDate"]);
    }
    if (endTs == 0) {
        endTs = parseLibreTimeValue(obj["expires"]);
    }

    if (endTs > 0) {
        long remaining = static_cast<long>(endTs - nowTs);
        if (remaining <= 0) {
            return 0;
        }
        return static_cast<int>((remaining + 86399) / 86400);
    }

    time_t activatedTs = parseLibreTimeValue(obj["a"]);
    if (activatedTs == 0) {
        activatedTs = parseLibreTimeValue(obj["activationTimestamp"]);
    }
    if (activatedTs == 0) {
        activatedTs = parseLibreTimeValue(obj["startDate"]);
    }
    if (activatedTs == 0) {
        Serial.println("Sensor days left: no known timestamp field in sensor object.");
        return 0;
    }

    long remaining = static_cast<long>((14 * 24 * 3600) - (nowTs - activatedTs));
    if (remaining <= 0) {
        return 0;
    }
    return static_cast<int>((remaining + 86399) / 86400);
}

bool loginToLibre(bool allowRedirect) {
    const String baseUrl = libreBaseUrl();
    if (baseUrl.isEmpty()) {
        Serial.println("Unsupported Libre region.");
        connectionStatus = "Region";
        return false;
    }

    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    if (!http.begin(secureClient, baseUrl + "/llu/auth/login")) {
        Serial.println("Failed to open Libre login URL.");
        connectionStatus = "Login URL";
        return false;
    }

    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    addLibreHeaders(http);

    const String body =
        String("{\"email\":\"") + libreEmail + "\",\"password\":\"" + librePassword + "\"}";

    Serial.print("Libre login region: ");
    Serial.println(libreRegion);
    const int httpCode = http.POST(body);
    const String response = readHttpResponse(http);
    http.end();

    Serial.printf("Libre login HTTP: %d\n", httpCode);
    if (httpCode != HTTP_CODE_OK) {
        Serial.println(response);
        connectionStatus = "Login HTTP";
        clearAuthTicket();
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.print("Libre login parse error: ");
        Serial.println(error.c_str());
        connectionStatus = "Login Parse";
        clearAuthTicket();
        return false;
    }

    if (!doc["data"].isNull() && doc["data"]["redirect"].as<bool>() && allowRedirect) {
        String redirectedRegion = doc["data"]["region"].as<String>();
        redirectedRegion.toUpperCase();
        if (redirectedRegion.length() > 0) {
            libreRegion = redirectedRegion;
            Serial.print("Libre redirected to region: ");
            Serial.println(libreRegion);
            return loginToLibre(false);
        }
    }

    if (!doc["status"].isNull() && doc["status"].as<int>() != 0) {
        Serial.println("Libre login returned non-zero status.");
        Serial.println(response);
        connectionStatus = "Login Err";
        clearAuthTicket();
        return false;
    }

    authTicket.token = doc["data"]["authTicket"]["token"].as<String>();
    authTicket.accountId = doc["data"]["user"]["id"].as<String>();

    unsigned long long durationRaw = 0;
    if (!doc["data"]["authTicket"]["duration"].isNull()) {
        durationRaw = doc["data"]["authTicket"]["duration"].as<unsigned long long>();
    }

    unsigned long durationMs = 45UL * 60UL * 1000UL;
    if (durationRaw > 0 && durationRaw < 86400ULL) {
        durationMs = static_cast<unsigned long>(durationRaw * 1000ULL);
    } else if (durationRaw >= 86400ULL && durationRaw < 86400000ULL) {
        durationMs = static_cast<unsigned long>(durationRaw);
    }

    authTicket.expiresAtMillis = millis() + durationMs;

    if (authTicket.token.isEmpty() || authTicket.accountId.isEmpty()) {
        Serial.println("Libre login missing auth ticket.");
        connectionStatus = "Login Data";
        clearAuthTicket();
        return false;
    }

    Serial.println("Libre login successful.");
    connectionStatus = "Login OK";
    return true;
}

bool ensureLibreAuthentication() {
    if (hasValidAuthTicket()) {
        return true;
    }
    return loginToLibre(true);
}

bool fetchLibreConnections(LibrePatientSnapshot& snapshot) {
    if (!ensureLibreAuthentication()) {
        return false;
    }

    const String baseUrl = libreBaseUrl();
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    if (!http.begin(secureClient, baseUrl + "/llu/connections")) {
        Serial.println("Failed to open Libre connections URL.");
        connectionStatus = "Conn URL";
        return false;
    }

    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    addAuthHeaders(http);

    int httpCode = http.GET();
    String response = readHttpResponse(http);
    http.end();

    if ((httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) && loginToLibre(true)) {
        return fetchLibreConnections(snapshot);
    }

    Serial.printf("Libre connections HTTP: %d\n", httpCode);
    if (httpCode != HTTP_CODE_OK) {
        Serial.println(response);
        connectionStatus = "Conn HTTP";
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.print("Libre connections parse error: ");
        Serial.println(error.c_str());
        connectionStatus = "Conn Parse";
        return false;
    }

    JsonArray connections;
    if (doc.is<JsonArray>()) {
        connections = doc.as<JsonArray>();
    } else if (doc["data"].is<JsonArray>()) {
        if (!doc["status"].isNull() && doc["status"].as<int>() != 0) {
            Serial.println("Libre connections status was non-zero.");
            connectionStatus = "Conn Err";
            return false;
        }
        connections = doc["data"].as<JsonArray>();
    } else if (doc["data"]["connections"].is<JsonArray>()) {
        connections = doc["data"]["connections"].as<JsonArray>();
    } else {
        Serial.println("Libre connections shape was unexpected.");
        connectionStatus = "Conn Shape";
        return false;
    }

    if (connections.isNull() || connections.size() == 0) {
        Serial.println("Libre connections returned no patients.");
        connectionStatus = "No Patient";
        return false;
    }

    JsonObject chosen = connections[0];
    if (selectedPatientId.length() > 0) {
        bool found = false;
        for (JsonObject candidate : connections) {
            if (candidate["patientId"].as<String>() == selectedPatientId) {
                chosen = candidate;
                found = true;
                break;
            }
        }
        if (!found) {
            Serial.println("Configured patient ID not found. Falling back to first patient.");
        }
    }

    snapshot.patientId = chosen["patientId"].as<String>();
    snapshot.firstName = chosen["firstName"] | "";
    snapshot.lastName = chosen["lastName"] | "";
    snapshot.glucose = glucoseValueFromJson(chosen["glucoseMeasurement"]);
    snapshot.timestampRaw = timestampFromJson(chosen["glucoseMeasurement"]);
    snapshot.trend = chosen["glucoseMeasurement"]["TrendArrow"] | 3;
    snapshot.sensorDaysLeft = sensorDaysLeftFromJson(chosen["sensor"]);
    snapshot.valid = snapshot.patientId.length() > 0;

    Serial.print("Selected Libre patient: ");
    Serial.println(snapshot.patientId);
    connectionStatus = "Conn OK";
    return snapshot.valid;
}

bool fetchLibreGraph(const String& patientId) {
    if (patientId.length() == 0) {
        return false;
    }

    if (!ensureLibreAuthentication()) {
        return false;
    }

    const String baseUrl = libreBaseUrl();
    WiFiClientSecure secureClient;
    secureClient.setInsecure();

    HTTPClient http;
    if (!http.begin(secureClient, baseUrl + "/llu/connections/" + patientId + "/graph")) {
        Serial.println("Failed to open Libre graph URL.");
        connectionStatus = "Graph URL";
        return false;
    }

    http.setConnectTimeout(8000);
    http.setTimeout(15000);
    addAuthHeaders(http);

    int httpCode = http.GET();
    String response = readHttpResponse(http);
    http.end();

    if ((httpCode == HTTP_CODE_UNAUTHORIZED || httpCode == HTTP_CODE_FORBIDDEN) && loginToLibre(true)) {
        return fetchLibreGraph(patientId);
    }

    Serial.printf("Libre graph HTTP: %d\n", httpCode);
    if (httpCode != HTTP_CODE_OK) {
        Serial.println(response);
        connectionStatus = "Graph HTTP";
        return false;
    }

    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, response);
    if (error) {
        Serial.print("Libre graph parse error: ");
        Serial.println(error.c_str());
        connectionStatus = "Graph Parse";
        return false;
    }

    JsonArray graphArray;
    if (doc.is<JsonArray>()) {
        graphArray = doc.as<JsonArray>();
    } else if (doc["data"]["graphData"].is<JsonArray>()) {
        if (!doc["status"].isNull() && doc["status"].as<int>() != 0) {
            Serial.println("Libre graph status was non-zero.");
            connectionStatus = "Graph Err";
            return false;
        }
        graphArray = doc["data"]["graphData"].as<JsonArray>();
    } else if (doc["graphData"].is<JsonArray>()) {
        graphArray = doc["graphData"].as<JsonArray>();
    } else {
        Serial.println("Libre graph shape was unexpected.");
        connectionStatus = "Graph Shape";
        return false;
    }

    if (doc["data"]["activeSensor"].is<JsonObjectConst>()) {
        int graphSensorDays = sensorDaysLeftFromJson(doc["data"]["activeSensor"]);
        if (graphSensorDays > 0) {
            sensorDaysLeft = graphSensorDays;
        }
    } else if (doc["data"]["connection"]["sensor"].is<JsonObjectConst>()) {
        int graphSensorDays = sensorDaysLeftFromJson(doc["data"]["connection"]["sensor"]);
        if (graphSensorDays > 0) {
            sensorDaysLeft = graphSensorDays;
        }
    }

    historyCount = 0;
    const int totalPoints = graphArray.size();
    const int startIndex = totalPoints > MAX_HISTORY_POINTS ? totalPoints - MAX_HISTORY_POINTS : 0;

    for (int i = startIndex; i < totalPoints && historyCount < MAX_HISTORY_POINTS; i++) {
        JsonVariant point = graphArray[i];
        history[historyCount].value = glucoseValueFromJson(point);
        history[historyCount].timestamp = formatTimeLabel(timestampFromJson(point));
        historyCount++;
    }

    Serial.print("Libre graph points: ");
    Serial.println(historyCount);
    connectionStatus = "OK";
    return historyCount > 0;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n=== Schugaa CYD Boot ===");
    Serial.print("UI theme: ");
    Serial.println(UI_THEME_VERSION);

    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);

    tft.init();
    tft.setRotation(1);
    tft.invertDisplay(true);
    applyTheme(THEME_DARK);

    loadConfigFromPreferences();
    startConfigPortal();

    drawStartupScreen("Schugaa CYD", "Config AP: 192.168.4.1");
    delay(1200);
    drawStartupScreen("Schugaa CYD", "Connecting to WiFi...");

    connectWiFi();
    syncClock();
    updateThemeMode(false);

    drawStartupScreen("Schugaa CYD", "Logging into Libre...");
    fetchGlucoseData();
    renderUI();
}

void loop() {
    handleConfigPortal();

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
        syncClock();
        updateThemeMode(false);
        renderUI();
    }

    handleTouchToggle();
    updateThemeMode(true);

    if (millis() - lastFetchTime >= FETCH_INTERVAL_MS) {
        fetchGlucoseData();
        renderUI();
    }

    delay(80);
}

void connectWiFi() {
    Serial.print("Connecting to WiFi: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected.");
        Serial.println(WiFi.localIP());
        connectionStatus = "WiFi OK";
    } else {
        Serial.println("\nWiFi connection failed.");
        connectionStatus = "WiFi";
    }
}

bool fetchGlucoseData() {
    lastFetchTime = millis();

    if (libreEmail.length() == 0 || librePassword.length() == 0) {
        connectionStatus = "Cfg Required";
        return false;
    }

    LibrePatientSnapshot snapshot;
    if (!fetchLibreConnections(snapshot)) {
        return false;
    }

    currentGlucose = snapshot.glucose;
    currentTrend = snapshot.trend;
    currentTimestamp = formatTimeLabel(snapshot.timestampRaw);
    lastUpdatedStr = currentTimestamp;
    sensorDaysLeft = snapshot.sensorDaysLeft;

    if (!fetchLibreGraph(snapshot.patientId)) {
        historyCount = 0;
        if (currentGlucose > 0) {
            history[0].value = currentGlucose;
            history[0].timestamp = currentTimestamp;
            historyCount = 1;
            connectionStatus = "Curr Only";
            return true;
        }
        return false;
    }

    if (currentGlucose <= 0 && historyCount > 0) {
        currentGlucose = history[historyCount - 1].value;
        currentTimestamp = history[historyCount - 1].timestamp;
        lastUpdatedStr = currentTimestamp;
    }

    return currentGlucose > 0 || historyCount > 0;
}

uint16_t glucoseColor(int value) {
    if (value < 70) {
        return COLOR_RED;
    }
    if (value < 80) {
        return COLOR_YELLOW;
    }
    if (value <= 180) {
        return COLOR_GREEN;
    }
    if (value <= 220) {
        return COLOR_YELLOW;
    }
    if (value <= 250) {
        return COLOR_ORANGE;
    }
    return COLOR_RED;
}

const char* trendArrow(int trend) {
    switch (trend) {
        case 1:
            return "v";
        case 2:
            return "\\";
        case 3:
            return "->";
        case 4:
            return "/";
        case 5:
            return "^";
        default:
            return "?";
    }
}

void renderUI() {
    tft.fillScreen(COLOR_BG);
    drawChart();
    drawStatusBar();
}

void drawChart() {
    const int panelX = 8;
    const int panelY = 6;
    const int panelW = 304;
    const int panelH = 208;
    const int graphLeft = panelX + 26;
    const int graphRight = panelX + panelW - 14;
    const int graphTop = panelY + 14;
    const int graphBottom = panelY + panelH - 32;
    const int graphWidth = graphRight - graphLeft;
    const int graphHeight = graphBottom - graphTop;
    const int minValue = 50;
    const int maxValue = 300;
    const int lowTarget = 70;
    const int highTarget = 180;
    const int veryHigh = 250;

    tft.fillRoundRect(panelX, panelY, panelW, panelH, 12, COLOR_PANEL);
    tft.drawRoundRect(panelX, panelY, panelW, panelH, 12, COLOR_PANEL_BORDER);

    const int yHighTarget = mapGraphY(highTarget, minValue, maxValue, graphTop, graphHeight);
    const int yLowTarget = mapGraphY(lowTarget, minValue, maxValue, graphTop, graphHeight);
    const int yVeryHigh = mapGraphY(veryHigh, minValue, maxValue, graphTop, graphHeight);

    tft.fillRect(graphLeft, yHighTarget, graphWidth, yLowTarget - yHighTarget, COLOR_SAFE_FILL);

    const int gridValues[] = {50, 100, 150, 200, 250, 300};
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COLOR_TEXT_MUTED, COLOR_PANEL);

    for (int value : gridValues) {
        const int y = mapGraphY(value, minValue, maxValue, graphTop, graphHeight);
        for (int x = graphLeft; x < graphRight; x += 7) {
            tft.drawPixel(x, y, value == lowTarget || value == veryHigh ? COLOR_LIMIT : COLOR_GRID);
        }
        tft.drawString(String(value), graphLeft - 7, y, 1);
    }

    for (int x = graphLeft; x < graphRight; x += 8) {
        tft.fillRect(x, yLowTarget - 1, 5, 3, COLOR_LIMIT);
        tft.fillRect(x, yVeryHigh - 1, 5, 3, COLOR_LIMIT);
    }

    if (historyCount <= 0) {
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_LIGHTGREY, COLOR_PANEL);
        tft.drawString("Waiting for glucose data", panelX + panelW / 2, panelY + panelH / 2, 2);
        return;
    }

    int previousX = -1;
    int previousY = -1;
    for (int index = 0; index < historyCount; index++) {
        const int x = mapGraphX(index, historyCount, graphLeft, graphWidth);
        const int y = mapGraphY(history[index].value, minValue, maxValue, graphTop, graphHeight);

        if (index > 0) {
            tft.drawLine(previousX, previousY - 1, x, y - 1, COLOR_GRAPH_LINE);
            tft.drawLine(previousX, previousY, x, y, COLOR_GRAPH_LINE);
            tft.drawLine(previousX, previousY + 1, x, y + 1, COLOR_GRAPH_LINE);
        }

        if (index % 4 == 0 || index == historyCount - 1) {
            const uint16_t dotColor = glucoseColor(history[index].value);
            tft.fillCircle(x, y, 4, dotColor);
            tft.drawCircle(x, y, 4, TFT_WHITE);
        }

        previousX = x;
        previousY = y;
    }

    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_LIGHTGREY, COLOR_PANEL);
    tft.drawString(history[0].timestamp, graphLeft, graphBottom + 12, 1);
    tft.drawString(history[historyCount / 2].timestamp, graphLeft + (graphWidth / 2), graphBottom + 12, 1);
    tft.drawString(history[historyCount - 1].timestamp, graphRight, graphBottom + 12, 1);
}

void drawStatusBar() {
    const int barY = 220;
    const int barHeight = 20;
    const int segmentWidth = 320 / 3;
    const String leftText = "Status: " + connectionStatus;
    const String centerText = "Updated: " + lastUpdatedStr;
    const String rightText = "Sensor: " + String(sensorDaysLeft) + " days";

    tft.fillRect(0, barY, 320, barHeight, COLOR_STATUS_BAR);
    tft.setTextColor(COLOR_STATUS_TEXT, COLOR_STATUS_BAR);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(leftText, segmentWidth / 2, barY + (barHeight / 2), 1);
    tft.drawString(centerText, segmentWidth + (segmentWidth / 2), barY + (barHeight / 2), 1);
    tft.drawString(rightText, (segmentWidth * 2) + (segmentWidth / 2), barY + (barHeight / 2), 1);

    tft.setTextDatum(TR_DATUM);
    drawBoldText(String(currentGlucose) + " " + trendArrow(currentTrend), 306, 12, 4,
                 glucoseColor(currentGlucose), COLOR_BG, 1);
    drawBoldText("mg/dL", 306, 42, 2, COLOR_TEXT_MUTED, COLOR_BG, 1);
}
