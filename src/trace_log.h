#pragma once
#include <Arduino.h>
extern "C" {
#include "fsd_state.h"
}

/* §4.8 게이트 결정 트레이스 — LittleFS 링 파일, 항상 켜짐(부팅 시 시작).
 *
 * 로직은 logic/fsd_trace.{h,c}(Task 10, 읽기 전용) 가 이미 완성했다. 여기서는
 * 그것을 ESP32 에서 호출하고 LittleFS 에 저장하고 웹으로 노출만 한다.
 *
 * 동시성: trace_step/trace_tick_tx/trace_flush 는 core 1, trace_dump/trace_clear
 *   는 core 0(웹). 같은 LittleFS 파일을 만지므로 FreeRTOS 뮤텍스로 상호배제한다
 *   (portMUX 아님 — flash I/O 처럼 느린 작업엔 스핀락 부적합). trace_step 의
 *   g_state 읽기는 core 1 단독이라 락이 필요 없다. */

void   trace_begin();                                    /* setup(): LittleFS 마운트 + 세션 헤더 예약 */
void   trace_note_rx(uint8_t bus, uint32_t id);          /* core 1: 진단 — 프레임 수신을 버스·ID별로 집계 */
void   trace_note_raw(uint32_t id, const uint8_t* data, uint32_t now_ms); /* core 1: 진단 — 0x3FD mux1/0x39B 원시 바이트 변화 기록 */
void   trace_step(const FSDState* s, uint32_t now_ms);   /* core 1: fsd_trace_step 호출 -> RAM 버퍼 적재 */
void   trace_tick_tx(const FSDState* s, uint32_t now_ms);/* core 1: 초당 1회 ④ 송신집계 줄 적재(선택) */
void   trace_flush();                                    /* core 1: RAM 버퍼를 LittleFS 로 비움 */
size_t trace_dump(Print& out);                           /* core 0 웹: 현재 로그 파일 전체를 out 으로 */
void   trace_clear();                                    /* 로그 파일 비우기 */
