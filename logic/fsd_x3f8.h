#pragma once
#include "fsd_state.h"

/* 0x3F8(UI_driverAssistControl) 실험 하네스.
   실제 0x3F8 을 에코하며 선택된 관심 신호 비트 하나만 목표값으로 강제한다.
   0x3F8 은 롤링 카운터·체크섬이 없음이 실차 확인됨(04137253: 57초간 전 프레임
   바이트 동일) → 에코+비트세팅만, 카운터/체크섬 처리 불필요.

   x3f8_exp: **비트마스크** — bit0~6 = 신호 1~7. 0=off. 여러 신호를 동시에 켜면
   한 프레임에 모두 적용된다(연관 기능 조합 검증용, 예: 차선변경 확인생략+고속밖
   ALC/ULC). 게이트(잔소리와 동일: AP 3~5 + 1초 안정 + 활성모드)는 호출부(route)가
   건다. 비트 위치는 tesla_can 0x3E8 맵 기반 **가설** — 켜서 실제 차 거동으로
   검증하는 게 이 하네스의 목적. */
#ifdef __cplusplus
extern "C" {
#endif

#define X3F8_EXP_COUNT 8u   /* 0(off) + 7 신호 */

/* in(수신 0x3F8)을 복사해 선택 비트를 강제한 out 을 만든다. exp==0/범위밖이거나
   프레임이 0x3F8 dlc8 이 아니면 false. */
bool fsd_x3f8_build(const FSDState* s, const CanFrame* in, CanFrame* out);

#ifdef __cplusplus
}
#endif
