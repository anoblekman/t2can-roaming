#include "prefs.h"
#include <Preferences.h>

static Preferences g_p;

void prefs_begin() { g_p.begin("t2c", false); }

void prefs_load(FSDState* s) {
    /* active 를 NVS 에서 복원(전원 껐다 켜도 마지막 모드 유지). 재부팅 후 자동
       주입 재개의 위험은 OTA-리슨-전용 게이트(web_ui: 활성 중 OTA 거부)로 막는다.
       즉 위험한 "활성 중 OTA 재부팅" 경로 자체가 불가능하므로 부팅=리슨 강제는 불필요.
       (첫 부팅/미설정 시 기본값 false = 리슨.) */
    s->active        = g_p.getBool("act",  false);
    s->nag_kill      = g_p.getBool("nag",  true);
    s->wave          = (NagWave)g_p.getUChar("wave", 0);
    s->summon_unlock = g_p.getBool("smn",  false);
    s->x3f8_exp      = g_p.getUChar("x3f8", 0);   /* 0x3F8 실험 마스크(기본 off) */
}

void prefs_save(const FSDState* s) {
    g_p.putBool ("act",  s->active);
    g_p.putBool ("nag",  s->nag_kill);
    g_p.putUChar("wave", (uint8_t)s->wave);
    g_p.putBool ("smn",  s->summon_unlock);
    g_p.putUChar("x3f8", s->x3f8_exp);
}

void prefs_reset() { g_p.clear(); }

String prefs_ap_ssid()  { return g_p.getString("apssid", "T2CAN-ROAMING"); }
String prefs_ap_pw()    { return g_p.getString("appw",   "passwd");        }

void prefs_set_net(const String& ssid, const String& ap_pw) {
    g_p.putString("apssid", ssid);
    g_p.putString("appw",   ap_pw);
}
