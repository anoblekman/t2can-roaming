#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t id;
    uint8_t  dlc;
    uint8_t  data[8];
} CanFrame;

typedef enum { NAG_BASIC = 0, NAG_BURST = 1, NAG_FAITHFUL = 2 } NagWave;

typedef struct {
    /* 설정 (NVS) */
    bool     active;
    bool     nag_kill;
    NagWave  wave;
    bool     summon_unlock;

    /* 0x39B / 0x399 정본 디코드 */
    uint8_t  das_ap_state;
    uint8_t  das_ap_state_b1;   /* 관측용: 0x39B byte1[7:4] (flipper HW4 기본 판독 위치) */
    uint8_t  das_hands_on;
    uint8_t  das_lane_change;
    uint8_t  das_speed_limit;
    bool     summon_available;
    bool     summon_fwd_leash;
    bool     summon_rvs_leash;
    bool     summon_obstacle;
    bool     summon_cleared_gate;
    bool     autopark_ready;
    bool     auto_parked;
    bool     das_seen;
    bool     das_39b_seen;      /* true 면 0x399 는 DAS 소스에서 제외 */

    /* 0x3F8 / 0x118 관측 (리슨 안전, 주입 무관) */
    uint8_t  self_park_req;   /* 0x3F8 UI_selfParkRequest (0=없음,7/8=자동서먼,11=스마트서먼) */
    bool     di_aca;          /* 0x118 DI_autonomyControlActive */

    /* 0x3F8 실험 하네스 (NVS 저장 — 잔소리/서먼과 동일하게 재부팅 후 복원) */
    uint8_t  x3f8_exp;        /* 비트마스크 bit0~6 = 신호 1~7 (여러 개 동시 가능), 0=off */
    uint32_t x3f8_tx_count;   /* 실험 에코 송신 누적 */
    uint8_t  x3f8_raw[8];     /* 마지막 수신 0x3F8 8바이트 (대시보드 리드백) */
    bool     x3f8_seen;

    uint32_t das_seen_ms;
    uint32_t ap_engaged_since_ms;

    /* 서먼 정책 입력 (ev-open SummonInjectionSnapshot) */
    uint8_t  di_gear;         /* 0x118 DI_gear 1=P 2=R 3=N 4=D */
    bool     di_seen;
    uint32_t di_seen_ms;
    uint16_t di_speed_raw;    /* 0x257 DI_vehicleSpeed raw, 500=정지 */
    bool     speed_seen;
    uint32_t speed_seen_ms;
    bool     spr_frame_seen;  /* 0x3F8 dlc8 수신 */
    uint32_t spr_frame_ms;

    /* 서먼 감지 (ev-open: ACA && 요청확정) */
    bool     spr_confirmed;
    uint32_t spr_confirmed_ms;
    bool     summoning;

    /* 0x129 */
    float    steering_deg;

    /* OTA */
    uint8_t  ota_assert;
    uint8_t  ota_clear;
    bool     ota_in_progress;

    /* 잔소리 내부 */
    uint8_t  das_prev_hands_on;
    bool     nag_demand_active;
    int16_t  nag_torq_walk;
    uint8_t  nag_exc_frames;
    uint16_t nag_frames_until_exc;
    uint32_t nag_prng;

    /* EPAS 충실 파형 상태머신 */
    uint8_t  f_prev_das;
    uint32_t f_stage1_ms;
    uint32_t f_stage2_ms;
    uint32_t f_strong_ms;
    int16_t  f_last_raw;
    int16_t  f_mild_walk;
    uint32_t f_s2_hold_until_ms;
    int16_t  f_s2_hold_raw;
    bool     f_s2_level2;

    /* 통계 */
    uint32_t nag_echo_count;
    uint32_t apctl_tx_count;
    uint32_t tx_fail_count;
    uint32_t rx_can0, rx_can1, err_can0, err_can1;
} FSDState;

void fsd_state_init(FSDState* s);
