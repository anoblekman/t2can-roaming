/*
 * trace_log.cpp — §4.8 게이트 결정 트레이스를 LittleFS 에 저장.
 *
 * 마모 방지: fsd_trace_step 이 내놓는 전이/판정 줄을 core 1 의 RAM 버퍼에
 *   append 만 한다(flash 안 건드림). 버퍼가 절반 차거나 loop() 의 2초 블록에서
 *   trace_flush() 가 버퍼를 LittleFS 로 비운다 — 매 줄 flush 하지 않는다.
 *
 * 링 파일: "/trace.log" 가 TRACE_CAP 을 넘으면 통째로 비우고(truncate) 세션
 *   헤더를 다시 쓴다. 진짜 바이트 링보다 단순·안전하고, 세션당 수십 KB라
 *   실사용에 문제없다.
 *
 * 동시성: LittleFS 접근(파일 열기/쓰기/읽기)은 전부 FreeRTOS 뮤텍스 s_mutex
 *   아래서 한다. RAM 버퍼(s_buf)와 헤더 소스(s_hsrc)는 core 1 만 쓰므로 그
 *   접근에는 락이 필요 없다. s_hdr_pending 은 core 0(clear)와 core 1(flush)이
 *   모두 만지므로 항상 뮤텍스 아래서 접근한다.
 */
#include "trace_log.h"
#include "config.h"
#include <LittleFS.h>
#include <string.h>
extern "C" {
#include "fsd_trace.h"
}

#define TRACE_PATH   "/trace.log"
#define TRACE_CAP    (2560u * 1024u)  /* 파일 상한 — 넘으면 truncate 후 헤더 재기록.
                                          LittleFS 파티션 3.4MB(0x360000)의 ~70%.
                                          링 한 바퀴 ≈ 6~7시간(RX 진단 상시 기록 기준). */
#define TRACE_BUF_SZ 3072u            /* RAM 버퍼(마모 방지) */
#define TRACE_LINE   512u             /* fsd_trace_step 한 번 출력의 최대치 */

/* main.cpp 가 setup() 에서 채우는 부팅 시 PSRAM 크기(바이트). 세션 헤더에 한 줄
   남겨 트레이스만 봐도 덤프 0바이트(ps_malloc 실패)가 PSRAM 부재 탓인지 안다. */
extern size_t g_psram_size;

static SemaphoreHandle_t s_mutex = nullptr;
static bool       s_mounted = false;
static TraceSnap  s_snap;

/* ── §서먼 관측: selfParkRequest(0x3F8)/ACA(0x118) 전이 기록 (core 1 단독) ────
   fsd_aux_handle 이 이미 g_state.self_park_req / g_state.di_aca 로 디코드해 둔다.
   여기선 그 값의 "변화"만 감지해 트레이스 줄로 남긴다 — 대시보드엔 보이지만
   트레이스(LittleFS)엔 안 찍혀서 원거리 서먼 시 기록이 안 남던 것을 메운다.
   logic/fsd_trace.* 는 손대지 않는다. append 는 기존 RAM 버퍼 헬퍼(append_buf)를
   재사용하므로 새 flash 접근·per-line flush 를 만들지 않는다. */
static uint8_t s_prev_spr    = 0xFF;
static bool    s_prev_aca    = false;
static bool    s_spr_primed  = false;   /* 첫 호출은 기준선만 잡는다 */

/* ── 웹/버튼 토글 기록: 사용자가 바꾼 설정(활성/잔소리/서먼/파형/0x3F8실험)의 전이를
   트레이스에 남긴다. apply_pending 이 g_state 에 흡수한 뒤 다음 trace_step 이 변화를
   감지 → "언제 뭘 켰나"를 트레이스만 보고 재구성 가능. core 1 단독. */
static bool    s_cfg_primed  = false;
static bool    s_pv_act, s_pv_nag, s_pv_smn;
static uint8_t s_pv_wave, s_pv_x3f8;

static char   s_buf[TRACE_BUF_SZ];
static size_t s_len = 0;

/* 헤더 재기록 필요 플래그(뮤텍스 아래서만 접근). 부팅/clear/cap-truncate 후 true. */
static bool s_hdr_pending = true;

/* 헤더 ① 소스값 — core 1(trace_step/tick)이 갱신, flush 가 읽는다(둘 다 core 1). */
static struct {
    bool     act, nag, smn;
    uint8_t  wave;
    uint32_t rxA, errA, rxB, errB;
    bool     have;
} s_hsrc = {0};

/* ④ 송신집계용 직전 카운트 */
static bool     s_tx_primed = false;
static uint32_t s_prev_nag = 0, s_prev_ap = 0, s_prev_x3f8tx = 0, s_prev_sec = 0;

/* ── 진단(RX 계측): 관심 프레임이 어느 버스에서 몇 개 오는지 창(2초)마다 집계 ──
   route() 가 매 프레임 trace_note_rx(bus,id) 로 센다(core 1 단독, 락 불필요).
   trace_tick_tx 가 창마다 줄로 내보내고 0 으로 리셋한다. 값 변화가 아니라
   "실제 도착 개수"라, 0x39B 가 안 오는지/오는데 우리가 떨구는지 구분 가능. */
static const uint32_t s_diag_ids[] = {
    0x39Bu, 0x399u, 0x370u, 0x3FDu, 0x3F8u, 0x118u, 0x257u, 0x129u, 0x318u, 0x286u
};
#define DIAG_N (sizeof s_diag_ids / sizeof s_diag_ids[0])
static const char* const s_diag_names[DIAG_N] = {
    "39B", "399", "370", "3FD", "3F8", "118", "257", "129", "318", "286"
};
static uint32_t s_rx_cnt[2][DIAG_N];   /* [bus][id] : bus 0=B(TWAI) 1=A(MCP) */
static uint32_t s_rx_other[2];         /* 표에 없는 나머지 프레임 총계 */

void trace_note_rx(uint8_t bus, uint32_t id) {
    uint8_t b = bus ? 1u : 0u;
    for (unsigned i = 0; i < DIAG_N; i++) {
        if (s_diag_ids[i] == id) { s_rx_cnt[b][i]++; return; }
    }
    s_rx_other[b]++;
}

/* ── 진단(원시 바이트): 차선변경 확인 제어(UI_ulcStalkConfirm 등)를 찾기 위해
   0x3F8(UI_driverAssistControl)·0x39B 의 바이트가 "의미있게" 바뀔 때만 8바이트
   전체를 기록한다.
   0x3F8: bit1=UI_ulcStalkConfirm, bit28~31=selfParkRequest(byte3 상위니블),
     bit38=undertakeAssistEnable, bit48~51=ulcSpeed/BlindSpotConfig, bit54=alcOffHwy.
     byte0 bit2~3 은 UI_summonHeartbeat(상시 변화)라 dedup 에서 마스크한다.
   0x39B: byte0~5 변화(byte6/7 카운터·체크섬 무시).
   0x286(DI_state): opendbc 자동주차 래치 신호가 우리 버스에 오는지 관측용.
     DI_autoparkState = byte3[4:1](값 3/4/9=자동주차), cruise_state = byte1[6:4].
     byte0=체크섬, byte1 하위니블=카운터라 dedup 에서 무시. Party 필터는 개방이라
     여기 오면 잡히고, Chassis 전용이면 0x39B 필터에 막혀 안 온다(2단계에서 판단). */
static uint8_t s_raw_3f8[8];  static bool s_raw_3f8_seen = false;
static uint8_t s_raw_39b[6];  static bool s_raw_39b_seen = false;
static uint8_t s_raw_286[8];  static bool s_raw_286_seen = false;
static uint8_t s_raw_370_key; static bool s_raw_370_seen = false;
static uint8_t s_raw_3fd2, s_raw_3fd5; static bool s_raw_3fd_seen = false;

static void append_buf(const char* p, size_t len);   /* 아래 정의 — 전방 선언 */

static void raw_emit(const char* tag, const uint8_t* d, uint32_t now_ms) {
    char l[80];
    int n = snprintf(l, sizeof l,
        "[%4lu.%03lu] %s %02X %02X %02X %02X %02X %02X %02X %02X\n",
        (unsigned long)(now_ms / 1000u), (unsigned long)(now_ms % 1000u), tag,
        d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
    if (n > 0) append_buf(l, (size_t)n);
}

void trace_note_raw(uint32_t id, const uint8_t* d, uint32_t now_ms) {
    if (id == 0x3F8u) {
        /* 진단: 롤링 카운터 유무 확인용 — dedup 없이 매 프레임 기록한다.
           0x3F8 은 ~1Hz 라 초당 1줄로 값싸다. 연속 프레임에서 어떤 바이트가 매번
           +1 씩 돌면 카운터 있음(주입 시 이어줘야), 전부 고정이면 카운터 없음.
           카운터/위치 확정 후 원래의 "변화 시에만" dedup 로 되돌린다. */
        (void)s_raw_3f8; (void)s_raw_3f8_seen;
        raw_emit("3F8 ", d, now_ms);
    } else if (id == 0x39Bu) {
        if (s_raw_39b_seen && memcmp(s_raw_39b, d, 6) == 0) return;
        memcpy(s_raw_39b, d, 6); s_raw_39b_seen = true;
        raw_emit("39B ", d, now_ms);
    } else if (id == 0x286u) {
        uint8_t m[8]; memcpy(m, d, 8);
        m[0] = 0;                          /* DI_stateChecksum(byte0) 무시 */
        m[1] = (uint8_t)(m[1] & 0xF0u);    /* DI_stateCounter(byte1 하위니블) 무시 */
        if (s_raw_286_seen && memcmp(s_raw_286, m, 8) == 0) return;
        memcpy(s_raw_286, m, 8); s_raw_286_seen = true;
        raw_emit("286 ", d, now_ms);       /* 로그는 원본 바이트 */
    } else if (id == 0x370u) {
        /* 잔소리 진단: EPAS3S_handsOnLevel(byte4[7:6]) + eacStatus(byte6[7:5]) 가
           바뀔 때만 기록. torsionBarTorque·조향각·카운터·체크섬은 상시 변해 무시.
           잔소리 뜰 때 handsOnLevel 이 실제로 어떤 값인지(0 로 떨어지나 1 로 버티나)
           확인용 — opendbc handsOnLevel VAL: 0/1/2/3 = LEVEL_0~3. */
        uint8_t key = (uint8_t)((d[4] & 0xC0u) | ((d[6] >> 5) & 0x07u));
        if (s_raw_370_seen && s_raw_370_key == key) return;
        s_raw_370_key = key; s_raw_370_seen = true;
        raw_emit("370 ", d, now_ms);
    } else if (id == 0x3FDu) {
        /* EU/서먼 주입 대상 mux1 관측: 우리가 넣는 비트가 든 byte2(bit19)·byte5
           (bit46/47) 가 바뀔 때만 기록 → 카운터 홍수 없이 차 native 상태 확인.
           우리 에코는 자기수신 안 하므로 로그는 항상 차 원본. mux0/2 는 무시. */
        if ((d[0] & 0x07u) != 1u) return;
        if (s_raw_3fd_seen && s_raw_3fd2 == d[2] && s_raw_3fd5 == d[5]) return;
        s_raw_3fd2 = d[2]; s_raw_3fd5 = d[5]; s_raw_3fd_seen = true;
        raw_emit("3FD1", d, now_ms);
    }
}

static bool lock()   { return s_mutex && xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE; }
static void unlock() { if (s_mutex) xSemaphoreGive(s_mutex); }

static const char* wave_name(uint8_t w) {
    switch (w) { case 0: return "basic"; case 1: return "burst"; case 2: return "faithful"; default: return "?"; }
}

/* 뮤텍스를 이미 쥔 상태에서 호출. 열린 파일에 세션 헤더 ① 를 쓴다. */
static void write_header_locked(File& f) {
    f.printf("# T2CAN-ROAMING %s  uptime-relative timestamps (no RTC)\n", FW_VERSION);
    f.printf("# 토글: nag=%s wave=%s summon=%s mode=%s\n",
             s_hsrc.nag ? "on" : "off",
             wave_name(s_hsrc.wave),
             s_hsrc.smn ? "on" : "off",
             s_hsrc.act ? "active" : "listen");
    f.printf("# 버스: A(Party) rx=%lu err=%lu / B(Chassis) rx=%lu err=%lu\n",
             (unsigned long)s_hsrc.rxA, (unsigned long)s_hsrc.errA,
             (unsigned long)s_hsrc.rxB, (unsigned long)s_hsrc.errB);
    if (g_psram_size)
        f.printf("# PSRAM: %u bytes\n", (unsigned)g_psram_size);
    else
        f.printf("# PSRAM: 없음\n");   /* 0 = 없음/미초기화 → §9 프레임 덤프 불가 */
}

void trace_begin() {
    s_mutex = xSemaphoreCreateMutex();
    fsd_trace_init(&s_snap);
    s_len = 0;
    s_hdr_pending = true;   /* 새 세션 헤더를 첫 flush 에서 쓴다(기존 로그엔 이어붙임) */

    /* 실패 시 포맷 후 재시도(true = 마운트 실패면 자동 포맷). */
    s_mounted = LittleFS.begin(true);
    if (!s_mounted) {
        Serial.println("[TRACE] LittleFS 마운트 실패 — 트레이스 비활성");
        return;
    }
    Serial.printf("[TRACE] LittleFS 마운트 — used=%u/%u\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
}

/* core 1 단독. 헤더 소스값 갱신(락 불필요 — 단일 기록자). */
static void capture_hdr(const FSDState* s) {
    s_hsrc.act  = s->active;
    s_hsrc.nag  = s->nag_kill;
    s_hsrc.smn  = s->summon_unlock;
    s_hsrc.wave = (uint8_t)s->wave;
    s_hsrc.rxA  = s->rx_can1;  s_hsrc.errA = s->err_can1;   /* A = can1(MCP) */
    s_hsrc.rxB  = s->rx_can0;  s_hsrc.errB = s->err_can0;   /* B = can0(TWAI) */
    s_hsrc.have = true;
}

/* core 1. RAM 버퍼에 append. 절반 차면 flush(주기적, 매 줄 아님). */
static void append_buf(const char* p, size_t len) {
    if (len == 0) return;
    if (s_len + len > TRACE_BUF_SZ) return;   /* flush 임계로 여기 도달하면 안 되지만 방어적으로 */
    memcpy(s_buf + s_len, p, len);
    s_len += len;
    if (s_len >= TRACE_BUF_SZ / 2) trace_flush();
}

void trace_step(const FSDState* s, uint32_t now_ms) {
    capture_hdr(s);
    char line[TRACE_LINE];
    size_t w = fsd_trace_step(s, &s_snap, now_ms, line, sizeof(line));
    if (w) append_buf(line, w);

    /* 서먼 관측 신호 전이 기록. 시간 포맷은 기존 트레이스 줄과 동일한
       "[%4lu.%03lu] " 스타일(now_ms/1000, now_ms%1000). 첫 호출은 기준선만 잡고
       이후 변화 시에만 append_buf 로 RAM 버퍼에 적재한다(기존 flush 주기 유지). */
    if (!s_spr_primed) {
        s_prev_spr   = s->self_park_req;
        s_prev_aca   = s->di_aca;
        s_spr_primed = true;
    } else {
        char l[64];
        if (s->self_park_req != s_prev_spr) {
            int n = snprintf(l, sizeof(l), "[%4lu.%03lu] self_park_req %u -> %u\n",
                             (unsigned long)(now_ms / 1000u), (unsigned long)(now_ms % 1000u),
                             (unsigned)s_prev_spr, (unsigned)s->self_park_req);
            if (n > 0) append_buf(l, (size_t)n);
            s_prev_spr = s->self_park_req;
        }
        if (s->di_aca != s_prev_aca) {
            int n = snprintf(l, sizeof(l), "[%4lu.%03lu] ACA %d -> %d\n",
                             (unsigned long)(now_ms / 1000u), (unsigned long)(now_ms % 1000u),
                             (int)s_prev_aca, (int)s->di_aca);
            if (n > 0) append_buf(l, (size_t)n);
            s_prev_aca = s->di_aca;
        }
    }

    /* 웹/버튼 토글 전이 기록 */
    if (!s_cfg_primed) {
        s_pv_act  = s->active;  s_pv_nag = s->nag_kill;  s_pv_smn = s->summon_unlock;
        s_pv_wave = (uint8_t)s->wave;  s_pv_x3f8 = s->x3f8_exp;
        s_cfg_primed = true;
    } else {
        char l[80];
        unsigned long sec = (unsigned long)(now_ms / 1000u), ms = (unsigned long)(now_ms % 1000u);
        if (s->active != s_pv_act) {
            int n = snprintf(l, sizeof l, "[%4lu.%03lu] TOGGLE mode %s -> %s\n", sec, ms,
                             s_pv_act ? "active" : "listen", s->active ? "active" : "listen");
            if (n > 0) append_buf(l, (size_t)n);  s_pv_act = s->active;
        }
        if (s->nag_kill != s_pv_nag) {
            int n = snprintf(l, sizeof l, "[%4lu.%03lu] TOGGLE nag %d -> %d\n", sec, ms,
                             (int)s_pv_nag, (int)s->nag_kill);
            if (n > 0) append_buf(l, (size_t)n);  s_pv_nag = s->nag_kill;
        }
        if (s->summon_unlock != s_pv_smn) {
            int n = snprintf(l, sizeof l, "[%4lu.%03lu] TOGGLE summon %d -> %d\n", sec, ms,
                             (int)s_pv_smn, (int)s->summon_unlock);
            if (n > 0) append_buf(l, (size_t)n);  s_pv_smn = s->summon_unlock;
        }
        if ((uint8_t)s->wave != s_pv_wave) {
            int n = snprintf(l, sizeof l, "[%4lu.%03lu] TOGGLE wave %u -> %u\n", sec, ms,
                             (unsigned)s_pv_wave, (unsigned)s->wave);
            if (n > 0) append_buf(l, (size_t)n);  s_pv_wave = (uint8_t)s->wave;
        }
        if (s->x3f8_exp != s_pv_x3f8) {
            int n = snprintf(l, sizeof l, "[%4lu.%03lu] TOGGLE x3f8 0x%02X -> 0x%02X\n", sec, ms,
                             (unsigned)s_pv_x3f8, (unsigned)s->x3f8_exp);
            if (n > 0) append_buf(l, (size_t)n);  s_pv_x3f8 = s->x3f8_exp;
        }
    }
}

void trace_tick_tx(const FSDState* s, uint32_t now_ms) {
    capture_hdr(s);
    uint32_t sec = now_ms / 1000u;
    if (!s_tx_primed) {
        s_prev_nag = s->nag_echo_count;
        s_prev_ap  = s->apctl_tx_count;
        s_prev_x3f8tx = s->x3f8_tx_count;
        s_prev_sec = sec;
        s_tx_primed = true;
        return;
    }
    uint32_t d_nag  = s->nag_echo_count - s_prev_nag;
    uint32_t d_ap   = s->apctl_tx_count - s_prev_ap;
    uint32_t d_x3f8 = s->x3f8_tx_count  - s_prev_x3f8tx;
    if (d_nag || d_ap || d_x3f8) {
        char line[110];
        int n = snprintf(line, sizeof(line),
                         "[%4lu~%lu] TX 370 echo=%lu 3FD=%lu 3F8=%lu\n",
                         (unsigned long)s_prev_sec, (unsigned long)sec,
                         (unsigned long)d_nag, (unsigned long)d_ap, (unsigned long)d_x3f8);
        if (n > 0) append_buf(line, (size_t)n);
    }
    /* 진단: 버스별(A=MCP, B=TWAI) 관심 프레임 도착 개수. 매 창 출력 후 리셋.
       0 도 찍어야 "안 온다"가 보이므로 전부 출력한다. */
    for (uint8_t b = 1u; ; b--) {   /* 1=A 먼저, 0=B */
        char line[220];
        size_t p = 0;
        int n = snprintf(line, sizeof line, "[%4lu~%lu] RX %s",
                         (unsigned long)s_prev_sec, (unsigned long)sec, b ? "A" : "B");
        if (n > 0) p = (size_t)n;
        for (unsigned i = 0; i < DIAG_N && p < sizeof line; i++) {
            n = snprintf(line + p, sizeof line - p, " %s=%lu",
                         s_diag_names[i], (unsigned long)s_rx_cnt[b][i]);
            if (n > 0) p += (size_t)n;
        }
        if (p < sizeof line) {
            n = snprintf(line + p, sizeof line - p, " oth=%lu\n", (unsigned long)s_rx_other[b]);
            if (n > 0) p += (size_t)n;
        }
        append_buf(line, p);
        for (unsigned i = 0; i < DIAG_N; i++) s_rx_cnt[b][i] = 0;
        s_rx_other[b] = 0;
        if (b == 0u) break;
    }

    s_prev_nag = s->nag_echo_count;
    s_prev_ap  = s->apctl_tx_count;
    s_prev_x3f8tx = s->x3f8_tx_count;
    s_prev_sec = sec;
}

void trace_flush() {
    if (!s_mounted || s_len == 0) return;
    if (!lock()) return;

    File f = LittleFS.open(TRACE_PATH, FILE_APPEND);
    if (!f) { unlock(); return; }

    /* 캡 도달 시 통째로 비우고 헤더 재기록(단순 링). */
    if ((size_t)f.size() + s_len > TRACE_CAP) {
        f.close();
        f = LittleFS.open(TRACE_PATH, FILE_WRITE);   /* truncate */
        if (!f) { unlock(); return; }
        s_hdr_pending = true;
    }
    if (s_hdr_pending) {
        write_header_locked(f);
        s_hdr_pending = false;
    }
    f.write((const uint8_t*)s_buf, s_len);
    f.close();
    s_len = 0;
    unlock();
}

size_t trace_dump(Print& out) {
    if (!s_mounted) return 0;
    if (!lock()) return 0;
    size_t total = 0;
    File f = LittleFS.open(TRACE_PATH, FILE_READ);
    if (f) {
        uint8_t chunk[256];
        int r;
        while ((r = f.read(chunk, sizeof(chunk))) > 0) total += out.write(chunk, (size_t)r);
        f.close();
    }
    unlock();
    return total;
}

void trace_clear() {
    if (!s_mounted) return;
    if (!lock()) return;
    File f = LittleFS.open(TRACE_PATH, FILE_WRITE);   /* truncate to empty */
    if (f) f.close();
    s_hdr_pending = true;    /* 다음 flush 가 세션 헤더를 다시 쓴다 */
    unlock();
}
