#pragma once

/* CAN ID */
#define ID_STEER_ANGLE   0x129u
#define ID_GTW_CAR_STATE 0x318u
#define ID_EPAS_STATUS   0x370u
#define ID_DAS_STATUS_A  0x399u   /* 구형 프레임. 0x39B 미수신 시에만 사용 */
#define ID_DAS_STATUS_B  0x39Bu   /* 우선 */
#define ID_AP_CONTROL    0x3FDu
#define ID_UI_DRIVERASSIST  0x3F8u   /* 1016, Party. selfParkRequest */
#define ID_DI_SYSTEMSTATUS  0x118u   /* 280.  DI_autonomyControlActive (ACA) */
#define UI_SELFPARK_BYTE    3        /* byte3 상위니블 */
#define UI_SELFPARK_SHIFT   4
#define UI_SELFPARK_MASK    0x0Fu
#define DI_ACA_BYTE         6        /* byte6 bit2 (DBC bit50) */
#define DI_ACA_MASK         0x04u
#define DI_GEAR_BYTE        2        /* 0x118 DI_gear: byte2 bit5..7 */
#define DI_GEAR_SHIFT       5
#define DI_GEAR_MASK        0x07u
#define DI_GEAR_P           1u
#define DI_GEAR_R           2u
#define DI_GEAR_N           3u
#define DI_GEAR_D           4u
#define ID_DI_SPEED         0x257u   /* 599. DI_vehicleSpeed bit12..23, 0.08kph, -40 */
#define DI_SPEED_STATIONARY_RAW 500u     /* 정확히 0kph */
#define DI_SPEED_MAX_VALID_RAW  4062u
#define DI_SPEED_SNA_RAW        4095u

/* 서먼 정책 (ev-open injection_policy.h) */
/* 기어·속도·DAS 신선도. 이 차 0x39B/0x118 이 ev-open 배선보다 느려 500ms 면
   정상인데도 깜빡인다. flipper 잔소리·EU지속, ev-open 잔소리 모드C 가 쓰는 1000ms 로. */
#define SUMMON_FRESH_MS          1000u
#define SUMMON_REQ_LEAD_MS       2000u   /* ACA 상승 시 요청 확정이 이보다 오래됐으면 해제 */
#define SUMMON_REQ_MAX_VALID     12u

/* 0x39B / 0x399 — opendbc BO_923 DAS_status. 두 프레임 레이아웃 동일 */
#define DAS_AP_STATE_BYTE        0
#define DAS_AP_STATE_MASK        0x0Fu
#define DAS_SPEED_LIMIT_BYTE     1
#define DAS_SPEED_LIMIT_MASK     0x1Fu
#define DAS_SUMMON_OBSTACLE_BYTE 1
#define DAS_SUMMON_OBSTACLE_MASK 0x40u
#define DAS_SUMMON_GATE_BYTE     1
#define DAS_SUMMON_GATE_MASK     0x80u
#define DAS_AUTOPARK_READY_BYTE  3
#define DAS_AUTOPARK_READY_MASK  0x01u
#define DAS_AUTO_PARKED_BYTE     3
#define DAS_AUTO_PARKED_MASK     0x02u
#define DAS_FWD_LEASH_BYTE       3
#define DAS_FWD_LEASH_MASK       0x10u
#define DAS_RVS_LEASH_BYTE       3
#define DAS_RVS_LEASH_MASK       0x20u
#define DAS_HANDS_ON_BYTE        5
#define DAS_HANDS_ON_SHIFT       2
#define DAS_HANDS_ON_MASK        0x0Fu
#define DAS_SUMMON_AVAIL_BYTE    6
#define DAS_SUMMON_AVAIL_MASK    0x08u

#define DAS_APSTATE_ENGAGED      3u    /* ACTIVE_NOMINAL */
#define DAS_APSTATE_NAG_MAX      5u    /* ACTIVE_NAV — 잔소리 주입은 3~5 에서만 */
#define DAS_APSTATE_FSD          6u    /* ACTIVE_FSD — 이 차에선 자동주차가 6 */
#define DAS_HANDS_NOT_REQUIRED   0u
#define DAS_HANDS_SUSPENDED      8u

/* 0x318 GTW_carState */
#define GTW_OTA_BYTE             6
#define GTW_OTA_MASK             0x03u
#define GTW_OTA_VALUE            2u    /* 값 1 은 이 차의 롤링카운터 오탐 */
#define OTA_ASSERT_FRAMES        3u
#define OTA_CLEAR_FRAMES         6u

/* 0x370 EPAS_sysStatus */
#define EPAS_TORQUE_HI_BYTE      2
#define EPAS_TORQUE_HI_KEEP      0xF0u
#define EPAS_TORQUE_HI_MASK      0x0Fu
#define EPAS_TORQUE_LO_BYTE      3
#define EPAS_HANDS_BYTE          4
#define EPAS_HANDS_SHIFT         6
#define EPAS_HANDS_MASK          0x03u
#define EPAS_HANDS_OK            1u
#define EPAS_HANDS_CLEAR         0xC0u
#define EPAS_HANDS_SPOOF         0x40u
#define EPAS_COUNTER_BYTE        6
#define EPAS_COUNTER_MASK        0x0Fu
#define EPAS_COUNTER_KEEP        0xF0u

/* 0x3FD mux1 */
#define AP_MUX_BYTE              0
#define AP_MUX_MASK              0x07u
#define AP_MUX1                  1u
#define AP_BIT19_BYTE            2
#define AP_BIT19_MASK            0x08u
#define AP_BIT47_BYTE            5
#define AP_BIT47_MASK            0x80u

/* 타이밍 / 안전 */
#define AP_STABLE_MS             1000u
#define DAS_FRESH_MS             500u
#define NAG_BURST_MS             1000u
#define NAG_PAUSE_MS             1500u
#define NAG_TORQUE_RAW_MIN       1870
/* +1.8 Nm. 2400(+3.5 Nm)으로 올려봤으나 (1) 일부 차(2026 하이랜드) 미해결(파형 3종
   다 실패=프레임 수용 문제지 토크 크기 아님) (2) 커뮤니티가 ">1.8 Nm 은 커브에서 AP
   해제 유발"로 캡하는 값 → 원복. 회피는 크기가 아니라 버스트 타이밍(1000/1500ms)으로 한다. */
#define NAG_TORQUE_RAW_MAX       2230
#define NAG_TORQUE_CENTER        2048
