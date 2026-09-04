/*
 * WiFi, the setup portal, the settings page and NVS storage.
 */

#include "config.h"

// ------------------------------------------------------------ settings

void loadConfig()
{
  prefs.begin("flipclock", true);
  cfg.ssid       = prefs.getString("ssid", "");
  cfg.pass       = prefs.getString("pass", "");
  cfg.tz         = prefs.getString("tz", "UTC0");
  cfg.fmt12      = prefs.getBool("fmt12", false);
  cfg.offHours   = prefs.getFloat("offH", 0.0f);
  cfg.offMinutes = prefs.getFloat("offM", 0.0f);
  prefs.end();

  hours.homeOffset   = cfg.offHours;
  minutes.homeOffset = cfg.offMinutes;
}

void saveConfig()
{
  prefs.begin("flipclock", false);
  prefs.putString("ssid", cfg.ssid);
  prefs.putString("pass", cfg.pass);
  prefs.putString("tz",   cfg.tz);
  prefs.putBool("fmt12",  cfg.fmt12);
  prefs.putFloat("offH",  cfg.offHours);
  prefs.putFloat("offM",  cfg.offMinutes);
  prefs.end();
}

// ----------------------------------------------------------------- page

// Fallback list for the portal, where the phone has no internet to fetch the
// full IANA table.
static const char *TZ_PRESETS[][2] = {
  {"UTC0",                          "UTC"},
  {"EST5EDT,M3.2.0,M11.1.0",        "US Eastern - New York"},
  {"CST6CDT,M3.2.0,M11.1.0",        "US Central - Chicago"},
  {"MST7MDT,M3.2.0,M11.1.0",        "US Mountain - Denver"},
  {"MST7",                          "US Arizona - Phoenix"},
  {"PST8PDT,M3.2.0,M11.1.0",        "US Pacific - Los Angeles"},
  {"AKST9AKDT,M3.2.0,M11.1.0",      "US Alaska - Anchorage"},
  {"HST10",                         "US Hawaii - Honolulu"},
  {"GMT0BST,M3.5.0/1,M10.5.0",      "London"},
  {"CET-1CEST,M3.5.0,M10.5.0/3",    "Berlin / Warsaw"},
  {"EET-2EEST,M3.5.0/3,M10.5.0/4",  "Kyiv"},
};

static String htmlPage()
{
  String p = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Flip Clock</title><style>"
               "body{font:16px system-ui;margin:0;padding:24px;background:#111;color:#eee}"
               "h1{font-size:20px;margin:0 0 20px}"
               "label{display:block;margin:14px 0 4px;color:#aaa;font-size:14px}"
               "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;"
               "border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}"
               "button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;"
               "background:#e0e0e0;color:#111;font-size:16px;font-weight:600}"
               "a{color:#7ab7ff}.n{color:#777;font-size:13px;margin-top:16px}"
               "</style><h1>Flip Clock</h1><form method=POST action=/save>");

  p += F("<label>WiFi network</label><input name=ssid list=nets value='");
  p += cfg.ssid;
  p += F("'><datalist id=nets>");

  int n = WiFi.scanNetworks();
  for (int i = 0; i < n && i < 15; i++) {
    p += "<option value='" + WiFi.SSID(i) + "'>";
  }

  p += F("</datalist><label>Password</label>"
         "<input name=pass type=password placeholder='leave empty to keep current'>");

  p += F("<label>Timezone</label><select name=tz id=tz>");
  for (auto &z : TZ_PRESETS) {
    p += "<option value='" + String(z[0]) + "'";
    if (cfg.tz == z[0]) p += " selected";
    p += ">" + String(z[1]) + "</option>";
  }
  p += F("</select>");

  p += F("<label>Time format</label><select name=fmt>");
  p += cfg.fmt12 ? F("<option value=24>24 hour</option><option value=12 selected>12 hour</option>")
                 : F("<option value=24 selected>24 hour</option><option value=12>12 hour</option>");
  p += F("</select>");

  p += F("<label>Home offset, hours drum</label><input name=offh type=number step=0.5 value='");
  p += String(cfg.offHours, 1);
  p += F("'><label>Home offset, minutes drum</label><input name=offm type=number step=0.5 value='");
  p += String(cfg.offMinutes, 1);
  p += F("'>");

  p += F("<button>Save and restart</button></form>"
         "<p class=n><b>Home offset</b> is measured in flaps: how many of them sit "
         "between the magnet and 00. One flap = 34 motor steps = 6 degrees of drum "
         "rotation. Halves are allowed, 0.5 shifts the drum by 17 steps.<br><br>"
         "After homing the drum must show 00. Shows 59 - add 1. Shows 01 - subtract 1. "
         "Running behind means the offset is too small, running ahead means too large. "
         "A new value takes effect on the next homing.</p>");

  if (!portalMode)
    p += F("<p class=n><a href=/rehome>Re-home both drums now</a> &nbsp;·&nbsp; "
           "<a href=/forget>Erase WiFi and settings</a></p>");

  // The phone pulls the full IANA -> POSIX table and preselects its own zone.
  // Done in the browser, not on the board. Without internet - in portal mode,
  // for instance - the built in presets above stay as they are.
  p += "<script>const CUR='" + cfg.tz + "';";
  p += F("const MINE=Intl.DateTimeFormat().resolvedOptions().timeZone;"
         "fetch('https://cdn.jsdelivr.net/gh/nayarsystems/posix_tz_db@master/zones.json')"
         ".then(r=>r.json()).then(z=>{"
         "const s=document.getElementById('tz');s.innerHTML='';let hit=false;"
         "for(const k in z){const o=new Option(k,z[k]);"
         "if(z[k]===CUR){o.selected=true;hit=true;}s.add(o);}"
         "if(!hit&&z[MINE])s.value=z[MINE];}).catch(()=>{});</script>");

  return p;
}

// ------------------------------------------------------------- handlers

// In the portal the WPA2 passphrase is the gate already, and a login prompt
// there tends to confuse captive portal browsers. On a real network anyone
// could reach the page, so ask for credentials.
static bool denied()
{
  if (portalMode) return false;
  if (server.authenticate(UI_USER, UI_PASS)) return false;

  server.requestAuthentication();
  return true;
}

static void handleRoot()
{
  if (denied()) return;
  server.send(200, "text/html", htmlPage());
}

static void handleForget()
{
  if (denied()) return;

  prefs.begin("flipclock", false);
  prefs.clear();
  prefs.end();
  WiFi.disconnect(true, true);   // also wipes the copy the WiFi stack keeps in NVS

  server.send(200, "text/html",
              F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<body style='font:16px system-ui;background:#111;color:#eee;padding:24px'>"
                "Settings erased. Restarting into setup mode."));
  delay(500);
  ESP.restart();
}

static void handleSave()
{
  if (denied()) return;

  if (server.hasArg("ssid")) cfg.ssid = server.arg("ssid");
  if (server.arg("pass").length()) cfg.pass = server.arg("pass");
  if (server.hasArg("tz"))   cfg.tz = server.arg("tz");

  cfg.fmt12      = server.arg("fmt") == "12";
  cfg.offHours   = server.arg("offh").toFloat();
  cfg.offMinutes = server.arg("offm").toFloat();

  saveConfig();

  server.send(200, "text/html",
              F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
                "<body style='font:16px system-ui;background:#111;color:#eee;padding:24px'>"
                "Saved. Restarting."));
  delay(500);
  ESP.restart();
}

static void handleRehome()
{
  if (denied()) return;

  server.sendHeader("Location", "/");
  server.send(303);
  homeDrum(hours);
  homeDrum(minutes);
}

// ---------------------------------------------------------------- setup

void startServer()
{
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/rehome", handleRehome);
  server.on("/forget", handleForget);
  server.onNotFound(handleRoot);      // captive portal: anything lands on the form
  server.begin();
}

void startPortal()
{
  portalMode = true;

  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_NAME, AP_PASS);      // WPA2, so the typed in password is not broadcast
  dns.start(53, "*", WiFi.softAPIP());
  startServer();

  Serial.printf("portal: connect to \"%s\" with password \"%s\", open http://%s\n",
                AP_NAME, AP_PASS, WiFi.softAPIP().toString().c_str());
}

bool connectWiFi()
{
  if (cfg.ssid.length() == 0) return false;

  WiFi.mode(WIFI_STA);
  WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

  uint32_t start = millis();
  Serial.print("WiFi");
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();

  return WiFi.status() == WL_CONNECTED;
}
