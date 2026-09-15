#pragma once
#include <stdint.h>

void web_begin();
void web_task(void* arg);

/* /api/set 이 바꿀 수 있는 토글 필드. */
enum PendingField { PF_ACT, PF_NAG, PF_SMN, PF_WAVE, PF_X3F8 };

/* main.cpp 가 정의. core 0(웹)에서 호출한다. seed(현재 g_state)+덮어쓰기를
   g_mux 한 번으로 원자적으로 처리하므로, core 1 의 apply_pending() 이 절반만
   쓰인 pending 을 읽는 레이스가 없다. wave 는 field==PF_WAVE 일 때만 쓰인다
   (이미 [0,2] 로 클램프된 값). 그 외 필드는 bv 만 쓴다. */
void pending_commit(PendingField field, bool bv, uint8_t wave);
