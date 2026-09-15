#pragma once
#include "fsd_state.h"

/* 0x370 에코 생성. 만들었으면 true.
   호출 전제: 호출자가 nag_kill && can_transmit && ap_ok 를 이미 확인했다. */
bool fsd_nag_build(FSDState* s, const CanFrame* in, CanFrame* out, uint32_t now_ms);
