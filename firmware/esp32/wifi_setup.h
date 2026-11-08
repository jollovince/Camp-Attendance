#pragma once
#include <WebServer.h>
#include <DNSServer.h>

WebServer wifiSetupServer(80);
DNSServer wifiSetupDns;
Preferences wifiSettings;
String stationSsid, stationPassword, setupSsid;
bool wifiSettingsReady = false;
bool setupActive = false;
bool credentialsPending = false;
bool wifiCredentialsNeedSave = false;
unsigned long wifiSaveRetryAt = 0;
struct SavedWifiCredentials {
  uint32_t version;
  char ssid[33];
  char password[64];
};
unsigned long wifiAttemptStarted = 0;
unsigned long wifiRetryAt = 0;
unsigned long portalCloseAt = 0;
constexpr unsigned long WIFI_ATTEMPT_MS = 30000;

String escapeWifiHtml(String value) {
  value.replace("&", "&amp;");
  value.replace("<", "&lt;");
  value.replace(">", "&gt;");
  value.replace("\"", "&quot;");
  value.replace("'", "&#39;");
  return value;
}

void scanAvailableWifi() {
  wifiSetupServer.sendHeader("Cache-Control", "no-store");
  if (credentialsPending || WiFi.status() == WL_CONNECTED) {
    wifiSetupServer.send(409, "text/plain", "Connection in progress or already connected.");
    return;
  }
  const int count = WiFi.scanComplete();
  if (count == WIFI_SCAN_RUNNING) {
    wifiSetupServer.send(202, "text/plain", "Scanning nearby Wi-Fi...");
    return;
  }
  if (count == WIFI_SCAN_FAILED) {
    const int result = WiFi.scanNetworks(true);
    wifiSetupServer.send(result == WIFI_SCAN_FAILED ? 503 : 202, "text/plain",
      result == WIFI_SCAN_FAILED ? "Scan failed. Press Refresh Wi-Fi to try again." : "Scanning nearby Wi-Fi...");
    return;
  }
  String options = "<option value=''>Choose available Wi-Fi</option>";
  int visibleCount = 0;
  // Bound the response and collapse multiple access points with the same SSID.
  for (int i = 0; i < count && visibleCount < 40; i++) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty()) continue;
    bool duplicate = false;
    for (int j = 0; j < i; j++) if (WiFi.SSID(j) == ssid) { duplicate = true; break; }
    if (duplicate) continue;
    const String escaped = escapeWifiHtml(ssid);
    options += "<option value=\"" + escaped + "\">" + escaped + " (" + String(WiFi.RSSI(i)) + " dBm, " +
      (WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "open" : "secured") + ")</option>";
    visibleCount++;
  }
  wifiSetupServer.send(200, "text/html; charset=utf-8", options);
  WiFi.scanDelete();
}

void showWifiSetup() {
  wifiSetupServer.sendHeader("Cache-Control", "no-store");
  wifiSetupServer.send(200, "text/html", R"HTML(<!doctype html><html><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Camp Wi-Fi setup</title>
<style>body{font:18px sans-serif;max-width:420px;margin:40px auto;padding:20px}input,button,select{box-sizing:border-box;width:100%;padding:12px;margin:8px 0}</style></head>
<body><h1>Camp Wi-Fi setup</h1><p>Enter your 2.4 GHz Wi-Fi details. If connection fails, reconnect to this hotspot and try again.</p>
<label>Available Wi-Fi<select id="networks"><option value="">Scanning...</option></select></label>
<button type="button" id="refresh">Refresh Wi-Fi</button><p id="scan-status" role="status"></p>
<form action="/connect" method="post"><label>Wi-Fi name<input id="ssid" name="ssid" maxlength="32" required></label>
<label>Password<input name="password" type="password" maxlength="63"></label><button>Connect and save</button></form>
<p>For hidden Wi-Fi, enter the name manually.</p><p><a href="/status">Connection status</a></p>
<script>
const networks=document.getElementById('networks'),refresh=document.getElementById('refresh'),statusText=document.getElementById('scan-status');
networks.onchange=()=>{if(networks.value)document.getElementById('ssid').value=networks.value;};
async function scan(){
 refresh.disabled=true;statusText.textContent='Scanning nearby Wi-Fi...';
 try{for(let attempt=0;attempt<20;attempt++){
  const response=await fetch('/scan',{cache:'no-store'}),body=await response.text();
  if(response.status===202){await new Promise(resolve=>setTimeout(resolve,1000));continue;}
  if(!response.ok)throw new Error(body);
  networks.innerHTML=body;
  statusText.textContent=networks.options.length>1?'Select a network, then enter its password.':'No Wi-Fi found. Refresh or enter the name manually.';
  return;
 }throw new Error('Scan timed out. Press Refresh Wi-Fi to retry.');
 }catch(error){statusText.textContent=error.message||'Unable to scan. Refresh to retry.';}
 finally{refresh.disabled=false;}
}
refresh.onclick=scan;scan();
</script></body></html>)HTML");
}

void startWifiSetup() {
  if (setupActive) return;
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(setupSsid.c_str(), "campwifi123")) {
    Serial.println("Wi-Fi setup hotspot failed; retrying later.");
    return;
  }
  setupActive = true;
  wifiSetupDns.start(53, "*", WiFi.softAPIP());
  wifiSetupServer.begin();
  Serial.printf("Wi-Fi setup hotspot: %s / password: campwifi123 / http://%s\n",
    setupSsid.c_str(), WiFi.softAPIP().toString().c_str());
}

void beginWifiConnection() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(false);
  wifiSettingsReady = wifiSettings.begin("camp-wifi", false);
  stationSsid = wifiSettingsReady ? wifiSettings.getString("ssid", WIFI_SSID) : String(WIFI_SSID);
  stationPassword = wifiSettingsReady ? wifiSettings.getString("password", WIFI_PASSWORD) : String(WIFI_PASSWORD);
  SavedWifiCredentials saved{};
  const bool savedConfig = wifiSettingsReady &&
    wifiSettings.getBytesLength("config-v1") == sizeof(saved) &&
    wifiSettings.getBytes("config-v1", &saved, sizeof(saved)) == sizeof(saved) &&
    saved.version == 1 && saved.ssid[0] && saved.ssid[32] == '\0' && saved.password[63] == '\0';
  if (savedConfig) {
    stationSsid = saved.ssid;
    stationPassword = saved.password;
  }
  // Migrate legacy settings/default credentials after a successful connection.
  wifiCredentialsNeedSave = !savedConfig;
  Serial.println(savedConfig ? "Saved Wi-Fi loaded; reconnecting automatically."
    : "Trying existing Wi-Fi settings; successful connection will be saved.");
  if (!wifiSettingsReady) Serial.println("ERROR: Wi-Fi settings storage could not be opened.");
  setupSsid = "Camp-Setup-" + WiFi.macAddress().substring(12);
  setupSsid.replace(":", "");
  wifiSetupServer.on("/", HTTP_GET, showWifiSetup);
  wifiSetupServer.on("/scan", HTTP_GET, scanAvailableWifi);
  wifiSetupServer.on("/status", HTTP_GET, []() {
    wifiSetupServer.sendHeader("Cache-Control", "no-store");
    wifiSetupServer.send(200, "text/plain", WiFi.status() == WL_CONNECTED
      ? (wifiCredentialsNeedSave ? "Connected, but Wi-Fi settings are not saved yet. Keep the ESP32 powered while it retries."
        : "Connected and saved. The hotspot will close shortly. You may return to your usual Wi-Fi.")
      : credentialsPending ? "Connecting. Check again in 30 seconds."
      : "Not connected. Open http://192.168.4.1/ to enter Wi-Fi details.");
  });
  wifiSetupServer.on("/connect", HTTP_POST, []() {
    const String ssid = wifiSetupServer.arg("ssid");
    const String password = wifiSetupServer.arg("password");
    if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63 ||
        (!password.isEmpty() && password.length() < 8)) {
      wifiSetupServer.send(400, "text/plain", "Enter a Wi-Fi name (1-32 bytes) and password (8-63 bytes, or empty for open Wi-Fi).");
      return;
    }
    stationSsid = ssid;
    stationPassword = password;
    portalCloseAt = 0;
    wifiSetupServer.send(200, "text/html", "<p>Connecting. If disconnected, reconnect to the setup hotspot.</p><a href='/status'>Check connection status</a>");
    WiFi.disconnect();
    WiFi.begin(stationSsid.c_str(), stationPassword.c_str());
    credentialsPending = true;
    wifiCredentialsNeedSave = true;
    wifiSaveRetryAt = 0;
    wifiAttemptStarted = millis();
    wifiRetryAt = wifiAttemptStarted + WIFI_ATTEMPT_MS;
  });
  wifiSetupServer.onNotFound(showWifiSetup);
  wifiAttemptStarted = millis();
  wifiRetryAt = wifiAttemptStarted + WIFI_ATTEMPT_MS;
  if (!stationSsid.isEmpty()) WiFi.begin(stationSsid.c_str(), stationPassword.c_str());
  else startWifiSetup();
}

void pollWifiConnection() {
  if (setupActive) {
    wifiSetupDns.processNextRequest();
    wifiSetupServer.handleClient();
  }
  // The POST handler can set wifiAttemptStarted. Sample time AFTER handling it:
  // an earlier timestamp would underflow and falsely time out the new attempt.
  const unsigned long now = millis();
  if (WiFi.status() == WL_CONNECTED) {
    wifiAttemptStarted = now;
    wifiRetryAt = now + WIFI_ATTEMPT_MS;
    credentialsPending = false;
    if (wifiCredentialsNeedSave && (!wifiSaveRetryAt || (long)(now - wifiSaveRetryAt) >= 0)) {
      SavedWifiCredentials next{};
      next.version = 1;
      strlcpy(next.ssid, stationSsid.c_str(), sizeof(next.ssid));
      strlcpy(next.password, stationPassword.c_str(), sizeof(next.password));
      // One committed blob keeps the network name and password together.
      if (wifiSettingsReady && wifiSettings.putBytes("config-v1", &next, sizeof(next)) == sizeof(next)) {
        Serial.println("Wi-Fi connected; credentials saved.");
        wifiCredentialsNeedSave = false;
      } else {
        Serial.println("Wi-Fi connected, but credentials could not be saved. Retrying in 10 seconds.");
        wifiSaveRetryAt = now + 10000;
      }
    }
    if (setupActive) {
      if (wifiCredentialsNeedSave) { portalCloseAt = 0; return; }
      if (!portalCloseAt) portalCloseAt = now + 15000;
      if ((long)(now - portalCloseAt) >= 0) {
        wifiSetupServer.stop();
        wifiSetupDns.stop();
        WiFi.scanDelete();
        WiFi.softAPdisconnect(true);
        WiFi.mode(WIFI_STA);
        setupActive = false;
        portalCloseAt = 0;
        Serial.println("Wi-Fi connected; setup hotspot closed.");
      }
    }
    return;
  }
  portalCloseAt = 0;
  if (now - wifiAttemptStarted >= WIFI_ATTEMPT_MS) {
    credentialsPending = false;
    startWifiSetup();
  }
  if ((long)(now - wifiRetryAt) >= 0) {
    wifiRetryAt = now + WIFI_ATTEMPT_MS;
    if (!stationSsid.isEmpty() && WiFi.scanComplete() != WIFI_SCAN_RUNNING)
      WiFi.begin(stationSsid.c_str(), stationPassword.c_str());
  }
}
