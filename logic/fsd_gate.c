#include "fsd_gate.h"
#include "fsd_signals.h"

static const char* const k_reason_names[] = {
    "OK", "TOGGLE_OFF", "LISTEN", "OTA", "DAS_UNSEEN", "DAS_STALE",
    "AP_NOT_ENGAGED", "AP_UNSTABLE", "AP_ENGAGED", "AP_FSD",
    "DI_UNSEEN", "DI_STALE", "SPEED_UNSEEN", "SPEED_STALE", "SPR_UNSEEN", "SPR_STALE",
    "BAD_GEAR", "BAD_SPEED", "AP_INVALID", "SPR_INVALID",
    "PARKED_MOVING", "GEAR_CONFLICT", "ACA_UNCONFIRMED", "NOT_PARKED",
};
_Static_assert(sizeof k_reason_names / sizeof k_reason_names[0] == GATE_REASON_COUNT,
               "k_reason_names must match GateReason");

const char* fsd_gate_reason_name(GateReason r) {
    if ((unsigned)r >= (sizeof k_reason_names / sizeof k_reason_names[0]))
        return "UNKNOWN";
    return k_reason_names[r];
}

GateReason fsd_can_transmit_why(const FSDState* s) {
    if (!s->active)         return GATE_LISTEN;
    if (s->ota_in_progress) return GATE_OTA;
    return GATE_OK;
}

GateReason fsd_ap_ok_why(const FSDState* s, uint32_t now_ms) {
    if (s->das_ap_state < DAS_APSTATE_ENGAGED) return GATE_AP_NOT_ENGAGED;
    /* 실차 사고 2건(2026-09-12)은 자동주차(6) 중 주입. FSD 미사용이라 6 은 전부 차단 */
    if (s->das_ap_state == DAS_APSTATE_FSD)    return GATE_AP_FSD;
    if (s->das_ap_state > DAS_APSTATE_NAG_MAX) return GATE_AP_INVALID;
    if (s->ap_engaged_since_ms == 0u)          return GATE_AP_NOT_ENGAGED;
    if ((uint32_t)(now_ms - s->ap_engaged_since_ms) < AP_STABLE_MS)
        return GATE_AP_UNSTABLE;
    return GATE_OK;
}

static bool stale(uint32_t now_ms, uint32_t ts) {
    return (uint32_t)(now_ms - ts) > SUMMON_FRESH_MS;
}

/* ev-open evaluateSummonInjectionPolicy 와 같은 순서.
   0x390 보조 기어 교차확인은 생략(ev-open 에서도 그 프레임을 받을 때만 적용). */
GateReason fsd_summon_ok_why(const FSDState* s, uint32_t now_ms) {
    if (!s->di_seen)                          return GATE_DI_UNSEEN;
    if (!s->speed_seen)                       return GATE_SPEED_UNSEEN;
    if (!s->das_seen)                         return GATE_DAS_UNSEEN;
    if (stale(now_ms, s->di_seen_ms))         return GATE_DI_STALE;
    if (stale(now_ms, s->speed_seen_ms))      return GATE_SPEED_STALE;
    if (stale(now_ms, s->das_seen_ms))        return GATE_DAS_STALE;
    /* 0x3F8(서먼요청) 신선도는 안 본다: 세션은 spr_confirmed 래치(ACA 하강엣지에
       해제)로 잡고, 주차 분기는 요청 자체가 불필요. 0x3F8 이 이 차에서 가장 느려
       (~1~2s) 신선도로 걸면 게이트가 깜빡이기만 한다. 값 유효성(>12)만 아래서 본다. */
    if (s->di_gear < DI_GEAR_P || s->di_gear > DI_GEAR_D) return GATE_BAD_GEAR;
    if (s->di_speed_raw > DI_SPEED_MAX_VALID_RAW)        return GATE_BAD_SPEED;
    if (s->das_ap_state >= DAS_APSTATE_ENGAGED && s->das_ap_state <= DAS_APSTATE_FSD)
                                              return GATE_AP_ENGAGED;
    if (s->das_ap_state > 2u)                 return GATE_AP_INVALID;   /* 중단/고장/SNA */
    if (s->self_park_req > SUMMON_REQ_MAX_VALID) return GATE_SPR_INVALID;

    bool stationary = s->di_speed_raw == DI_SPEED_STATIONARY_RAW;
    bool summon     = s->di_aca && s->spr_confirmed;

    if (s->di_gear == DI_GEAR_P && !stationary) return GATE_PARKED_MOVING;
    if (summon && s->di_gear == DI_GEAR_N)      return GATE_GEAR_CONFLICT;
    if (summon)                                 return GATE_OK;   /* 서먼 진행 */
    if (s->di_aca)                              return GATE_ACA_UNCONFIRMED;
    if (s->di_gear == DI_GEAR_P)                return GATE_OK;   /* 주차·정지 */
    return GATE_NOT_PARKED;
}

bool fsd_can_transmit(const FSDState* s) {
    return fsd_can_transmit_why(s) == GATE_OK;
}
bool fsd_ap_ok(const FSDState* s, uint32_t now_ms) {
    return fsd_ap_ok_why(s, now_ms) == GATE_OK;
}
bool fsd_summon_ok(const FSDState* s, uint32_t now_ms) {
    return fsd_summon_ok_why(s, now_ms) == GATE_OK;
}
