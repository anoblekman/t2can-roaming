#pragma once
#include "fsd_state.h"

/* 활성 모드이고 Tesla OTA 중이 아니면 송신 가능 */
bool fsd_can_transmit(const FSDState* s);

/* 주행 게이트: AP 결합(3~5) 후 AP_STABLE_MS 경과. 토글 없이 항상 적용.
   6(ACTIVE_FSD = 이 차 자동주차)·8/9/14/15(중단/고장/SNA)는 차단 */
bool fsd_ap_ok(const FSDState* s, uint32_t now_ms);

/* 서먼 게이트: ev-open evaluateSummonInjectionPolicy — 서먼 진행 중이거나
   P단·정지(주차)면 허용. AP 결합·신호 끊김·값 이상은 차단. */
bool fsd_summon_ok(const FSDState* s, uint32_t now_ms);

typedef enum {
    GATE_OK = 0,
    GATE_TOGGLE_OFF,
    GATE_LISTEN,
    GATE_OTA,
    GATE_DAS_UNSEEN,
    GATE_DAS_STALE,
    GATE_AP_NOT_ENGAGED,
    GATE_AP_UNSTABLE,
    GATE_AP_ENGAGED,
    GATE_AP_FSD,
    GATE_DI_UNSEEN,
    GATE_DI_STALE,
    GATE_SPEED_UNSEEN,
    GATE_SPEED_STALE,
    GATE_SPR_UNSEEN,
    GATE_SPR_STALE,
    GATE_BAD_GEAR,
    GATE_BAD_SPEED,
    GATE_AP_INVALID,
    GATE_SPR_INVALID,
    GATE_PARKED_MOVING,
    GATE_GEAR_CONFLICT,
    GATE_ACA_UNCONFIRMED,
    GATE_NOT_PARKED,
    GATE_REASON_COUNT
} GateReason;

/* 사유의 짧은 이름. 범위 밖 값에도 항상 유효한 문자열을 준다. */
const char* fsd_gate_reason_name(GateReason r);

GateReason fsd_can_transmit_why(const FSDState* s);
GateReason fsd_ap_ok_why(const FSDState* s, uint32_t now_ms);
GateReason fsd_summon_ok_why(const FSDState* s, uint32_t now_ms);
