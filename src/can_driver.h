#pragma once
#include <Arduino.h>
extern "C" {
#include "fsd_state.h"
}

/* 실크 A = MCP2515 = can1, 실크 B = TWAI = can0 */
enum CanBusId { CAN_B_TWAI = 0, CAN_A_MCP = 1 };

bool can_init();
bool can_receive(CanBusId* bus, CanFrame* frame);   /* 큐에서 하나 꺼냄 */
bool can_send(CanBusId bus, const CanFrame& frame);
void can_poll_bus_health();                          /* TWAI bus-off 복구 */

/* 현재 수신 에러 카운터(REC) 스냅샷을 읽는다. err_mcp_a = 실크 A(MCP2515),
   err_twai_b = 실크 B(TWAI). 0 이 정상 — 오르면 버스 품질/배선/비트레이트 의심.
   버스가 죽어 있으면 0 을 쓴다. MCP 쪽은 SPI 레지스터 읽기라 매 루프 호출 금지. */
void can_read_errors(uint32_t* err_mcp_a, uint32_t* err_twai_b);

/* 하드웨어 모드 전환: true=Normal(송신 가능), false=Listen-Only(물리적으로 송신 불가).
   can_init() 은 두 버스 모두 Listen-Only 로 부팅한다. 멱등 — 이미 원하는 모드면
   아무 것도 하지 않는다 (TWAI 재설치를 반복하지 않음). */
void can_set_active(bool active_mode);

/* 재부팅 직전 CAN 안전 정리 — 컨트롤러 정지 + TX recessive 고정 (OTA 재부팅
   글리치로 인한 버스 교란 완화). 호출 후엔 CAN 이 죽으므로 바로 재부팅해야 한다. */
void can_shutdown_safe();
