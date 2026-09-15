#pragma once
#include "fsd_state.h"

/* 0x3FD mux1 프레임인지 (byte0 & 0x07 == 1) */
bool fsd_apctl_is_mux1(const CanFrame* f);

/* 지금 mux1 사본을 보내야 하는가.
   잔소리 경로와 서먼 경로가 보내는 비트는 동일하고 게이트만 다르다. */
bool fsd_apctl_should_send(const FSDState* s, uint32_t now_ms);

/* 사본 생성: bit19 클리어 + bit47 세트. 나머지 바이트는 그대로.
   체크섬은 재계산하지 않는다 (원본 동작 유지). */
void fsd_apctl_build(const CanFrame* in, CanFrame* out);
