#pragma once
#include <stddef.h>
#include "fsd_gate.h"
#include "fsd_state.h"

/* 직전에 관측한 값들. 호출자가 소유한다. */
typedef struct {
    bool     primed;
    uint8_t  ap_state, ap_state_b1, hands_on, lane_change, speed_limit;
    uint16_t speed_raw;
    bool     summon_available, fwd_leash, rvs_leash, summon_obstacle;
    bool     autopark_ready, auto_parked, ota, summoning;
    uint8_t  gear;
    bool     stationary;
    uint8_t  ap_reason, summon_reason;
} TraceSnap;

void fsd_trace_init(TraceSnap* snap);

/* 변화가 있으면 out 에 줄들을 쓰고 쓴 바이트 수를 반환(NUL 제외). 없으면 0.
   out 은 항상 NUL 로 끝나고 cap 을 넘지 않는다.
   첫 호출은 기준선만 잡고 0 을 반환한다. */
size_t fsd_trace_step(const FSDState* s, TraceSnap* snap, uint32_t now_ms,
                      char* out, size_t cap);
