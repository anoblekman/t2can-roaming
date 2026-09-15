/*
 * can_driver.cpp — T-2CAN 전용 CAN 드라이버.
 *
 * 실크 A = MCP2515 (SPI) = 논리 can1
 * 실크 B = TWAI (ESP32 내장) = 논리 can0
 *
 * 원본 esp32/.firmware/can_driver.cpp 에서 다음만 이식:
 *   - TWAI 초기화 (500kbps, 수신 큐 32)
 *   - MCP2515 초기화 (MCP_16MHZ, 500kbps, 필터 개방)
 *   - can_receive: TWAI 논블로킹 + MCP2515 INT 폴링
 *   - can_send: 버스별 송신
 *   - can_poll_bus_health: TWAI BUS_OFF 감지/복구
 *   - 하드웨어 리슨-온리 <-> Normal 전환 (원본 install_and_start/setListenOnly 패턴)
 *
 * 제외: 딥슬립, GVRET, 필터 프로그래밍, 통계 출력, LED/디스플레이.
 *
 * 리슨 모드는 소프트웨어 게이트(main.cpp 의 fsd_can_transmit)만으로는 부족하다 —
 * Normal 모드인 노드는 데이터 프레임을 안 보내더라도 ACK 비트, 에러 프레임 등
 * dominant 비트를 실차 버스에 낼 수 있다(테슬라의 CAN 참여 탐지 = 밴 리스크).
 * 그래서 이 드라이버는 두 버스 모두 하드웨어 Listen-Only 로 부팅하고(원본의
 * TWAI_MODE_LISTEN_ONLY / MCP setListenOnlyMode() 와 동일), Normal 전환은
 * can_set_active() 를 통해서만 일어난다. active=false 인 동안은 물리적으로
 * 송신 불가능 — 소프트웨어 게이트가 뚫려도 dominant 비트가 나갈 수 없다.
 *
 * can_set_active() 는 이 태스크에서 아무 데도 연결하지 않는다. active 토글이
 * 실제로 바뀔 때 호출하는 배선은 Task 12 의 몫이다.
 */

#include "can_driver.h"
#include "config.h"

#include <SPI.h>
#include <mcp2515.h>
#include "driver/twai.h"
#include <string.h>

// ── MCP2515 (실크 A = can1) ─────────────────────────────────────────────────
static MCP2515 s_mcp(MCP_CS_PIN);
static bool    s_mcp_ok          = false;
static bool    s_mcp_listen_only = true;   /* 현재 하드웨어 모드. 부팅 기본값 = Listen-Only */

static bool mcp_init_bus() {
    pinMode(MCP_INT_PIN, INPUT_PULLUP);

    // RST 핀 수동 토글: LOW ~10ms -> HIGH, reset() 호출 전에.
    pinMode(MCP_RST_PIN, OUTPUT);
    digitalWrite(MCP_RST_PIN, LOW);
    delay(10);
    digitalWrite(MCP_RST_PIN, HIGH);
    delay(10);

    SPI.begin(MCP_SCK_PIN, MCP_MISO_PIN, MCP_MOSI_PIN, MCP_CS_PIN);

    s_mcp.reset();

    if (s_mcp.setBitrate(CAN_500KBPS, MCP_16MHZ) != MCP2515::ERROR_OK) {
        Serial.println("[CAN] A(MCP2515) setBitrate 실패 — SPI/배선 확인");
        return false;
    }
    // 필터 개방: setFilter*/setFilterMask 를 호출하지 않으면 autowp 드라이버는
    // 기본적으로 모든 프레임을 수신한다 (필터 프로그래밍은 이 태스크 범위 밖).
    // 부팅 기본 모드는 Listen-Only — 칩이 물리적으로 송신 불가능한 상태로 시작한다.
    if (s_mcp.setListenOnlyMode() != MCP2515::ERROR_OK) {
        Serial.println("[CAN] A(MCP2515) setListenOnlyMode 실패");
        return false;
    }
    s_mcp_listen_only = true;
    return true;
}

static bool mcp_receive(CanFrame* frame) {
    if (!s_mcp_ok) return false;
    // INT 핀이 LOW 일 때만 SPI 로 폴링 (active-LOW 인터럽트)
    if (digitalRead(MCP_INT_PIN) != LOW) return false;

    struct can_frame f;
    if (s_mcp.readMessage(&f) != MCP2515::ERROR_OK) return false;

    frame->id  = f.can_id & CAN_EFF_MASK;
    frame->dlc = f.can_dlc;
    memcpy(frame->data, f.data, f.can_dlc);
    return true;
}

static bool mcp_send(const CanFrame& frame) {
    if (!s_mcp_ok || s_mcp_listen_only) return false;
    struct can_frame f;
    f.can_id  = frame.id;
    f.can_dlc = frame.dlc;
    memcpy(f.data, frame.data, frame.dlc);
    return s_mcp.sendMessage(&f) == MCP2515::ERROR_OK;
}

// ── TWAI (실크 B = can0) ────────────────────────────────────────────────────
static bool s_twai_ok          = false;
static bool s_twai_listen_only = true;   /* 현재 하드웨어 모드. 부팅 기본값 = Listen-Only */
static bool s_twai_recovering  = false;

static bool twai_install_and_start(bool listen_only) {
    twai_general_config_t g = TWAI_GENERAL_CONFIG_DEFAULT(
        (gpio_num_t)TWAI_TX_PIN, (gpio_num_t)TWAI_RX_PIN,
        listen_only ? TWAI_MODE_LISTEN_ONLY : TWAI_MODE_NORMAL);
    g.rx_queue_len = 32;

    twai_timing_config_t t = TWAI_TIMING_CONFIG_500KBITS();
    /* 버스 B(Chassis)는 초당 수천 프레임이라 ACCEPT_ALL 이면 RX 큐가 넘쳐 저빈도
       (~2Hz)인 0x39B 가 밀려 유실된다(실차 50445de6: B 우리 프레임 전멸, oth 6400
       폭주 → DAS_STALE). 그래서 하드웨어 필터로 필요한 ID 만 칩 단계에서 통과시킨다.

       듀얼 필터(진단): 0x39B(DAS, 필수) + 0x286(DI_state, 자동주차 래치 후보 관측).
       0x286 이 Chassis 에 오는지 확인용 — Party 엔 없음 확정(02b79efb). 0x286 은
       ~10Hz 라 0x39B(2Hz)와 합쳐도 큐 여유(깊이 32) 충분, 홍수 재발 없음.

       SJA1000/TWAI 듀얼 필터 std 프레임 비트배치(검증: docs v5.1.4 + 우리 단일필터
       실증): Filter1 ID = 비트[31:21] = id<<21, Filter2 ID = 비트[15:5] = id<<5.
       mask 1=무시/0=일치필수 → ~((0x7FF<<21)|(0x7FF<<5)) = 0x001F001F 가 두 ID 의
       11비트만 일치요구, RTR·데이터니블은 무시. 프레임은 두 필터 중 하나만 맞으면
       통과(OR). 0x286 결과 나오면 단일 0x39B 로 되돌릴지 결정. */
    /* 듀얼 필터: 0x39B(DAS, 필수) + 0x286(DI_state 관측). Chassis 는 초당 수천 프레임
       이라 ACCEPT_ALL 이면 저빈도 0x39B 가 밀려 유실(50445de6). 하드웨어 필터로 필요한
       ID 만 칩 단계에서 통과시킨다. (0x370 을 여기 추가하면 Chassis 로 echo 가 나가
       차량 경고/회생제동 정지를 유발 → 절대 추가 금지.) */
    const uint32_t das_id = 0x39Bu;   /* Filter 1 */
    const uint32_t di_id  = 0x286u;   /* Filter 2 — DI_state 관측 */
    twai_filter_config_t f = {
        .acceptance_code = (das_id << 21) | (di_id << 5),
        .acceptance_mask = ~(((uint32_t)0x7FFu << 21) | ((uint32_t)0x7FFu << 5)),
        .single_filter   = false,
    };

    if (twai_driver_install(&g, &t, &f) != ESP_OK) return false;
    if (twai_start() != ESP_OK) {
        twai_driver_uninstall();
        return false;
    }
    s_twai_listen_only = listen_only;
    s_twai_recovering  = false;
    return true;
}

static void twai_stop_and_uninstall() {
    twai_stop();
    twai_driver_uninstall();
    s_twai_recovering = false;
}

/* 재부팅(OTA) 직전 호출. 두 CAN 컨트롤러를 깨끗이 내리고 TX 핀을 recessive 로
   붙잡아, 리셋 순간 TX 가 dominant 로 튀어 Chassis 버스를 교란(→ DAS fault)하는
   것을 줄인다. flipper/우리 기존 코드는 이 정리 없이 곧장 ESP.restart() 했다.
   실리콘 리셋 자체의 순간 글리치까지 없애진 못할 수 있다(완화, 확정 아님). */
void can_shutdown_safe() {
    /* TWAI(Chassis, B): 구동 중단 → TX 핀을 풀업 recessive 로 고정 */
    if (s_twai_ok) {
        twai_stop();
        twai_driver_uninstall();
        s_twai_ok = false;
    }
    pinMode((gpio_num_t)TWAI_TX_PIN, INPUT_PULLUP);   /* recessive = HIGH */
    /* MCP2515(Party, A): 리셋하면 config 모드 → TX recessive */
    if (s_mcp_ok) {
        s_mcp.reset();
        s_mcp_ok = false;
    }
    delay(50);   /* 버스가 recessive 로 안정될 시간 */
}

static bool twai_receive_one(CanFrame* frame) {
    if (!s_twai_ok) return false;
    twai_message_t msg;
    if (twai_receive(&msg, 0) != ESP_OK) return false;  // 논블로킹 (timeout=0)
    frame->id  = msg.identifier;
    frame->dlc = msg.data_length_code;
    memcpy(frame->data, msg.data, msg.data_length_code);
    return true;
}

static bool twai_send_one(const CanFrame& frame) {
    if (!s_twai_ok || s_twai_listen_only) return false;
    twai_message_t msg;
    memset(&msg, 0, sizeof(msg));
    msg.identifier       = frame.id;
    msg.data_length_code = frame.dlc;
    memcpy(msg.data, frame.data, frame.dlc);
    return twai_transmit(&msg, pdMS_TO_TICKS(5)) == ESP_OK;
}

static void twai_poll_health() {
    if (!s_twai_ok) return;
    twai_status_info_t info;
    if (twai_get_status_info(&info) != ESP_OK) return;

    if (info.state == TWAI_STATE_BUS_OFF && !s_twai_recovering) {
        if (twai_initiate_recovery() == ESP_OK) {
            s_twai_recovering = true;
            Serial.println("[CAN] B(TWAI) bus-off 감지 — 복구 시작");
        }
    } else if (s_twai_recovering && info.state == TWAI_STATE_STOPPED) {
        if (twai_start() == ESP_OK) {
            s_twai_recovering = false;
            Serial.println("[CAN] B(TWAI) 복구 완료 — 재시작됨");
        }
    }
}

// ── 공개 API ────────────────────────────────────────────────────────────────
bool can_init() {
    s_mcp_ok  = mcp_init_bus();
    Serial.printf("[CAN] A(MCP2515) %s Listen-Only @ %d kbps (CS=%d SCK=%d MOSI=%d MISO=%d RST=%d INT=%d)\n",
                  s_mcp_ok ? "OK" : "FAILED", CAN_BITRATE_KBPS,
                  MCP_CS_PIN, MCP_SCK_PIN, MCP_MOSI_PIN, MCP_MISO_PIN, MCP_RST_PIN, MCP_INT_PIN);

    s_twai_ok = twai_install_and_start(/*listen_only=*/true);
    Serial.printf("[CAN] B(TWAI) %s Listen-Only @ %d kbps (TX=%d RX=%d)\n",
                  s_twai_ok ? "OK" : "FAILED", CAN_BITRATE_KBPS, TWAI_TX_PIN, TWAI_RX_PIN);

    return s_mcp_ok && s_twai_ok;
}

bool can_receive(CanBusId* bus, CanFrame* frame) {
    if (twai_receive_one(frame)) {
        *bus = CAN_B_TWAI;
        return true;
    }
    if (mcp_receive(frame)) {
        *bus = CAN_A_MCP;
        return true;
    }
    return false;
}

bool can_send(CanBusId bus, const CanFrame& frame) {
    if (bus == CAN_B_TWAI) return twai_send_one(frame);
    return mcp_send(frame);
}

void can_poll_bus_health() {
    twai_poll_health();
}

void can_read_errors(uint32_t* err_mcp_a, uint32_t* err_twai_b) {
    /* MCP2515: REC 레지스터(errorCountRX). SPI 읽기라 호출부에서 주기 제한한다. */
    *err_mcp_a = s_mcp_ok ? (uint32_t)s_mcp.errorCountRX() : 0u;

    /* TWAI: 상태 구조체의 수신 에러 카운터. */
    twai_status_info_t info;
    *err_twai_b = (s_twai_ok && twai_get_status_info(&info) == ESP_OK)
                      ? info.rx_error_counter : 0u;
}

/* active_mode==true  -> 두 버스 모두 Normal (송신 가능)
   active_mode==false -> 두 버스 모두 Listen-Only (물리적으로 송신 불가능)
   이미 원하는 모드면 아무 것도 하지 않는다 (멱등 — TWAI 재설치를 반복하지 않음).
   이 함수를 언제 호출할지는 이 태스크의 범위 밖이다 (Task 12: active 토글 배선). */
void can_set_active(bool active_mode) {
    bool want_listen_only = !active_mode;

    // MCP2515 (실크 A = can1)
    if (s_mcp_ok && s_mcp_listen_only != want_listen_only) {
        MCP2515::ERROR err = want_listen_only ? s_mcp.setListenOnlyMode() : s_mcp.setNormalMode();
        if (err == MCP2515::ERROR_OK) {
            s_mcp_listen_only = want_listen_only;
            Serial.printf("[CAN] A(MCP2515) 모드 전환 -> %s\n",
                          want_listen_only ? "Listen-Only" : "Normal");
        } else {
            Serial.println("[CAN] A(MCP2515) 모드 전환 실패");
        }
    }

    // TWAI (실크 B = can0) — 모드 전환은 드라이버 재설치가 필요하다 (원본 setListenOnly 패턴)
    if (s_twai_ok && s_twai_listen_only != want_listen_only) {
        twai_stop_and_uninstall();
        if (twai_install_and_start(want_listen_only)) {
            Serial.printf("[CAN] B(TWAI) 모드 전환 -> %s\n",
                          want_listen_only ? "Listen-Only" : "Normal");
        } else {
            s_twai_ok = false;
            Serial.println("[CAN] B(TWAI) 모드 전환 실패 — 버스 중단됨");
        }
    }
}
