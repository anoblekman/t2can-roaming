#pragma once
#include "fsd_state.h"

/* 0x39B / 0x399 정본 디코더. 처리했으면 true.
   0x39B 를 한 번이라도 받으면 0x399 는 이후 무시한다 (HW4 에서 0x399 는 속도경고 차임 프레임). */
bool fsd_das_handle(FSDState* s, const CanFrame* f, uint32_t now_ms);

/* 0x318 GTW_carState 로 Tesla OTA 진행 여부 추적. 0x318 이면 true. */
bool fsd_ota_handle(FSDState* s, const CanFrame* f);

/* 0x3F8(selfParkRequest)/0x118(ACA·기어)/0x257(속도) 관측 디코드. 처리했으면 true.
   관측만 한다 — 송신과 무관, 리슨/활성 모두에서 디코드된다. */
bool fsd_aux_handle(FSDState* s, const CanFrame* f, uint32_t now_ms);
