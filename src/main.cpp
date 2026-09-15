#include <Arduino.h>
#include "can_driver.h"
#include "config.h"
#include "prefs.h"
#include "wifi_ap.h"
#include "web_ui.h"
#include "trace_log.h"
extern "C" {
#include "fsd_apctl.h"
#include "fsd_das.h"
#include "fsd_gate.h"
#include "fsd_nag.h"
#include "fsd_state.h"
#include "fsd_x3f8.h"
}

FSDState g_state;
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

/* ── PSRAM 진단 (§9 프레임 덤프가 ps_malloc(2MB) 로 0바이트 = 실패인 원인 확정용)
   부팅 시 실제 PSRAM 크기를 여기 담아 /api/status·대시보드·트레이스 헤더가
   읽는다. 0 이면 보드에 PSRAM 이 없거나(또는 타입 설정 불일치) 초기화 실패 —
   그 경우 덤프는 버퍼를 못 잡아 0바이트가 된다. core 1(setup)에서 한 번만 쓰고
   이후엔 읽기만 하므로 락 불필요. */
size_t g_psram_size = 0;

/* ── 웹 OTA 진행 플래그 (core 0 쓰기 / core 1 읽기) ─────────────────────────
   web_ui.cpp(core 0)의 OTA 핸들러가 true 로 세운다. loop()(core 1)의 하드웨어
   리컨실러가 이 플래그를 보고 OTA 동안 버스를 물리적으로 Listen-Only 로 내린다.
   g_state.active 원본은 건드리지 않으므로 NVS 오염이 없고, core 0 는 g_state 를
   직접 쓰지 않는다는 동시성 규칙도 지킨다. 단일 volatile bool(한쪽 쓰기/한쪽
   읽기)이라 이 자문형 플래그엔 충분하다. */
volatile bool g_web_ota_active = false;

FSDState state_snapshot() {
    portENTER_CRITICAL(&g_mux);
    FSDState copy = g_state;
    portEXIT_CRITICAL(&g_mux);
    return copy;
}

/* ── 웹 태스크(core 0) -> core 1 로 토글 변경을 넘기는 통로 ─────────────────
   web_ui.cpp 는 g_state 를 절대 직접 쓰지 않는다. /api/set 이 여기 있는
   g_pending_* 에만 적어 두면, loop() 가 매 루프 맨 앞에서 apply_pending() 을
   불러 lock 아래서 g_state 로 흡수하고 NVS 에 저장한다 — NVS 쓰기(prefs_save)
   가 core 1 에서 일어나야 한다는 게 핵심이라 여기 둔다. */
/* g_pending_* 는 이 파일 안에서만 만진다 (web_ui 는 pending_commit() 으로만
   접근). volatile 불필요 — 접근은 전부 portMUX 임계구역 안에서 일어난다. */
static bool     g_pending_valid = false;
static bool     g_pending_act, g_pending_nag, g_pending_smn;
static uint8_t  g_pending_wave;
static uint8_t  g_pending_x3f8;

/* web_ui(core 0) 가 /api/set 처리에서 부른다. seed(현재 g_state)+필드 덮어쓰기를
   g_mux 하나로 원자적으로 처리한다. g_state 를 직접 읽으므로 state_snapshot()
   을 중첩 호출하지 않는다 — portMUX 는 재귀 불가라 중첩하면 데드락이다. */
void pending_commit(PendingField field, bool bv, uint8_t wave) {
    portENTER_CRITICAL(&g_mux);
    if (!g_pending_valid) {
        g_pending_act  = g_state.active;
        g_pending_nag  = g_state.nag_kill;
        g_pending_smn  = g_state.summon_unlock;
        g_pending_wave = (uint8_t)g_state.wave;
        g_pending_x3f8 = g_state.x3f8_exp;
    }
    switch (field) {
        case PF_ACT:  g_pending_act  = bv;   break;
        case PF_NAG:  g_pending_nag  = bv;   break;
        case PF_SMN:  g_pending_smn  = bv;   break;
        case PF_WAVE: g_pending_wave = wave; break;
        case PF_X3F8: g_pending_x3f8 = wave; break;   /* wave 슬롯에 실험 인덱스(0~7) */
    }
    g_pending_valid = true;   /* 마지막에 세워야 core 1 이 흡수를 시작한다 */
    portEXIT_CRITICAL(&g_mux);
}

void apply_pending() {
    portENTER_CRITICAL(&g_mux);
    if (!g_pending_valid) { portEXIT_CRITICAL(&g_mux); return; }
    g_state.active        = g_pending_act;
    g_state.nag_kill      = g_pending_nag;
    g_state.summon_unlock = g_pending_smn;
    g_state.wave          = (NagWave)g_pending_wave;
    g_state.x3f8_exp      = g_pending_x3f8;
    g_pending_valid = false;
    portEXIT_CRITICAL(&g_mux);
    prefs_save(&g_state);
}

/* route() 는 상태 판단/디코딩만 한다 — SPI(MCP2515)/TWAI I/O 인 can_send() 는
   여기서 절대 호출하지 않는다. portENTER_CRITICAL 스핀락 아래서 SPI 트랜잭션을
   돌리면 다른 코어를 필요 이상 오래 멈춰 세우고, 인터럽트 지연으로 이어질 수
   있다. 대신 보낼 프레임이 있으면 TxRequest 에 적어 돌려주고, 실제 송신은
   loop() 가 락을 놓은 뒤에 한다. */
enum TxKind { TX_NONE = 0, TX_NAG_ECHO, TX_APCTL_MIRROR, TX_X3F8_ECHO };

struct TxRequest {
    TxKind   kind;
    CanBusId bus;
    CanFrame frame;
};

static void route(CanBusId bus, const CanFrame& f, TxRequest* tx) {
    uint32_t now = millis();
    tx->kind = TX_NONE;

    bool das_decoded = fsd_das_handle(&g_state, &f, now);
    if (das_decoded) return;   /* 디코드된 DAS 는 여기서 끝 */
    if (fsd_ota_handle(&g_state, &f)) return;
    if (fsd_aux_handle(&g_state, &f, now)) {
        /* 0x3F8 실험 하네스: 실험 선택 + 활성모드 + 잔소리 게이트(AP 3~5·1초 안정)
           통과 시에만 에코. 게이트/리슨 판정은 여기서 건다 — aux_handle 은 디코드만.
           0x3F8 = 0x3F8u. */
        if (f.id == 0x3F8u && g_state.x3f8_exp != 0u &&
            fsd_can_transmit(&g_state) && fsd_ap_ok(&g_state, now)) {
            CanFrame echo;
            if (fsd_x3f8_build(&g_state, &f, &echo)) {
                tx->kind  = TX_X3F8_ECHO;
                tx->bus   = bus;
                tx->frame = echo;
            }
        }
        return;   /* 0x3F8/0x118/0x257 은 여기서 끝 */
    }

    if (f.id == 0x129u && f.dlc >= 4) {
        int16_t raw = (int16_t)(((uint16_t)f.data[1] << 8) | f.data[0]);
        g_state.steering_deg = (float)raw * 0.1f;
        return;
    }

    if (!fsd_can_transmit(&g_state)) return;   /* 리슨 모드면 여기서 끝 */

    if (f.id == 0x370u && g_state.nag_kill && fsd_ap_ok(&g_state, now)) {
        CanFrame echo;
        if (fsd_nag_build(&g_state, &f, &echo, now)) {
            tx->kind  = TX_NAG_ECHO;
            tx->bus   = bus;
            tx->frame = echo;
        }
        return;
    }
    if (fsd_apctl_is_mux1(&f) && fsd_apctl_should_send(&g_state, now)) {
        CanFrame copy;
        fsd_apctl_build(&f, &copy);
        tx->kind  = TX_APCTL_MIRROR;
        tx->bus   = bus;
        tx->frame = copy;
        return;
    }
}

/* ── BOOT 버튼 (GPIO0, active-LOW, 내부 풀업) ───────────────────────────────
   core 1(loop) 에서만 부른다 — 그래서 g_mux 아래 g_state 를 직접 토글해도
   안전하다(웹/core 0 는 pending 경유). 두 제스처:
     · 짧게(50ms~1s)  = 리슨 <-> 활성 모드 토글 + NVS 저장
     · 부팅 20초 내 5초 롱프레스 = 공장 초기화(prefs_reset) 후 재부팅
   50ms 하한은 디바운스(칩 노이즈/채터 무시). */
static void poll_button() {
    static uint32_t pressed_at = 0;
    static bool     was_down   = false;
    bool down = (digitalRead(BOOT_BTN_PIN) == LOW);
    uint32_t now = millis();
    if (down && !was_down) pressed_at = now;
    if (!down && was_down) {
        uint32_t held = now - pressed_at;
        if (held >= 5000u && now < 20000u) {        /* 부팅 20초 내 5초 롱프레스 = 공장초기화 */
            prefs_reset();
            ESP.restart();
        } else if (held >= 50u && held < 1000u) {    /* 짧게 = 리슨/활성 토글 (50ms 하한 = 디바운스) */
            portENTER_CRITICAL(&g_mux);
            g_state.active = !g_state.active;
            portEXIT_CRITICAL(&g_mux);
            prefs_save(&g_state);
        }
    }
    was_down = down;
}

void setup() {
    Serial.begin(115200);
    fsd_state_init(&g_state);
    prefs_begin();
    prefs_load(&g_state);
    pinMode(BOOT_BTN_PIN, INPUT_PULLUP);
    if (!can_init()) Serial.println("[CAN] init failed");
    Serial.printf("t2can-fsd %s — 리슨 모드로 시작\n", FW_VERSION);

    /* PSRAM 상태를 부팅 로그에 남기고 전역에 보관 — 덤프 0바이트(ps_malloc 실패)
       원인 확정용. size==0 이면 PSRAM 없음/미초기화 → 덤프 불가. */
    g_psram_size = ESP.getPsramSize();     /* 0 이면 PSRAM 없음/미초기화 */
    Serial.printf("[PSRAM] found=%d size=%u free=%u\n",
                  (int)psramFound(), (unsigned)ESP.getPsramSize(), (unsigned)ESP.getFreePsram());

    trace_begin();   /* §4.8 게이트 트레이스: LittleFS 마운트 + 세션 헤더 예약 (항상 켜짐) */

    wifi_ap_start();
    web_begin();
    xTaskCreatePinnedToCore(web_task, "web", 8192, nullptr, 1, nullptr, 0);
}

void loop() {
    apply_pending();
    poll_button();   /* core 1 이라 g_mux 아래 g_state 직접 토글 OK */

    /* 하드웨어 모드를 g_state.active 에 맞춰 매 루프 재동기화한다. 이렇게
       중앙화해 두면 active 를 바꾸는 쪽(NVS 로드, 향후 웹/버튼 토글)이 뭐가 됐든
       can_set_active() 를 직접 호출할 필요가 없다 — 다음 루프에서 자동으로
       맞춰진다. can_set_active() 는 멱등이라 값이 안 바뀌면 그냥 아무 일도
       안 한다. active 는 이 태스크 안에서 core 1(이 루프) 외에는 아무도 안 쓰므로
       평범한 읽기로 충분하다. */
    static int8_t s_hw_active = -1;   /* -1 = 부팅 직후, 아직 한 번도 동기화 안 함 */
    /* 웹 OTA 중이면 want_active 를 false 로 눌러 리컨실러가 버스를 Listen-Only 로
       내린다 — 플래시 쓰는 동안 물리적으로 송신을 멈춘다. g_state.active 원본은
       안 건드리므로 OTA 실패/취소 시 다음 루프에서 원래 모드로 자동 복귀한다. */
    bool want_active = g_state.active && !g_web_ota_active;
    if ((int8_t)want_active != s_hw_active) {
        can_set_active(want_active);   /* Listen-Only <-> Normal */
        s_hw_active = (int8_t)want_active;
    }

    CanBusId bus;
    CanFrame f;
    while (can_receive(&bus, &f)) {
        if (bus == CAN_B_TWAI) g_state.rx_can0++; else g_state.rx_can1++;
        trace_note_rx((uint8_t)bus, f.id);   /* 진단: 버스·ID별 도착 집계 (core 1) */
        if (f.dlc == 8) trace_note_raw(f.id, f.data, millis());   /* 진단: 0x3F8/0x39B/0x286 원시 바이트 변화 */

        TxRequest tx;
        portENTER_CRITICAL(&g_mux);
        route(bus, f, &tx);
        portEXIT_CRITICAL(&g_mux);

        if (tx.kind != TX_NONE) {
            bool sent = can_send(tx.bus, tx.frame);   /* 락 밖에서 SPI/TWAI I/O */
            if (sent) {
                portENTER_CRITICAL(&g_mux);
                switch (tx.kind) {
                    case TX_NAG_ECHO:     g_state.nag_echo_count++; break;
                    case TX_APCTL_MIRROR: g_state.apctl_tx_count++; break;
                    case TX_X3F8_ECHO:    g_state.x3f8_tx_count++;  break;
                    default: break;
                }
                portEXIT_CRITICAL(&g_mux);
            }
        }

        /* §4.8 게이트 트레이스 stepping: g_state 를 읽어 전이/판정 줄을 RAM
           버퍼에 적재. g_mux 임계구역 **밖**에서 부른다 — g_state 읽기는
           core 1 단독이라 락이 필요 없다. 매 프레임 불러도 fsd_trace_step 이
           변화 없으면 0 을 반환해 값싸다(전이를 놓치지 않으려 rate-limit 안 함). */
        trace_step(&g_state, millis());
    }
    can_poll_bus_health();

    static uint32_t last = 0;
    if (millis() - last >= 2000) {
        last = millis();
        /* 현재 수신 에러 카운터(REC) 스냅샷. 매 루프가 아니라 여기(2초)에서만
           읽어 MCP2515 SPI 레지스터 읽기 비용을 아낀다. rx_can* 와 같은 규칙으로
           core 1 에서만 쓰므로 락 없이 쓴다(스칼라, 읽는 쪽은 스냅샷). */
        can_read_errors(&g_state.err_can1, &g_state.err_can0);  /* A=can1(MCP), B=can0(TWAI) */
        Serial.printf("[RX] A=%lu B=%lu | AP=%u hands=%u summon_avail=%u fwd_leash=%u\n",
                      (unsigned long)g_state.rx_can1, (unsigned long)g_state.rx_can0,
                      g_state.das_ap_state, g_state.das_hands_on,
                      (unsigned)g_state.summon_available,
                      (unsigned)g_state.summon_fwd_leash);

        /* §4.8: 초당 송신집계 줄 적재 + RAM 버퍼를 LittleFS 로 비움(주기적,
           매 줄 아님 — flash 마모 방지). 둘 다 core 1. */
        trace_tick_tx(&g_state, millis());
        trace_flush();
    }
}
