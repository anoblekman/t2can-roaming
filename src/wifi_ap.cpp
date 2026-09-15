#include "wifi_ap.h"
#include "prefs.h"
#include <WiFi.h>

void wifi_ap_start() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(prefs_ap_ssid().c_str(), prefs_ap_pw().c_str());
    Serial.printf("[AP] %s  http://%s\n", prefs_ap_ssid().c_str(),
                  WiFi.softAPIP().toString().c_str());
}
