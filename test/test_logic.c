#include <stdio.h>
#include <string.h>
#include "fsd_checksum.h"
#include "fsd_signals.h"
#include "fsd_state.h"
#include "fsd_das.h"
#include "fsd_gate.h"
#include "fsd_apctl.h"
#include "fsd_nag.h"
#include "fsd_trace.h"
#include "fsd_x3f8.h"

static int g_fail = 0;
#define CHECK(cond, msg)                                        \
    do {                                                        \
        if (!(cond)) { printf("FAIL: %s\n", (msg)); g_fail++; }  \
    } while (0)

static CanFrame mk(uint32_t id, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                   uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7) {
    CanFrame f;
    f.id = id; f.dlc = 8;
    f.data[0]=b0; f.data[1]=b1; f.data[2]=b2; f.data[3]=b3;
    f.data[4]=b4; f.data[5]=b5; f.data[6]=b6; f.data[7]=b7;
    return f;
}

static void test_das_real_frame(void) {
    FSDState s; fsd_state_init(&s);
    /* 실차 캡처: 01 0A DF E0 B0 08 xx xx */
    CanFrame f = mk(ID_DAS_STATUS_B, 0x01,0x0A,0xDF,0xE0,0xB0,0x08,0x00,0x00);
    CHECK(fsd_das_handle(&s, &f, 1000) == true, "das: handled");
    CHECK(s.das_ap_state == 1, "das: ap_state 1");
    CHECK(s.das_speed_limit == 10, "das: speed_limit raw 10 (=50kph)");
    CHECK(s.das_hands_on == 2, "das: hands_on 2");
    CHECK(s.das_ap_state_b1 == 0x0A >> 4, "das: b1 nibble observed (0x0A byte1 -> 0)");
    CHECK(s.das_seen == true, "das: seen");
    CHECK(s.das_39b_seen == true, "das: 39b seen");
    CHECK(s.das_seen_ms == 1000, "das: timestamp");
}

static void test_das_summon_bits(void) {
    FSDState s; fsd_state_init(&s);
    /* byte1 bit6=obstacle bit7=gate, byte3 bit0=ready bit1=parked
       bit4=fwdleash bit5=rvsleash, byte6 bit3=available */
    CanFrame f = mk(ID_DAS_STATUS_B, 0x00,0xC0,0x00,0x33,0x00,0x00,0x08,0x00);
    fsd_das_handle(&s, &f, 100);
    CHECK(s.summon_obstacle == true, "das: obstacle");
    CHECK(s.summon_cleared_gate == true, "das: cleared gate");
    CHECK(s.autopark_ready == true, "das: autopark ready");
    CHECK(s.auto_parked == true, "das: auto parked");
    CHECK(s.summon_fwd_leash == true, "das: fwd leash");
    CHECK(s.summon_rvs_leash == true, "das: rvs leash");
    CHECK(s.summon_available == true, "das: summon available");
}

static void test_das_source_priority(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame b = mk(ID_DAS_STATUS_B, 0x03,0,0,0,0,0,0,0);
    CanFrame a = mk(ID_DAS_STATUS_A, 0x01,0,0,0,0,0,0,0);
    fsd_das_handle(&s, &b, 100);
    CHECK(s.das_ap_state == 3, "prio: 39B applied");
    CHECK(fsd_das_handle(&s, &a, 200) == false, "prio: 399 rejected after 39B");
    CHECK(s.das_ap_state == 3, "prio: 399 did not overwrite");
}

static void test_das_399_when_no_39b(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame a = mk(ID_DAS_STATUS_A, 0x02,0,0,0,0,0,0,0);
    CHECK(fsd_das_handle(&s, &a, 50) == true, "399: accepted alone");
    CHECK(s.das_ap_state == 2, "399: ap_state 2");
}

static void test_das_engage_timestamp(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame f1 = mk(ID_DAS_STATUS_B, 0x02,0,0,0,0,0,0,0);
    CanFrame f3 = mk(ID_DAS_STATUS_B, 0x03,0,0,0,0,0,0,0);
    fsd_das_handle(&s, &f1, 100);
    CHECK(s.ap_engaged_since_ms == 0, "engage: not engaged yet");
    fsd_das_handle(&s, &f3, 500);
    CHECK(s.ap_engaged_since_ms == 500, "engage: stamped on rising edge");
    fsd_das_handle(&s, &f3, 900);
    CHECK(s.ap_engaged_since_ms == 500, "engage: not restamped while held");
    fsd_das_handle(&s, &f1, 1200);
    CHECK(s.ap_engaged_since_ms == 0, "engage: cleared on drop");

    /* 잔소리 결합 = 3~5. 6(자동주차)·8(중단)은 결합 아님 → 타이머 리셋, 복귀 시 재스탬프 */
    CanFrame f6 = mk(ID_DAS_STATUS_B, 0x06,0,0,0,0,0,0,0);
    CanFrame f8 = mk(ID_DAS_STATUS_B, 0x08,0,0,0,0,0,0,0);
    fsd_das_handle(&s, &f3, 2000);
    CHECK(s.ap_engaged_since_ms == 2000, "engage: stamped again at 3");
    fsd_das_handle(&s, &f8, 2500);
    CHECK(s.ap_engaged_since_ms == 0, "engage: abort 8 clears timer");
    fsd_das_handle(&s, &f3, 2600);
    CHECK(s.ap_engaged_since_ms == 2600, "engage: 8 -> 3 restamps (no instant stable)");
    fsd_das_handle(&s, &f6, 3000);
    CHECK(s.ap_engaged_since_ms == 0, "engage: 6 (autopark) clears timer");
}

static void test_das_short_frame_ignored(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame f = mk(ID_DAS_STATUS_B, 0x03,0,0,0,0,0,0,0);
    f.dlc = 7;
    CHECK(fsd_das_handle(&s, &f, 100) == false, "das: dlc<8 rejected");
    CHECK(s.das_seen == false, "das: not marked seen");
}

static void test_aux_selfpark_and_aca(void) {
    FSDState s; fsd_state_init(&s);
    CHECK(s.self_park_req == 0, "aux: self_park_req init 0");
    CHECK(s.di_aca == false, "aux: di_aca init false");

    /* selfParkRequest = 0x3F8 byte3 상위니블 */
    CanFrame p7 = mk(ID_UI_DRIVERASSIST, 0,0,0,0x70,0,0,0,0);
    CHECK(fsd_aux_handle(&s, &p7, 0) == true, "aux: 0x3F8 handled");
    CHECK(s.self_park_req == 7, "aux: byte3 0x70 -> 7");
    CanFrame p11 = mk(ID_UI_DRIVERASSIST, 0,0,0,0xB0,0,0,0,0);
    fsd_aux_handle(&s, &p11, 0);
    CHECK(s.self_park_req == 11, "aux: byte3 0xB0 -> 11");
    CanFrame p0 = mk(ID_UI_DRIVERASSIST, 0,0,0,0x00,0,0,0,0);
    fsd_aux_handle(&s, &p0, 0);
    CHECK(s.self_park_req == 0, "aux: byte3 0x00 -> 0");

    /* ACA = 0x118 byte6 bit2 */
    CanFrame aset = mk(ID_DI_SYSTEMSTATUS, 0,0,0,0,0,0,0x04,0);
    CHECK(fsd_aux_handle(&s, &aset, 0) == true, "aux: 0x118 handled");
    CHECK(s.di_aca == true, "aux: byte6 bit2 set -> true");
    CanFrame aclr = mk(ID_DI_SYSTEMSTATUS, 0,0,0,0,0,0,0x00,0);
    fsd_aux_handle(&s, &aclr, 0);
    CHECK(s.di_aca == false, "aux: byte6 bit2 clear -> false");

    /* dlc 부족 시 무시 */
    FSDState s2; fsd_state_init(&s2);
    CanFrame sp = mk(ID_UI_DRIVERASSIST, 0,0,0,0x70,0,0,0,0); sp.dlc = 3;
    CHECK(fsd_aux_handle(&s2, &sp, 0) == false, "aux: 0x3F8 dlc<4 ignored");
    CHECK(s2.self_park_req == 0, "aux: dlc<4 leaves self_park_req 0");
    CanFrame ac = mk(ID_DI_SYSTEMSTATUS, 0,0,0,0,0,0,0x04,0); ac.dlc = 6;
    CHECK(fsd_aux_handle(&s2, &ac, 0) == false, "aux: 0x118 dlc<7 ignored");
    CHECK(s2.di_aca == false, "aux: dlc<7 leaves di_aca false");

    /* 무관 ID 는 false */
    CanFrame other = mk(ID_DAS_STATUS_B, 0,0,0,0,0,0,0,0);
    CHECK(fsd_aux_handle(&s2, &other, 0) == false, "aux: unrelated id -> false");
}

static void feed_ota(FSDState* s, uint8_t b6, int n) {
    CanFrame f = mk(ID_GTW_CAR_STATE, 0,0,0,0,0,0,b6,0);
    for (int i = 0; i < n; i++) fsd_ota_handle(s, &f);
}

static void test_ota_rolling_counter_never_trips(void) {
    FSDState s; fsd_state_init(&s);
    for (int i = 0; i < 50; i++) { feed_ota(&s, 0x01, 1); feed_ota(&s, 0x03, 1); }
    CHECK(s.ota_in_progress == false, "ota: value 1/3 never asserts");
}

static void test_ota_assert_after_three(void) {
    FSDState s; fsd_state_init(&s);
    feed_ota(&s, 0x02, 2);
    CHECK(s.ota_in_progress == false, "ota: 2 frames not enough");
    feed_ota(&s, 0x02, 1);
    CHECK(s.ota_in_progress == true, "ota: 3 frames asserts");
}

static void test_ota_clear_after_six(void) {
    FSDState s; fsd_state_init(&s);
    feed_ota(&s, 0x02, 3);
    CHECK(s.ota_in_progress == true, "ota: asserted");
    feed_ota(&s, 0x00, 5);
    CHECK(s.ota_in_progress == true, "ota: 5 clear frames not enough");
    feed_ota(&s, 0x00, 1);
    CHECK(s.ota_in_progress == false, "ota: 6 clear frames clears");
}

static void test_ota_assert_run_resets(void) {
    FSDState s; fsd_state_init(&s);
    feed_ota(&s, 0x02, 2);
    feed_ota(&s, 0x00, 1);
    feed_ota(&s, 0x02, 2);
    CHECK(s.ota_in_progress == false, "ota: interrupted run does not assert");
}

static void test_checksum(void) {
    uint8_t d[7] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07};
    /* 0x370 -> 0x70 + 0x03 = 0x73; sum(1..7) = 28 = 0x1C; 0x73+0x1C = 0x8F */
    CHECK(tesla_additive_checksum(0x370u, d, 7) == 0x8F, "checksum 0x370");
}

static void test_state_init(void) {
    FSDState s;
    fsd_state_init(&s);
    CHECK(s.active == false, "init: active false");
    CHECK(s.nag_kill == true, "init: nag_kill true");
    CHECK(s.summon_unlock == false, "init: summon_unlock false");
    CHECK(s.wave == NAG_BASIC, "init: wave basic");
    CHECK(s.das_seen == false, "init: das_seen false");
    CHECK(s.summoning == false, "init: summoning false");
    CHECK(s.spr_confirmed == false, "init: spr_confirmed false");
    CHECK(s.di_speed_raw == DI_SPEED_SNA_RAW, "init: speed SNA until seen");
    CHECK(s.nag_torq_walk == NAG_TORQUE_CENTER, "init: torq walk centered");
    CHECK(s.nag_prng != 0u, "init: prng seeded nonzero");
}

static void test_can_transmit(void) {
    FSDState s; fsd_state_init(&s);
    CHECK(fsd_can_transmit(&s) == false, "tx: listen mode blocks");
    s.active = true;
    CHECK(fsd_can_transmit(&s) == true, "tx: active allows");
    s.ota_in_progress = true;
    CHECK(fsd_can_transmit(&s) == false, "tx: ota blocks");
}

static void test_ap_ok(void) {
    FSDState s; fsd_state_init(&s);
    s.das_ap_state = 2; s.ap_engaged_since_ms = 0;
    CHECK(fsd_ap_ok(&s, 5000) == false, "ap_ok: state 2 blocked");

    s.das_ap_state = 3; s.ap_engaged_since_ms = 1000;
    CHECK(fsd_ap_ok(&s, 1999) == false, "ap_ok: 999ms not stable yet");
    CHECK(fsd_ap_ok(&s, 2000) == true,  "ap_ok: 1000ms stable");

    s.das_ap_state = 4; s.ap_engaged_since_ms = 1000;
    CHECK(fsd_ap_ok(&s, 3000) == true, "ap_ok: 4 ACTIVE_RESTRICTED allowed");
    s.das_ap_state = 5;
    CHECK(fsd_ap_ok(&s, 3000) == true, "ap_ok: 5 ACTIVE_NAV allowed");

    s.das_ap_state = 1; s.ap_engaged_since_ms = 0;
    CHECK(fsd_ap_ok(&s, 9999) == false, "ap_ok: disengaged blocked");

    /* 자동주차 회귀 (실차 2026-09-12 13:51 / 16:05): 이 차 자동주차 = 6(ACTIVE_FSD).
       타이머가 남아 있어도 6 이면 차단 */
    s.das_ap_state = 6; s.ap_engaged_since_ms = 1000;
    CHECK(fsd_ap_ok(&s, 3000) == false, "ap_ok: 6 (autopark) blocked");
    CHECK(fsd_ap_ok_why(&s, 3000) == GATE_AP_FSD, "ap_ok: why AP_FSD");

    /* 중단/고장/SNA 차단 (abort guard) */
    {
        const uint8_t bad[] = {7u, 8u, 9u, 14u, 15u};
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            s.das_ap_state = bad[i]; s.ap_engaged_since_ms = 1000;
            CHECK(fsd_ap_ok(&s, 3000) == false, "ap_ok: abort/fault/SNA blocked");
            CHECK(fsd_ap_ok_why(&s, 3000) == GATE_AP_INVALID, "ap_ok: why AP_INVALID");
        }
    }
}

static void test_das_engage_at_time_zero(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame f3 = mk(ID_DAS_STATUS_B, 0x03,0,0,0,0,0,0,0);
    fsd_das_handle(&s, &f3, 0);
    CHECK(s.ap_engaged_since_ms != 0u, "engage at t=0 must not write the sentinel");
    CHECK(fsd_ap_ok(&s, 1001) == true, "engage at t=0 still opens the gate at 1001ms");
}

static void test_ap_ok_across_millis_wrap(void) {
    FSDState s; fsd_state_init(&s);
    s.das_ap_state = 3;
    s.ap_engaged_since_ms = 0xFFFFFF00u;      /* 256ms before wrap */
    CHECK(fsd_ap_ok(&s, 0x000002E7u) == false, "wrap: 999ms elapsed, not stable");
    CHECK(fsd_ap_ok(&s, 0x000002E8u) == true,  "wrap: 1000ms elapsed, stable");
}

/* 서먼 정책 입력(ev-open SummonInjectionSnapshot 에 해당)을
   "P단·정지·AP 비활성·서먼 없음, 전부 신선" 상태로 채운다. */
static void summon_parked(FSDState* s, uint32_t now) {
    s->di_seen = true;        s->di_seen_ms = now;    s->di_gear = DI_GEAR_P;
    s->speed_seen = true;     s->speed_seen_ms = now; s->di_speed_raw = DI_SPEED_STATIONARY_RAW;
    s->das_seen = true;       s->das_seen_ms = now;   s->das_ap_state = 1;
    s->spr_frame_seen = true; s->spr_frame_ms = now;  s->self_park_req = 0;
    s->di_aca = false;        s->spr_confirmed = false;
}

static void test_aux_gear_speed(void) {
    FSDState s; fsd_state_init(&s);
    /* DI_gear = 0x118 byte2 bit5..7 */
    CanFrame p = mk(ID_DI_SYSTEMSTATUS, 0,0,0x20,0,0,0,0,0);
    CHECK(fsd_aux_handle(&s, &p, 100) == true, "gear: 0x118 handled");
    CHECK(s.di_gear == DI_GEAR_P, "gear: 0x20 -> P");
    CHECK(s.di_seen == true && s.di_seen_ms == 100, "gear: seen stamped");
    CanFrame d = mk(ID_DI_SYSTEMSTATUS, 0,0,0x9F,0,0,0,0,0);
    fsd_aux_handle(&s, &d, 200);
    CHECK(s.di_gear == DI_GEAR_D, "gear: 0x9F -> D (low bits ignored)");
    /* dlc 7: ACA 는 읽지만 기어/신선도는 갱신 안 함 (ev-open 동일) */
    CanFrame r7 = mk(ID_DI_SYSTEMSTATUS, 0,0,0x40,0,0,0,0x04,0); r7.dlc = 7;
    fsd_aux_handle(&s, &r7, 300);
    CHECK(s.di_aca == true, "gear: dlc7 still updates ACA");
    CHECK(s.di_gear == DI_GEAR_D && s.di_seen_ms == 200, "gear: dlc7 leaves gear/stamp");

    /* DI_vehicleSpeed = 0x257 bit12..23, raw 500 = 0kph */
    CanFrame z = mk(ID_DI_SPEED, 0,0x40,0x1F,0,0,0,0,0);
    CHECK(fsd_aux_handle(&s, &z, 400) == true, "speed: 0x257 handled");
    CHECK(s.di_speed_raw == 500u, "speed: raw 500 decoded");
    CHECK(s.speed_seen == true && s.speed_seen_ms == 400, "speed: seen stamped");
    CanFrame sna = mk(ID_DI_SPEED, 0,0xF0,0xFF,0,0,0,0,0);
    fsd_aux_handle(&s, &sna, 500);
    CHECK(s.di_speed_raw == 4095u, "speed: SNA 4095 decoded");
    CanFrame sh = mk(ID_DI_SPEED, 0,0x40,0x1F,0,0,0,0,0); sh.dlc = 7;
    CHECK(fsd_aux_handle(&s, &sh, 600) == false, "speed: dlc<8 ignored");
    CHECK(s.di_speed_raw == 4095u && s.speed_seen_ms == 500, "speed: dlc<8 leaves value");
}

/* ev-open 서먼 확정: Summoning = ACA && 요청확정.
   확정 = 0x3F8(dlc 8) 요청값 1,2,4,5,6,7,8,10,11,12. 3/9 는 취소, 0 은 유지.
   ACA 하강엣지에 해제, ACA 상승 시 확정이 2초보다 오래됐으면 해제. */
static void test_summon_detect(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame aca_on  = mk(ID_DI_SYSTEMSTATUS, 0,0,0x20,0,0,0,0x04,0);
    CanFrame aca_off = mk(ID_DI_SYSTEMSTATUS, 0,0,0x20,0,0,0,0x00,0);
    CanFrame spr0  = mk(ID_UI_DRIVERASSIST, 0,0,0,0x00,0,0,0,0);
    CanFrame spr3  = mk(ID_UI_DRIVERASSIST, 0,0,0,0x30,0,0,0,0);
    CanFrame spr4  = mk(ID_UI_DRIVERASSIST, 0,0,0,0x40,0,0,0,0);
    CanFrame spr11 = mk(ID_UI_DRIVERASSIST, 0,0,0,0xB0,0,0,0,0);

    fsd_aux_handle(&s, &aca_on, 0);
    CHECK(s.summoning == false, "detect: aca alone doesn't summon");

    fsd_aux_handle(&s, &spr11, 100);
    CHECK(s.spr_frame_seen && s.spr_frame_ms == 100, "detect: 0x3F8 stamped");
    CHECK(s.spr_confirmed == true, "detect: 11 confirms");
    CHECK(s.summoning == true, "detect: aca && confirmed");

    fsd_aux_handle(&s, &spr0, 200);
    CHECK(s.summoning == true, "detect: request 0 keeps session");

    fsd_aux_handle(&s, &spr3, 300);
    CHECK(s.spr_confirmed == false && s.summoning == false, "detect: 3 cancels");

    fsd_aux_handle(&s, &spr4, 400);
    CHECK(s.summoning == true, "detect: re-confirm");
    fsd_aux_handle(&s, &aca_off, 500);
    CHECK(s.spr_confirmed == false && s.summoning == false, "detect: ACA falling edge ends");

    /* 확정 후 2초 안에 ACA 상승 → 유지 */
    fsd_aux_handle(&s, &spr4, 1000);
    fsd_aux_handle(&s, &aca_on, 3000);
    CHECK(s.summoning == true, "detect: ACA rise within 2000ms keeps");
    fsd_aux_handle(&s, &aca_off, 3100);

    /* 확정 후 2초 넘게 지나 ACA 상승 → 해제 (오래된 요청으로 자율제어를 서먼 취급 안 함) */
    fsd_aux_handle(&s, &spr4, 4000);
    fsd_aux_handle(&s, &spr0, 4100);
    fsd_aux_handle(&s, &aca_on, 6101);
    CHECK(s.summoning == false, "detect: stale confirm cleared on ACA rise");

    /* dlc<8 0x3F8: 요청값만 읽고 확정/신선도는 안 건드림 */
    FSDState t; fsd_state_init(&t);
    CanFrame sh = mk(ID_UI_DRIVERASSIST, 0,0,0,0x40,0,0,0,0); sh.dlc = 4;
    fsd_aux_handle(&t, &sh, 50);
    CHECK(t.self_park_req == 4, "detect: dlc4 reads request");
    CHECK(t.spr_confirmed == false && t.spr_frame_seen == false, "detect: dlc4 no confirm/stamp");
}

/* ev-open evaluateSummonInjectionPolicy 판정 순서 그대로 */
static void test_summon_policy(void) {
    FSDState s; fsd_state_init(&s);
    const uint32_t T = 10000u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_DI_UNSEEN, "policy: init DI unseen");

    summon_parked(&s, T);
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: parked+stationary allowed");
    CHECK(fsd_summon_ok(&s, T + 500u) == true, "policy: 500ms still fresh");

    /* 미수신 */
    summon_parked(&s, T); s.speed_seen = false;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_SPEED_UNSEEN, "policy: speed unseen");
    summon_parked(&s, T); s.das_seen = false;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_DAS_UNSEEN, "policy: das unseen");

    /* 신선도 창 1000ms: 999ms 는 살아있음, 1001ms 는 끊김 */
    summon_parked(&s, T); s.di_seen_ms = T - 999u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: di 999ms still fresh");
    summon_parked(&s, T); s.di_seen_ms = T - 1001u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_DI_STALE, "policy: di stale >1000ms");
    summon_parked(&s, T); s.speed_seen_ms = T - 1001u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_SPEED_STALE, "policy: speed stale");
    summon_parked(&s, T); s.das_seen_ms = T - 1001u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_DAS_STALE, "policy: das stale");
    /* 0x3F8(서먼요청) 신선도는 더 이상 게이트를 막지 않는다 */
    summon_parked(&s, T); s.spr_frame_seen = false;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: spr unseen no longer blocks");
    summon_parked(&s, T); s.spr_frame_ms = T - 5000u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: stale spr no longer blocks");

    /* 값 이상 */
    summon_parked(&s, T); s.di_gear = 0;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_BAD_GEAR, "policy: gear invalid 0");
    s.di_gear = 7;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_BAD_GEAR, "policy: gear SNA 7");
    summon_parked(&s, T); s.di_speed_raw = DI_SPEED_SNA_RAW;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_BAD_SPEED, "policy: speed SNA");

    /* AP: 3~6 결합, 2 초과 나머지(8/9/14/15)는 불확실 → 둘 다 차단 */
    for (uint8_t st = 3; st <= 6; st++) {
        summon_parked(&s, T); s.das_ap_state = st;
        CHECK(fsd_summon_ok_why(&s, T) == GATE_AP_ENGAGED, "policy: AP 3..6 engaged");
    }
    {
        const uint8_t bad[] = {7u, 8u, 9u, 14u, 15u};
        for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
            summon_parked(&s, T); s.das_ap_state = bad[i];
            CHECK(fsd_summon_ok_why(&s, T) == GATE_AP_INVALID, "policy: AP abort/fault/SNA");
        }
    }
    summon_parked(&s, T); s.self_park_req = 13;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_SPR_INVALID, "policy: request 13 invalid");

    /* P단인데 이동 */
    summon_parked(&s, T); s.di_speed_raw = 600u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_PARKED_MOVING, "policy: parked but moving");

    /* 서먼 진행 (ACA && 확정): R/D 이동 허용, N 모순 */
    summon_parked(&s, T); s.di_aca = true; s.spr_confirmed = true;
    s.di_gear = DI_GEAR_R; s.di_speed_raw = 520u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: summon in R moving allowed");
    s.di_gear = DI_GEAR_D;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: summon in D moving allowed");
    s.di_gear = DI_GEAR_N;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_GEAR_CONFLICT, "policy: summon in N blocked");
    s.di_gear = DI_GEAR_P; s.di_speed_raw = DI_SPEED_STATIONARY_RAW;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_OK, "policy: summon starting in P allowed");

    /* 확정 없는 ACA 는 P단이어도 차단 */
    summon_parked(&s, T); s.di_aca = true;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_ACA_UNCONFIRMED, "policy: aca without confirm");

    /* 수동 상태 */
    summon_parked(&s, T); s.di_gear = DI_GEAR_D;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_NOT_PARKED, "policy: D stationary no summon");
    s.di_speed_raw = 900u;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_NOT_PARKED, "policy: D moving no summon");

    /* 자동주차 회귀 (실차 2026-09-12 16:05): D·이동·AP6·ACA1·요청0 */
    summon_parked(&s, T);
    s.di_gear = DI_GEAR_D; s.di_speed_raw = 530u; s.das_ap_state = 6; s.di_aca = true;
    CHECK(fsd_summon_ok_why(&s, T) == GATE_AP_ENGAGED, "policy: autopark engaged blocked");
    s.das_ap_state = 1;   /* 결합 직전 */
    CHECK(fsd_summon_ok_why(&s, T) == GATE_ACA_UNCONFIRMED, "policy: autopark pre-engage blocked");

    /* millis 랩어라운드 (창 1000ms): base 0xFFFFFF00, now+0x100 = 경과 */
    summon_parked(&s, 0xFFFFFF00u);
    CHECK(fsd_summon_ok_why(&s, 0x000002E7u) == GATE_OK, "policy: wrap 999ms fresh");
    CHECK(fsd_summon_ok_why(&s, 0x000002E9u) == GATE_DI_STALE, "policy: wrap 1001ms stale");
}

static void test_apctl_mux_detect(void) {
    CanFrame m1 = mk(ID_AP_CONTROL, 0x01,0,0,0,0,0,0,0);
    CanFrame m0 = mk(ID_AP_CONTROL, 0x00,0,0,0,0,0,0,0);
    CanFrame m2 = mk(ID_AP_CONTROL, 0x0A,0,0,0,0,0,0,0); /* &7 == 2 */
    CHECK(fsd_apctl_is_mux1(&m1) == true,  "mux: 1 detected");
    CHECK(fsd_apctl_is_mux1(&m0) == false, "mux: 0 rejected");
    CHECK(fsd_apctl_is_mux1(&m2) == false, "mux: 2 rejected");
}

static void test_apctl_build_bits(void) {
    CanFrame in  = mk(ID_AP_CONTROL, 0x01,0xFF,0xFF,0xFF,0xFF,0x00,0xFF,0xFF);
    CanFrame out;
    fsd_apctl_build(&in, &out);
    CHECK((out.data[2] & 0x08u) == 0u,    "build: bit19 cleared");
    CHECK((out.data[5] & 0x80u) == 0x80u, "build: bit47 set");
    CHECK(out.data[2] == 0xF7u, "build: byte2 other bits untouched");
    CHECK(out.data[5] == 0x80u, "build: byte5 other bits untouched");
    CHECK(out.data[0] == 0x01u && out.data[1] == 0xFFu, "build: byte0/1 kept");
    CHECK(out.data[3] == 0xFFu && out.data[4] == 0xFFu, "build: byte3/4 kept");
    CHECK(out.data[6] == 0xFFu && out.data[7] == 0xFFu, "build: byte6/7 kept");
    CHECK(out.id == ID_AP_CONTROL && out.dlc == 8, "build: id/dlc kept");
}

static void test_apctl_should_send(void) {
    FSDState s; fsd_state_init(&s);
    s.das_seen = true; s.das_seen_ms = 1000;

    CHECK(fsd_apctl_should_send(&s, 1000) == false, "send: listen mode blocks");

    s.active = true;
    s.nag_kill = true; s.das_ap_state = 3; s.ap_engaged_since_ms = 0;
    CHECK(fsd_apctl_should_send(&s, 1000) == false, "send: not stable yet");

    s.ap_engaged_since_ms = 100;
    CHECK(fsd_apctl_should_send(&s, 1100) == true, "send: nag path open");

    s.nag_kill = false;
    CHECK(fsd_apctl_should_send(&s, 1100) == false, "send: nag off closes");

    s.summon_unlock = true; summon_parked(&s, 1100);
    CHECK(fsd_apctl_should_send(&s, 1100) == true, "send: summon parked path open");

    s.ota_in_progress = true;
    CHECK(fsd_apctl_should_send(&s, 1100) == false, "send: ota blocks all");
}

/* 손 요구 있음(level 0), das_hands_on = 2 인 기본 입력 */
static CanFrame epas_demand(void) {
    return mk(ID_EPAS_STATUS, 0x11,0x22,0x08,0x00,0x00,0x55,0x03,0x00);
}

static void test_nag_skips_when_hands_ok(void) {
    FSDState s; fsd_state_init(&s);
    s.das_hands_on = 2;
    CanFrame in = epas_demand();
    in.data[EPAS_HANDS_BYTE] = 0x40u;   /* handsOnLevel = 1 */
    CanFrame out;
    CHECK(fsd_nag_build(&s, &in, &out, 0) == false, "nag: hands ok skips");
}

static void test_nag_skips_when_das_not_required(void) {
    FSDState s; fsd_state_init(&s);
    CanFrame in = epas_demand(); CanFrame out;
    s.das_seen = true; s.das_hands_on = DAS_HANDS_NOT_REQUIRED;
    CHECK(fsd_nag_build(&s, &in, &out, 0) == false, "nag: das 0 skips");
    s.das_hands_on = DAS_HANDS_SUSPENDED;
    CHECK(fsd_nag_build(&s, &in, &out, 0) == false, "nag: das 8 skips");
}

static void test_nag_skips_short_frame(void) {
    FSDState s; fsd_state_init(&s);
    s.das_hands_on = 2;
    CanFrame in = epas_demand(); in.dlc = 7;
    CanFrame out;
    CHECK(fsd_nag_build(&s, &in, &out, 0) == false, "nag: dlc<8 skips");
}

static void test_nag_basic_frame_shape(void) {
    FSDState s; fsd_state_init(&s);
    s.das_seen = true; s.das_hands_on = 2;
    CanFrame in = epas_demand(); CanFrame out;
    CHECK(fsd_nag_build(&s, &in, &out, 0) == true, "nag: fires on demand");
    CHECK(out.id == ID_EPAS_STATUS && out.dlc == 8, "nag: id/dlc");
    CHECK(out.data[0] == in.data[0], "nag: byte0 kept");
    CHECK(out.data[1] == in.data[1], "nag: byte1 kept");
    CHECK(out.data[5] == in.data[5], "nag: byte5 kept");
    CHECK((out.data[EPAS_HANDS_BYTE] & 0xC0u) == 0x40u, "nag: handsOnLevel spoofed to 1");
    CHECK((out.data[EPAS_COUNTER_BYTE] & 0x0Fu) == 0x04u, "nag: counter +1");
    CHECK((out.data[EPAS_COUNTER_BYTE] & 0xF0u) == (in.data[6] & 0xF0u),
          "nag: counter high nibble kept");
    CHECK(out.data[7] == tesla_additive_checksum(ID_EPAS_STATUS, out.data, 7),
          "nag: checksum valid");
    CHECK(s.nag_echo_count == 1, "nag: echo counted");
}

static void test_nag_counter_wraps(void) {
    FSDState s; fsd_state_init(&s);
    s.das_seen = true; s.das_hands_on = 2;
    CanFrame in = epas_demand(); in.data[EPAS_COUNTER_BYTE] = 0xAFu;
    CanFrame out;
    fsd_nag_build(&s, &in, &out, 0);
    CHECK((out.data[EPAS_COUNTER_BYTE] & 0x0Fu) == 0x00u, "nag: counter 15 wraps to 0");
    CHECK((out.data[EPAS_COUNTER_BYTE] & 0xF0u) == 0xA0u, "nag: high nibble kept on wrap");
}

static void test_nag_torque_within_cap(void) {
    FSDState s; fsd_state_init(&s);
    s.das_seen = true; s.das_hands_on = 2;
    CanFrame in = epas_demand(); CanFrame out;
    for (int i = 0; i < 2000; i++) {
        if (!fsd_nag_build(&s, &in, &out, (uint32_t)i)) continue;
        int16_t raw = (int16_t)(((out.data[2] & 0x0Fu) << 8) | out.data[3]);
        CHECK(raw >= NAG_TORQUE_RAW_MIN && raw <= NAG_TORQUE_RAW_MAX,
              "nag: torque within +-1.8Nm cap");
        if (g_fail) break;
    }
}

static void test_nag_burst_duty(void) {
    FSDState s; fsd_state_init(&s);
    s.wave = NAG_BURST; s.das_seen = true; s.das_hands_on = 2;
    CanFrame in = epas_demand(); CanFrame out;
    CHECK(fsd_nag_build(&s, &in, &out, 0)    == true,  "burst: 0ms fires");
    CHECK(fsd_nag_build(&s, &in, &out, 999)  == true,  "burst: 999ms fires");
    CHECK(fsd_nag_build(&s, &in, &out, 1000) == false, "burst: 1000ms pauses");
    CHECK(fsd_nag_build(&s, &in, &out, 2499) == false, "burst: 2499ms pauses");
    CHECK(fsd_nag_build(&s, &in, &out, 2500) == true,  "burst: 2500ms resumes");
}

static void test_faithful_gates(void) {
    FSDState s; fsd_state_init(&s);
    s.wave = NAG_FAITHFUL; s.das_seen = true;
    CanFrame in = epas_demand(); CanFrame out;

    s.das_ap_state = 1; s.das_hands_on = 2;
    CHECK(fsd_nag_build(&s, &in, &out, 5000) == false, "faithful: ap_state<2 skips");

    s.das_ap_state = 3; s.das_hands_on = 0;
    CHECK(fsd_nag_build(&s, &in, &out, 5000) == false, "faithful: das 0 skips");
    s.das_hands_on = 8;
    CHECK(fsd_nag_build(&s, &in, &out, 5000) == false, "faithful: das 8 skips");
    s.das_hands_on = 15;
    CHECK(fsd_nag_build(&s, &in, &out, 5000) == false, "faithful: das 15 skips");
}

static void test_faithful_entry_delay_and_hands_untouched(void) {
    FSDState s; fsd_state_init(&s);
    s.wave = NAG_FAITHFUL; s.das_seen = true; s.das_ap_state = 3;
    CanFrame in = epas_demand(); CanFrame out;

    /* 단계 3: 진입 후 1000ms 지연 */
    s.das_hands_on = 3;
    CHECK(fsd_nag_build(&s, &in, &out, 1000) == false, "faithful: stage3 delay start");
    CHECK(fsd_nag_build(&s, &in, &out, 1999) == false, "faithful: stage3 999ms wait");
    CHECK(fsd_nag_build(&s, &in, &out, 2000) == true,  "faithful: stage3 fires at 1000ms");
    CHECK(out.data[EPAS_HANDS_BYTE] == in.data[EPAS_HANDS_BYTE],
          "faithful: handsOnLevel untouched");
    CHECK(out.data[7] == tesla_additive_checksum(ID_EPAS_STATUS, out.data, 7),
          "faithful: checksum valid");
}

static void test_faithful_torque_opposes_steering(void) {
    FSDState s; fsd_state_init(&s);
    s.wave = NAG_FAITHFUL; s.das_seen = true; s.das_ap_state = 3;
    s.das_hands_on = 3; s.steering_deg = 20.0f;   /* 우측 조향 -> 토크 좌측 */
    CanFrame in = epas_demand(); CanFrame out;
    fsd_nag_build(&s, &in, &out, 1000);
    fsd_nag_build(&s, &in, &out, 2500);
    int16_t raw = (int16_t)(((out.data[2] & 0x0Fu) << 8) | out.data[3]);
    CHECK(raw < NAG_TORQUE_CENTER, "faithful: torque opposes positive steering");
}

static void test_gate_reasons(void) {
    FSDState s; fsd_state_init(&s);
    CHECK(fsd_can_transmit_why(&s) == GATE_LISTEN, "why: listen");
    s.active = true;
    CHECK(fsd_can_transmit_why(&s) == GATE_OK, "why: ok");
    s.ota_in_progress = true;
    CHECK(fsd_can_transmit_why(&s) == GATE_OTA, "why: ota");

    fsd_state_init(&s); s.active = true;
    s.das_ap_state = 1;
    CHECK(fsd_ap_ok_why(&s, 5000) == GATE_AP_NOT_ENGAGED, "why: ap not engaged");
    s.das_ap_state = 3; s.ap_engaged_since_ms = 1000;
    CHECK(fsd_ap_ok_why(&s, 1500) == GATE_AP_UNSTABLE, "why: ap unstable");
    CHECK(fsd_ap_ok_why(&s, 2000) == GATE_OK, "why: ap ok");
    s.das_ap_state = 6;
    CHECK(fsd_ap_ok_why(&s, 2000) == GATE_AP_FSD, "why: ap fsd/autopark");

    fsd_state_init(&s); s.active = true;
    CHECK(fsd_summon_ok_why(&s, 0) == GATE_DI_UNSEEN, "why: summon di unseen");
    summon_parked(&s, 500);
    CHECK(fsd_summon_ok_why(&s, 500) == GATE_OK, "why: summon parked ok");
    s.das_ap_state = 3;
    CHECK(fsd_summon_ok_why(&s, 500) == GATE_AP_ENGAGED, "why: summon ap engaged");
}

static void test_gate_bool_wrappers_agree(void) {
    FSDState s; fsd_state_init(&s);
    s.active = true; s.das_seen = true; s.das_seen_ms = 0;
    for (uint8_t st = 0; st <= 6; st++) {
        s.das_ap_state = st;
        s.ap_engaged_since_ms = (st >= 3) ? 1u : 0u;
        CHECK(fsd_ap_ok(&s, 2000) == (fsd_ap_ok_why(&s, 2000) == GATE_OK),
              "wrapper: ap_ok agrees with _why");
        CHECK(fsd_summon_ok(&s, 400) == (fsd_summon_ok_why(&s, 400) == GATE_OK),
              "wrapper: summon_ok agrees with _why");
    }
}

static void test_reason_names(void) {
    CHECK(fsd_gate_reason_name(GATE_OK)[0] != 0, "name: OK non-empty");
    CHECK(fsd_gate_reason_name(GATE_AP_ENGAGED)[0] != 0, "name: AP_ENGAGED non-empty");
    CHECK(fsd_gate_reason_name((GateReason)99)[0] != 0, "name: out of range safe");
}

static void test_trace_first_call_is_silent(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    CHECK(fsd_trace_step(&s, &snap, 1000, buf, sizeof buf) == 0, "trace: first call silent");
}

static void test_trace_emits_transition(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    fsd_trace_step(&s, &snap, 1000, buf, sizeof buf);
    s.das_ap_state = 2;
    size_t n = fsd_trace_step(&s, &snap, 1500, buf, sizeof buf);
    CHECK(n > 0, "trace: transition emitted");
    CHECK(strstr(buf, "das_ap_state") != NULL, "trace: names the field");
    CHECK(strstr(buf, "0 -> 2") != NULL, "trace: shows old -> new");
    CHECK(strstr(buf, "1.500") != NULL, "trace: timestamp in seconds");
}

static void test_trace_emits_gate_reason(void) {
    FSDState s; fsd_state_init(&s);
    s.active = true; s.summon_unlock = true;
    summon_parked(&s, 0);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    fsd_trace_step(&s, &snap, 0, buf, sizeof buf);   /* prime: summon_ok OK */
    s.di_gear = DI_GEAR_D;
    size_t n = fsd_trace_step(&s, &snap, 100, buf, sizeof buf);
    CHECK(n > 0, "trace: gate change emitted");
    CHECK(strstr(buf, "NOT_PARKED") != NULL, "trace: summon reason named");
    CHECK(strstr(buf, "di_gear") != NULL, "trace: gear transition logged");
}

static void test_trace_emits_speed_and_b1(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    s.di_speed_raw = 600u;   /* 정지(500) 아님 — stationary 전이 배제 */
    fsd_trace_step(&s, &snap, 0, buf, sizeof buf);   /* baseline 600 */

    /* 40 미만 변화는 안 남음 */
    s.di_speed_raw = 630u;
    CHECK(fsd_trace_step(&s, &snap, 100, buf, sizeof buf) == 0, "trace: <40 speed drift silent");
    /* 누적 40 이상이면 남음 */
    s.di_speed_raw = 645u;   /* 600 -> 645 = 45 */
    size_t n = fsd_trace_step(&s, &snap, 200, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "di_speed_raw") != NULL, "trace: >=40 drift logged");
    CHECK(strstr(buf, "600 -> 645") != NULL, "trace: speed from last-logged baseline");

    /* b1 니블 전이 기록 */
    s.das_ap_state_b1 = 3u;
    n = fsd_trace_step(&s, &snap, 300, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "ap_state_b1") != NULL, "trace: b1 nibble transition logged");
}

static void test_trace_emits_stationary(void) {
    FSDState s; fsd_state_init(&s);
    s.di_speed_raw = DI_SPEED_STATIONARY_RAW;
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    fsd_trace_step(&s, &snap, 0, buf, sizeof buf);
    s.di_speed_raw = 620u;
    size_t n = fsd_trace_step(&s, &snap, 100, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "stationary") != NULL, "trace: stationary logged");
    CHECK(strstr(buf, "1 -> 0") != NULL, "trace: stationary 1 -> 0");
}

static void test_trace_emits_markers(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    fsd_trace_step(&s, &snap, 0, buf, sizeof buf);
    s.summon_fwd_leash = true;
    size_t n = fsd_trace_step(&s, &snap, 500, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "LEASH_FWD") != NULL, "trace: leash marker");

    FSDState t; fsd_state_init(&t);
    TraceSnap snap2; fsd_trace_init(&snap2);
    t.das_ap_state = 2;
    fsd_trace_step(&t, &snap2, 0, buf, sizeof buf);
    t.das_ap_state = 1;
    n = fsd_trace_step(&t, &snap2, 900, buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "AP_OFFER_LOST") != NULL, "trace: ap offer lost marker");
}

static void test_trace_silent_when_static(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char buf[512];
    fsd_trace_step(&s, &snap, 0, buf, sizeof buf);
    CHECK(fsd_trace_step(&s, &snap, 1000, buf, sizeof buf) == 0, "trace: quiet when static");
    CHECK(fsd_trace_step(&s, &snap, 2000, buf, sizeof buf) == 0, "trace: still quiet");
}

static void test_trace_respects_buffer_cap(void) {
    FSDState s; fsd_state_init(&s);
    TraceSnap snap; fsd_trace_init(&snap);
    char big[512], small[24];
    fsd_trace_step(&s, &snap, 0, big, sizeof big);
    s.das_ap_state = 3; s.das_hands_on = 2; s.summon_available = true;
    size_t n = fsd_trace_step(&s, &snap, 100, small, sizeof small);
    CHECK(n < sizeof small, "trace: never exceeds cap");
    CHECK(small[n] == 0, "trace: NUL terminated");
}

static void test_x3f8_build(void) {
    /* 관측된 실제 0x3F8: 03 28 80 00 19 29 19 24 */
    CanFrame in = mk(ID_UI_DRIVERASSIST, 0x03,0x28,0x80,0x00,0x19,0x29,0x19,0x24);
    CanFrame out;
    FSDState s; fsd_state_init(&s);

    s.x3f8_exp = 0x00u;
    CHECK(fsd_x3f8_build(&s, &in, &out) == false, "x3f8: off = no build");

    s.x3f8_exp = 0x01u;   /* bit0 = 신호1 ulcStalkConfirm: byte0 bit1 clear */
    CHECK(fsd_x3f8_build(&s, &in, &out) == true,  "x3f8: sig1 builds");
    CHECK(out.data[0] == 0x01u,                   "x3f8: sig1 clears byte0 bit1");
    CHECK(out.data[4] == 0x19u && out.data[7] == 0x24u, "x3f8: sig1 other bytes kept");
    CHECK(out.id == ID_UI_DRIVERASSIST && out.dlc == 8, "x3f8: id/dlc kept");

    s.x3f8_exp = 0x20u;   /* bit5 = 신호6 adaptiveSetSpeed: byte4 bit7 set */
    CHECK(fsd_x3f8_build(&s, &in, &out) == true,  "x3f8: sig6 builds");
    CHECK(out.data[4] == 0x99u,                   "x3f8: sig6 byte4 = 0x19|0x80");

    /* 여러 비트 동시: 신호1(bit0) + 신호3(bit2) — byte0 클리어 + byte6 세팅 */
    s.x3f8_exp = 0x05u;
    CHECK(fsd_x3f8_build(&s, &in, &out) == true,  "x3f8: sig1+3 builds");
    CHECK(out.data[0] == 0x01u,                   "x3f8: sig1+3 byte0 cleared");
    CHECK(out.data[6] == 0x59u,                   "x3f8: sig1+3 byte6 = 0x19|0x40");
    CHECK(out.data[4] == 0x19u,                   "x3f8: sig1+3 byte4 untouched");

    /* 잘못된 id / 짧은 프레임은 무시 */
    CanFrame other = mk(0x370u, 0x03,0x28,0x80,0x00,0x19,0x29,0x19,0x24);
    s.x3f8_exp = 0x01u;
    CHECK(fsd_x3f8_build(&s, &other, &out) == false, "x3f8: wrong id ignored");
}

int main(void) {
    test_checksum();
    test_x3f8_build();
    test_state_init();
    test_das_real_frame();
    test_das_summon_bits();
    test_das_source_priority();
    test_das_399_when_no_39b();
    test_das_engage_timestamp();
    test_das_short_frame_ignored();
    test_aux_selfpark_and_aca();
    test_ota_rolling_counter_never_trips();
    test_ota_assert_after_three();
    test_ota_clear_after_six();
    test_ota_assert_run_resets();
    test_can_transmit();
    test_ap_ok();
    test_das_engage_at_time_zero();
    test_ap_ok_across_millis_wrap();
    test_aux_gear_speed();
    test_summon_detect();
    test_summon_policy();
    test_apctl_mux_detect();
    test_apctl_build_bits();
    test_apctl_should_send();
    test_nag_skips_when_hands_ok();
    test_nag_skips_when_das_not_required();
    test_nag_skips_short_frame();
    test_nag_basic_frame_shape();
    test_nag_counter_wraps();
    test_nag_torque_within_cap();
    test_nag_burst_duty();
    test_faithful_gates();
    test_faithful_entry_delay_and_hands_untouched();
    test_faithful_torque_opposes_steering();
    test_gate_reasons();
    test_gate_bool_wrappers_agree();
    test_reason_names();
    test_trace_first_call_is_silent();
    test_trace_emits_transition();
    test_trace_emits_gate_reason();
    test_trace_emits_markers();
    test_trace_emits_stationary();
    test_trace_emits_speed_and_b1();
    test_trace_silent_when_static();
    test_trace_respects_buffer_cap();
    if (g_fail == 0) { printf("ALL PASS\n"); return 0; }
    printf("%d FAILED\n", g_fail);
    return 1;
}
