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

// Network names are attacker controlled - a neighbour can call their AP
// anything at all, and it lands straight in our HTML.
static String esc(const String &s)
{
  String o;
  o.reserve(s.length() + 8);

  for (unsigned i = 0; i < s.length(); i++) {
    switch (s[i]) {
      case '&':  o += F("&amp;");  break;
      case '<':  o += F("&lt;");   break;
      case '>':  o += F("&gt;");   break;
      case '"':  o += F("&quot;"); break;
      case '\'': o += F("&#39;");  break;
      default:   o += s[i];
    }
  }
  return o;
}

// Built once when the portal starts. Scanning blocks for seconds and knocks
// the station link off channel, so it must not happen per request.
static String netOptions;

static void scanNetworks()
{
  int n = WiFi.scanNetworks();
  netOptions = "";

  for (int i = 0; i < n && i < 15; i++)
    netOptions += "<option value='" + esc(WiFi.SSID(i)) + "'>";

  WiFi.scanDelete();
}

static String htmlPage()
{
  String p;
  p.reserve(7000);   // the page is ~5 KB, and it is grown in small pieces

  p += F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Flip Clock</title><style>"
               "body{font:16px system-ui;margin:0;padding:24px;background:#111;color:#eee}"
               "h1{font-size:20px;margin:0 0 20px}"
               "label{display:block;margin:14px 0 4px;color:#aaa;font-size:14px}"
               "input,select{width:100%;box-sizing:border-box;padding:10px;border-radius:8px;"
               "border:1px solid #444;background:#1c1c1c;color:#eee;font-size:16px}"
               "button{width:100%;margin-top:20px;padding:12px;border:0;border-radius:8px;"
               "background:#e0e0e0;color:#111;font-size:16px;font-weight:600}"
               ".row{display:flex;gap:10px;margin-top:16px}"
               ".tzrow{display:flex;gap:10px}.tzrow select:first-child{flex:0 0 38%}"
               ".tzrow select:last-child{flex:1;min-width:0}"
               ".row form{flex:1}.row button{margin:0;background:#2a2a2a;color:#ccc;font-weight:400}"
               ".chk{display:flex;align-items:center;gap:8px;margin-top:10px}"
               ".chk input{width:auto}"
               ".n{color:#777;font-size:13px;margin-top:16px}"
               "</style><h1>Flip Clock</h1><form method=POST action=/save>");

  p += F("<label>WiFi network</label><input name=ssid list=nets value='");
  p += esc(cfg.ssid);
  p += F("'><datalist id=nets>");
  p += netOptions;

  p += F("</datalist><label>Password</label>"
         "<input name=pass type=password placeholder='leave empty to keep current'>"
         "<label class=chk><input type=checkbox name=nopass>open network, no password</label>");

  p += F("<label>Timezone</label>"
         "<div class=tzrow id=tzpick hidden>"
         "<select id=tzregion></select><select id=tzcity></select></div>"
         "<input type=hidden id=tzval>"
         "<select name=tz id=tz>");

  bool known = false;
  for (auto &z : TZ_PRESETS) if (cfg.tz == z[0]) known = true;

  // Keep the stored zone in the list even when it came from the full IANA
  // table, otherwise reloading the page would silently reset it.
  if (!known) {
    p += "<option value='" + esc(cfg.tz) + "' selected>" + esc(cfg.tz) + "</option>";
  }

  for (auto &z : TZ_PRESETS) {
    p += "<option value='" + String(z[0]) + "'";
    if (cfg.tz == z[0]) p += " selected";
    p += ">" + String(z[1]) + "</option>";
  }
  p += F("</select><div class=n id=tzinfo></div>");

  p += F("<label>Time format</label><select name=fmt>");
  p += cfg.fmt12 ? F("<option value=24>24 hour</option><option value=12 selected>12 hour</option>")
                 : F("<option value=24 selected>24 hour</option><option value=12>12 hour</option>");
  p += F("</select>");

  p += F("<label>Home offset, hours drum</label><input name=offh type=number step=0.5 value='");
  p += String(cfg.offHours, 1);
  p += F("'><label>Home offset, minutes drum</label><input name=offm type=number step=0.5 value='");
  p += String(cfg.offMinutes, 1);
  p += F("'>");

  p += F("<button>Save and restart</button></form>");

  p += F("<p class=n><b>Home offset</b> is measured in flaps: how many of them sit "
         "between the magnet and 00. Whole flaps only tell the firmware which "
         "number the magnet sits under, so the drum never reverses or winds most "
         "of a turn. A fraction is a real move: 0.5 nudges the drum 17 steps.<br><br>"
         "To calibrate, compare the window with what the firmware thinks and add "
         "the difference. Firmware says <b>hours ");
  p += String(hours.value);
  p += F(", minutes ");
  p += String(minutes.value);
  p += F("</b> right now. Window one lower than that - add 1, one higher - "
         "subtract 1. Takes effect on the next homing.</p>");

  // POST, not links: with basic auth the browser would attach credentials to
  // any cross site request, so a GET endpoint could be fired from a foreign page.
  if (!portalMode)
    p += F("<div class=row>"
           "<form method=POST action=/scan><button>Scan WiFi</button></form>"
           "<form method=POST action=/rehome><button>Re-home drums</button></form>"
           "<form method=POST action=/forget><button>Erase settings</button></form>"
           "</div>");

  // The phone pulls the full IANA -> POSIX table and splits it into region and
  // city, because 450 entries in one dropdown is unusable on a phone. Done in
  // the browser, not on the board. Without internet - in portal mode, for
  // instance - the flat preset list stays as it is and keeps the tz name. The
  // current value is read out of the select, never interpolated into the script.
  p += F("<script>"
         "const s=document.getElementById('tz'),pick=document.getElementById('tzpick'),"
         "reg=document.getElementById('tzregion'),city=document.getElementById('tzcity'),"
         "val=document.getElementById('tzval'),info=document.getElementById('tzinfo');"
         "const CUR=s.value,MINE=Intl.DateTimeFormat().resolvedOptions().timeZone;"
         "let T={};"
         // Show the local time of the selected zone. IANA names one city per
         // zone, so the one you live in usually is not on the list - the clock
         // reading back is what tells you the choice is right.
         "const note=()=>{val.value=city.value;"
         "const c=(city.selectedOptions[0]||{}).text||'';"
         "const z=reg.value==='Other'?c.replace(/ /g,'_'):reg.value+'/'+c.replace(/ /g,'_');"
         "let s=z.replace(/_/g,' ');"
         "try{s+=' — '+new Intl.DateTimeFormat([],{timeZone:z,hour:'2-digit',"
         "minute:'2-digit',timeZoneName:'short'}).format();}catch(e){}"
         "info.textContent=s+(MINE?'   ·   phone: '+MINE.replace(/_/g,' '):'');};"
         // many zones share one POSIX string, so select by name, not by value
         "const cities=(r,name)=>{city.innerHTML='';"
         "for(const [n,v] of T[r])city.add(new Option(n,v));"
         "if(name)for(const o of city.options)if(o.text===name){o.selected=true;break;}"
         "if(city.selectedIndex<0)city.selectedIndex=0;note();};"
         "reg.onchange=()=>cities(reg.value);city.onchange=note;"
         "fetch('https://cdn.jsdelivr.net/gh/nayarsystems/posix_tz_db@master/zones.json')"
         ".then(r=>r.json()).then(z=>{"
         "for(const k in z){const i=k.indexOf('/');"
         "const r=i<0?'Other':k.slice(0,i),c=i<0?k:k.slice(i+1);"
         "(T[r]=T[r]||[]).push([c.replace(/_/g,' '),z[k]]);}"
         // map the stored string back to a zone name; the factory default is
         // not a choice, so the phone wins over it
         "let name=null;for(const k in z)if(z[k]===CUR){name=k;break;}"
         "if((!name||CUR==='UTC0')&&z[MINE])name=MINE;"
         "if(!name)name='UTC';"
         "const i=name.indexOf('/'),r=i<0?'Other':name.slice(0,i);"
         "for(const g of Object.keys(T).sort())reg.add(new Option(g,g));"
         "reg.value=r;cities(r,(i<0?name:name.slice(i+1)).replace(/_/g,' '));"
         "s.removeAttribute('name');s.hidden=true;val.name='tz';pick.hidden=false;"
         "}).catch(()=>{});</script>");

  return p;
}

// ------------------------------------------------------------- handlers

// In the portal the WPA2 passphrase is the gate already, and a login prompt
// there tends to confuse captive portal browsers. On a real network anyone
// could reach the page, so ask for credentials.
// Basic auth alone does not stop a cross site POST: a foreign page can submit
// a form here and the browser attaches the credentials by itself. A form post
// from somewhere else carries that origin in Origin or Referer.
static bool crossSite()
{
  String from = server.header("Origin");
  if (from.length() == 0) from = server.header("Referer");
  if (from.length() == 0) return false;    // curl and friends, no browser context

  return from.indexOf(server.hostHeader()) < 0;
}

static bool denied()
{
  if (server.method() == HTTP_POST && crossSite()) {
    server.send(403, "text/plain", "cross site request blocked");
    return true;
  }

  if (portalMode) return false;
  if (server.authenticate(UI_USER, UI_PASS)) return false;

  server.requestAuthentication();
  return true;
}

static void sendNotice(const char *text)
{
  String p = F("<!doctype html><meta name=viewport content='width=device-width,initial-scale=1'>"
               "<title>Flip Clock</title>"
               "<body style='font:16px system-ui;background:#111;color:#eee;padding:24px'><p>");
  p += text;
  p += F("</p></body>");
  server.send(200, "text/html", p);
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

  sendNotice("Settings erased. Restarting into setup mode.");
  delay(500);
  ESP.restart();
}

// POSIX timezone strings are fed to setenv, and this one also ends up inside
// the page. Keep it to the characters the format actually uses.
static bool validTz(const String &s)
{
  if (s.length() == 0 || s.length() > 48) return false;

  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    // <> are part of the format: modern tzdata writes numeric abbreviations
    // that way, as in "<+05>-5" for Almaty. Safe here, since the string is
    // escaped on output and never reaches the script.
    if (!isalnum((unsigned char)c) && strchr("+-,./:<>", c) == nullptr) return false;
  }
  return true;
}

static void handleSave()
{
  if (denied()) return;

  if (server.hasArg("ssid")) cfg.ssid = server.arg("ssid");

  // An empty password field means "keep the current one", so clearing it needs
  // its own switch - otherwise moving to an open network is impossible.
  if (server.hasArg("nopass"))          cfg.pass = "";
  else if (server.arg("pass").length()) cfg.pass = server.arg("pass");

  if (server.hasArg("tz")) {
    String tz = server.arg("tz");
    if (!validTz(tz)) { sendNotice("Bad timezone string."); return; }
    cfg.tz = tz;
  }

  if (server.hasArg("fmt"))  cfg.fmt12      = server.arg("fmt") == "12";
  if (server.hasArg("offh")) cfg.offHours   = server.arg("offh").toFloat();
  if (server.hasArg("offm")) cfg.offMinutes = server.arg("offm").toFloat();

  saveConfig();

  sendNotice("Saved. Restarting.");
  delay(500);
  ESP.restart();
}

static void handleRehome()
{
  if (denied()) return;

  server.sendHeader("Location", "/");
  server.send(303);
  hours.lastFail = minutes.lastFail = 0;   // an explicit request overrides the backoff
  homeDrum(hours);
  homeDrum(minutes);
}

// On a working network the list is not refreshed per request - scanning blocks
// for seconds and drops the link off channel. Do it when asked instead.
static void handleScan()
{
  if (denied()) return;

  scanNetworks();
  server.sendHeader("Location", "/");
  server.send(303);
}

static void handleNotFound()
{
  if (portalMode) { handleRoot(); return; }   // captive portal: everything is the form
  server.send(404, "text/plain", "not found");
}

// ---------------------------------------------------------------- setup

void startServer()
{
  static const char *csrfHeaders[] = {"Origin", "Referer"};
  server.collectHeaders(csrfHeaders, 2);

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/rehome", HTTP_POST, handleRehome);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/scan", HTTP_POST, handleScan);
  server.onNotFound(handleNotFound);
  server.begin();
}

// AP_STA, not AP: the portal is up for setup, but the station side keeps
// retrying underneath. A router that was simply down at boot will be picked
// up on its own, without anyone walking over with a phone.
void startPortal()
{
  portalMode = true;

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_NAME, AP_PASS);      // WPA2, so the typed in password is not broadcast
  dns.start(53, "*", WiFi.softAPIP());

  scanNetworks();
  if (cfg.ssid.length()) WiFi.begin(cfg.ssid.c_str(), cfg.pass.c_str());

  startServer();

  Serial.printf("portal: connect to \"%s\" with password \"%s\", open http://%s\n",
                AP_NAME, AP_PASS, WiFi.softAPIP().toString().c_str());
}

void stopPortal()
{
  if (!portalMode) return;

  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  portalMode = false;

  Serial.println("portal closed");
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
