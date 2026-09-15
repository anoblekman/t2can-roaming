#include "fsd_trace.h"
#include "fsd_signals.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* 남은 공간에만 쓴다. 잘릴 바에는 아예 안 쓴다. */
static void put(char* out, size_t cap, size_t* n, const char* fmt, ...)
    __attribute__((format(printf, 4, 5)));

static void put(char* out, size_t cap, size_t* n, const char* fmt, ...) {
    if (cap == 0 || *n >= cap) return;
    va_list ap;
    va_start(ap, fmt);
    int wrote = vsnprintf(out + *n, cap - *n, fmt, ap);
    va_end(ap);
    if (wrote < 0) { out[*n] = 0; return; }
    if ((size_t)wrote >= cap - *n) { out[*n] = 0; return; }
    *n += (size_t)wrote;
}

#define TS   "[%4lu.%03lu] "
#define TSA(ms) (unsigned long)((ms) / 1000u), (unsigned long)((ms) % 1000u)

void fsd_trace_init(TraceSnap* snap) { memset(snap, 0, sizeof(*snap)); }

size_t fsd_trace_step(const FSDState* s, TraceSnap* snap, uint32_t now_ms,
                      char* out, size_t cap) {
    size_t n = 0;
    if (cap) out[0] = 0;

    GateReason apr = fsd_ap_ok_why(s, now_ms);
    GateReason smr = fsd_summon_ok_why(s, now_ms);
    bool stationary = s->di_speed_raw == DI_SPEED_STATIONARY_RAW;
    if (!snap->primed) snap->speed_raw = s->di_speed_raw;   /* 속도 기준선 */

    if (snap->primed) {
        /* 마커 먼저 — 눈에 띄어야 한다 */
        if (s->summon_fwd_leash && !snap->fwd_leash)
            put(out, cap, &n, TS "*** LEASH_FWD 0->1  서먼 전진 거리 제한 도달\n", TSA(now_ms));
        if (s->summon_rvs_leash && !snap->rvs_leash)
            put(out, cap, &n, TS "*** LEASH_RVS 0->1  서먼 후진 거리 제한 도달\n", TSA(now_ms));
        if (snap->ap_state == 2u && s->das_ap_state < 2u)
            put(out, cap, &n, TS "*** AP_OFFER_LOST   das_ap_state 2 -> %u\n",
                TSA(now_ms), (unsigned)s->das_ap_state);

        /* 상태 전이 */
        if (s->das_ap_state != snap->ap_state)
            put(out, cap, &n, TS "das_ap_state     %u -> %u\n",
                TSA(now_ms), (unsigned)snap->ap_state, (unsigned)s->das_ap_state);
        if (s->das_ap_state_b1 != snap->ap_state_b1)
            put(out, cap, &n, TS "ap_state_b1      %u -> %u\n",
                TSA(now_ms), (unsigned)snap->ap_state_b1, (unsigned)s->das_ap_state_b1);
        if (s->das_hands_on != snap->hands_on)
            put(out, cap, &n, TS "das_hands_on     %u -> %u\n",
                TSA(now_ms), (unsigned)snap->hands_on, (unsigned)s->das_hands_on);
        if (s->das_lane_change != snap->lane_change)
            put(out, cap, &n, TS "lane_change      %u -> %u\n",
                TSA(now_ms), (unsigned)snap->lane_change, (unsigned)s->das_lane_change);
        if (s->das_speed_limit != snap->speed_limit)
            put(out, cap, &n, TS "speed_limit      %u -> %u\n",
                TSA(now_ms), (unsigned)snap->speed_limit, (unsigned)s->das_speed_limit);
        if (s->summon_available != snap->summon_available)
            put(out, cap, &n, TS "summon_available %u -> %u\n",
                TSA(now_ms), (unsigned)snap->summon_available, (unsigned)s->summon_available);
        if (s->summon_obstacle != snap->summon_obstacle)
            put(out, cap, &n, TS "summon_obstacle  %u -> %u\n",
                TSA(now_ms), (unsigned)snap->summon_obstacle, (unsigned)s->summon_obstacle);
        if (s->autopark_ready != snap->autopark_ready)
            put(out, cap, &n, TS "autopark_ready   %u -> %u\n",
                TSA(now_ms), (unsigned)snap->autopark_ready, (unsigned)s->autopark_ready);
        if (s->auto_parked != snap->auto_parked)
            put(out, cap, &n, TS "auto_parked      %u -> %u\n",
                TSA(now_ms), (unsigned)snap->auto_parked, (unsigned)s->auto_parked);
        if (s->summoning != snap->summoning)
            put(out, cap, &n, TS "summoning        %u -> %u\n",
                TSA(now_ms), (unsigned)snap->summoning, (unsigned)s->summoning);
        if (s->ota_in_progress != snap->ota)
            put(out, cap, &n, TS "tesla_ota        %u -> %u\n",
                TSA(now_ms), (unsigned)snap->ota, (unsigned)s->ota_in_progress);
        if (s->di_gear != snap->gear)
            put(out, cap, &n, TS "di_gear          %u -> %u\n",
                TSA(now_ms), (unsigned)snap->gear, (unsigned)s->di_gear);
        if (stationary != snap->stationary)
            put(out, cap, &n, TS "stationary       %u -> %u\n",
                TSA(now_ms), (unsigned)snap->stationary, (unsigned)stationary);
        /* 속도 원본값: 매 프레임 변하므로 40 raw(약 3.2km/h) 이상 바뀔 때만 기록해
           로그 폭주를 막는다. SNA(4095)↔유효 전환은 폭 안에서 항상 잡힌다. */
        {
            int diff = (int)s->di_speed_raw - (int)snap->speed_raw;
            if (diff < 0) diff = -diff;
            if (diff >= 40) {
                put(out, cap, &n, TS "di_speed_raw     %u -> %u\n",
                    TSA(now_ms), (unsigned)snap->speed_raw, (unsigned)s->di_speed_raw);
                snap->speed_raw = s->di_speed_raw;   /* 마지막 기록값 기준 누적 */
            }
        }

        /* 게이트 판정 */
        if ((uint8_t)apr != snap->ap_reason)
            put(out, cap, &n, TS "ap_ok      %-5s %s\n", TSA(now_ms),
                apr == GATE_OK ? "true" : "false", fsd_gate_reason_name(apr));
        if ((uint8_t)smr != snap->summon_reason)
            put(out, cap, &n, TS "summon_ok  %-5s %s\n", TSA(now_ms),
                smr == GATE_OK ? "true" : "false", fsd_gate_reason_name(smr));
    }

    snap->primed           = true;
    snap->ap_state         = s->das_ap_state;
    snap->ap_state_b1      = s->das_ap_state_b1;
    snap->hands_on         = s->das_hands_on;
    snap->lane_change      = s->das_lane_change;
    snap->speed_limit      = s->das_speed_limit;
    snap->summon_available = s->summon_available;
    snap->fwd_leash        = s->summon_fwd_leash;
    snap->rvs_leash        = s->summon_rvs_leash;
    snap->summon_obstacle  = s->summon_obstacle;
    snap->autopark_ready   = s->autopark_ready;
    snap->auto_parked      = s->auto_parked;
    snap->ota              = s->ota_in_progress;
    snap->summoning        = s->summoning;
    snap->gear             = s->di_gear;
    snap->stationary       = stationary;
    snap->ap_reason        = (uint8_t)apr;
    snap->summon_reason    = (uint8_t)smr;
    return n;
}
