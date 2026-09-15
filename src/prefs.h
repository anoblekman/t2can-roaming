#pragma once
#include <Arduino.h>
extern "C" {
#include "fsd_state.h"
}

void   prefs_begin();
void   prefs_load(FSDState* s);
void   prefs_save(const FSDState* s);
void   prefs_reset();
String prefs_ap_ssid();
String prefs_ap_pw();
void   prefs_set_net(const String& ssid, const String& ap_pw);
