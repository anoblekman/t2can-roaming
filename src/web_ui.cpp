/*
 * web_ui.cpp — 웹 대시보드 서버 (core 0 에서 도는 태스크).
 *
 * 동시성 규칙:
 *   - 이 파일은 g_state 를 직접 만지지 않는다. 읽기는 항상 state_snapshot()
 *     (main.cpp, portMUX 로 보호된 복사본)을 통해서만 한다.
 *   - 토글 변경(/api/set)은 g_state 를 직접 쓰지 않는다. g_pending_* 전역에
 *     적어 두고, core 1 의 apply_pending() 이 lock 아래서 흡수해 g_state 에
 *     반영한 뒤 prefs_save() 로 NVS 에 쓴다. NVS 쓰기는 여기(core 0)가 아니라
 *     core 1 에서 일어난다.
 */
#include "web_ui.h"
#include "web_page.h"
#include "config.h"
#include "trace_log.h"
#include "prefs.h"
#include "can_driver.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <esp_ota_ops.h>

extern "C" {
#include "fsd_state.h"
#include "fsd_gate.h"
}

/* main.cpp 가 정의한다. 토글 변경은 pending_commit()(web_ui.h) 하나로만 넘긴다 —
   seed+덮어쓰기를 g_mux 아래 원자적으로 처리하므로 여기서 g_pending_* 를 직접
   만지지 않는다. */
FSDState state_snapshot();

/* main.cpp 가 정의한 웹 OTA 진행 플래그. 여기(core 0)서 세우기만 하면 core 1 의
   리컨실러가 다음 루프에 버스를 Listen-Only 로 내려 준다 — g_state 는 안 건드린다. */
extern volatile bool g_web_ota_active;

static WebServer s_server(80);

/* ── 게이트 사유 -> 한글 짧은 문구 ─────────────────────────────────────── */
static const char* gate_reason_kr(GateReason r) {
    switch (r) {
        case GATE_OK:             return "통과";
        case GATE_TOGGLE_OFF:     return "토글 꺼짐";
        case GATE_LISTEN:         return "리슨 모드";
        case GATE_OTA:            return "OTA 중";
        case GATE_DAS_UNSEEN:     return "DAS 미수신";
        case GATE_DAS_STALE:      return "DAS 오래됨";
        case GATE_AP_NOT_ENGAGED: return "AP 미결합";
        case GATE_AP_UNSTABLE:    return "AP 결합 안정화 중";
        case GATE_AP_ENGAGED:     return "AP 결합 중";
        case GATE_AP_FSD:         return "자동주차(AP 6) 중";
        case GATE_DI_UNSEEN:      return "기어 신호 미수신";
        case GATE_DI_STALE:       return "기어 신호 끊김";
        case GATE_SPEED_UNSEEN:   return "속도 신호 미수신";
        case GATE_SPEED_STALE:    return "속도 신호 끊김";
        case GATE_SPR_UNSEEN:     return "서먼요청 신호 미수신";
        case GATE_SPR_STALE:      return "서먼요청 신호 끊김";
        case GATE_BAD_GEAR:       return "기어 값 이상";
        case GATE_BAD_SPEED:      return "속도 값 이상";
        case GATE_AP_INVALID:     return "AP 중단/고장";
        case GATE_SPR_INVALID:    return "서먼요청 값 이상";
        case GATE_PARKED_MOVING:  return "P단인데 이동 중";
        case GATE_GEAR_CONFLICT:  return "서먼 중 N단";
        case GATE_ACA_UNCONFIRMED:return "서먼 요청 없는 자율제어";
        case GATE_NOT_PARKED:     return "주차 아님";
        default:                  return "알 수 없음";
    }
}

/* "주행 통과 / 서먼 차단(AP 결합 중)" 형태의 사람이 읽는 게이트 상태 문자열.
   fsd_*_why() 는 순수 함수(스냅샷 사본만 읽는다)라 lock 밖에서 불러도 안전
   하다 — g_state 자체를 만지는 게 아니라 state_snapshot() 이 이미 복사해 온
   사본을 읽을 뿐이다. */
static String gate_status_string(const FSDState& s, uint32_t now_ms) {
    GateReason txr = fsd_can_transmit_why(&s);
    GateReason smr = fsd_summon_ok_why(&s, now_ms);

    String tx = "주행 ";
    tx += (txr == GATE_OK) ? "통과" : (String("차단(") + gate_reason_kr(txr) + ")");

    String sm = "서먼 ";
    sm += (smr == GATE_OK) ? "통과" : (String("차단(") + gate_reason_kr(smr) + ")");

    return tx + " / " + sm;
}

static String build_status_json() {
    FSDState s = state_snapshot();
    uint32_t now = millis();
    String gate = gate_status_string(s, now);
    /* DAS 신선도: 마지막 0x39B/0x399 수신 이후 경과(ms). 미수신이면 큰 값. */
    uint32_t das_age = s.das_seen ? (now - s.das_seen_ms) : 999999u;

    char buf[1024];
    int n = snprintf(buf, sizeof(buf),
        "{\"act\":%s,\"nag\":%s,\"wave\":%u,\"smn\":%s,"
        "\"ap\":%u,\"hands\":%u,\"speed\":%u,\"lane\":%u,"
        "\"rxA\":%lu,\"rxB\":%lu,\"errA\":%lu,\"errB\":%lu,"
        "\"nag_tx\":%lu,\"ap_tx\":%lu,"
        "\"ota\":%s,\"ver\":\"%s\",\"gate\":\"%s\","
        "\"das_age\":%lu,"
        "\"x3f8_exp\":%u,\"x3f8_tx\":%lu,\"x3f8_seen\":%s,"
        "\"x3f8\":[%u,%u,%u,%u,%u,%u,%u,%u]}",
        s.active ? "true" : "false",
        s.nag_kill ? "true" : "false",
        (unsigned)s.wave,
        s.summon_unlock ? "true" : "false",
        (unsigned)s.das_ap_state,
        (unsigned)s.das_hands_on,
        (unsigned)s.das_speed_limit,
        (unsigned)s.das_lane_change,
        (unsigned long)s.rx_can1, (unsigned long)s.rx_can0,
        (unsigned long)s.err_can1, (unsigned long)s.err_can0,
        (unsigned long)s.nag_echo_count,
        (unsigned long)s.apctl_tx_count,
        s.ota_in_progress ? "true" : "false",
        FW_VERSION,
        gate.c_str(),
        (unsigned long)das_age,
        (unsigned)s.x3f8_exp,
        (unsigned long)s.x3f8_tx_count,
        s.x3f8_seen ? "true" : "false",
        (unsigned)s.x3f8_raw[0], (unsigned)s.x3f8_raw[1],
        (unsigned)s.x3f8_raw[2], (unsigned)s.x3f8_raw[3],
        (unsigned)s.x3f8_raw[4], (unsigned)s.x3f8_raw[5],
        (unsigned)s.x3f8_raw[6], (unsigned)s.x3f8_raw[7]);

    if (n < 0) return String("{}");
    if ((size_t)n >= sizeof(buf)) n = (int)sizeof(buf) - 1;   /* 잘렸어도 유효한 문자열 */
    return String(buf);
}

/* ── /api/set 요청 바디의 아주 작은 수동 JSON 파서 ─────────────────────────
   우리 자신의 프런트가 보내는 {"key":"...","value":true|false|N} 형태만
   상대한다 — 라이브러리 추가 없이 이 정도면 충분하다. */
static String json_extract_string(const String& body, const char* field) {
    String pat = String("\"") + field + "\"";
    int i = body.indexOf(pat);
    if (i < 0) return "";
    i = body.indexOf(':', i);
    if (i < 0) return "";
    i = body.indexOf('"', i);
    if (i < 0) return "";
    int j = body.indexOf('"', i + 1);
    if (j < 0) return "";
    return body.substring(i + 1, j);
}

static long json_extract_value(const String& body, bool* found) {
    *found = false;
    int i = body.indexOf("\"value\"");
    if (i < 0) return 0;
    i = body.indexOf(':', i);
    if (i < 0) return 0;
    i++;
    while (i < (int)body.length() && body[i] == ' ') i++;
    *found = true;
    if (body.startsWith("true", i))  return 1;
    if (body.startsWith("false", i)) return 0;
    return body.substring(i).toInt();
}

static void handle_root() {
    s_server.send_P(200, "text/html; charset=utf-8", WEB_PAGE);
}

static void handle_status() {
    s_server.send(200, "application/json; charset=utf-8", build_status_json());
}

static void handle_set() {
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }
    String body = s_server.arg("plain");
    String key  = json_extract_string(body, "key");
    bool   found = false;
    long   raw   = json_extract_value(body, &found);
    bool   bv    = (raw != 0);

    if (key.length() == 0 || !found) {
        s_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    /* 키 -> 필드 매핑만 여기서 하고, 실제 seed+덮어쓰기는 pending_commit() 이
       g_mux 아래서 원자적으로 처리한다. seed 로직(아직 안 건드린 다른 토글이
       0 으로 리셋되지 않게 현재 g_state 로 먼저 채우는 것)은 main.cpp 로 옮겼다 —
       그래야 core 1 의 apply_pending() 과 같은 락을 공유해 레이스가 없다. */
    PendingField field;
    uint8_t      wave = 0;
    if      (key == "act")  field = PF_ACT;
    else if (key == "nag")  field = PF_NAG;
    else if (key == "smn")  field = PF_SMN;
    else if (key == "wave") {
        field = PF_WAVE;
        long w = raw;
        if (w < 0) w = 0;
        if (w > 2) w = 2;
        wave = (uint8_t)w;
    } else if (key == "x3f8") {
        field = PF_X3F8;                 /* value = 비트마스크 0(off)~127 (신호 7종) */
        long w = raw;
        if (w < 0) w = 0;
        if (w > 127) w = 127;
        wave = (uint8_t)w;
    } else {
        s_server.send(400, "application/json", "{\"ok\":false}");
        return;
    }

    pending_commit(field, bv, wave);
    s_server.send(200, "application/json", "{\"ok\":true}");
}

/* ── 네트워크/보안 설정 (§8.1) ──────────────────────────────────────────────
   AP SSID/비번·관리자 비번을 NVS 에 저장한다(prefs_set_net). SSID/AP비번 변경은
   SoftAP 재시작이 필요해 저장 후 재부팅으로 적용한다. 관리자 비번은 웹 OTA 의
   HTTP Basic 인증에 쓰인다(handle_ota_*).

   보안: GET 은 SSID 만 돌려준다 — 비밀번호는 절대 응답에 넣지 않는다. */
static String json_escape(const String& in) {
    String out;
    out.reserve(in.length() + 4);
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else if (c == '\r') out += "\\r";
        else if (c == '\t') out += "\\t";
        else out += c;
    }
    return out;
}

static void handle_net_get() {
    String out = "{\"ssid\":\"";
    out += json_escape(prefs_ap_ssid());
    out += "\"}";
    s_server.send(200, "application/json; charset=utf-8", out);
}

static void handle_net_set() {
    /* 설정 변경은 AP 접속(WPA2)만으로 가능하다 — 별도 HTTP 인증 없음. AP 비밀번호가
       유일한 신뢰경계다(README §2). */
    if (!s_server.hasArg("plain")) {
        s_server.send(400, "application/json", "{\"ok\":false,\"err\":\"body\"}");
        return;
    }
    String body  = s_server.arg("plain");
    String ssid  = json_extract_string(body, "ssid");
    String ap_pw = json_extract_string(body, "ap_pw");

    /* 검증. WPA2 SoftAP 는 8..63 자 비번을 요구한다(빈 비번=개방 AP 는 불허). */
    if (ssid.length() < 1 || ssid.length() > 31) {
        s_server.send(400, "application/json", "{\"ok\":false,\"err\":\"ssid\"}");
        return;
    }
    if (ap_pw.length() < 8 || ap_pw.length() > 63) {
        s_server.send(400, "application/json", "{\"ok\":false,\"err\":\"appw_len\"}");
        return;
    }

    prefs_set_net(ssid, ap_pw);
    /* 응답 먼저 보내 클라가 받게 한 뒤 재부팅 — SoftAP 가 새 설정으로 다시 뜬다. */
    s_server.send(200, "application/json; charset=utf-8", "{\"ok\":true,\"reboot\":true}");
    Serial.println("[NET] 설정 저장 — 곧 재부팅");
    delay(300);
    ESP.restart();
}

static void handle_not_found() {
    s_server.send(404, "text/plain", "not found");
}

/* ── 큰 본문을 청크로 흘려보내는 Print 어댑터 ─────────────────────────────
   트레이스는 최대 수 MB라 String 으로 한 번에 만들면 힙이 터진다. trace_dump
   가 이 Print 로 쓰면, 1KB 씩 모아 WebServer 의 청크 인코딩으로 내보낸다. */
class WebChunkPrint : public Print {
    WebServer& srv;
    uint8_t buf[1024];
    size_t  n = 0;
public:
    explicit WebChunkPrint(WebServer& s) : srv(s) {}
    void flushChunk() { if (n) { srv.sendContent((const char*)buf, n); n = 0; } }
    size_t write(uint8_t c) override {
        buf[n++] = c;
        if (n >= sizeof(buf)) flushChunk();
        return 1;
    }
    size_t write(const uint8_t* b, size_t len) override {
        for (size_t i = 0; i < len; i++) {
            buf[n++] = b[i];
            if (n >= sizeof(buf)) flushChunk();
        }
        return len;
    }
};

/* ── §4.8 게이트 트레이스 엔드포인트 ─────────────────────────────────────── */
static void handle_trace_get() {
    s_server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server.sendHeader("Content-Disposition", "attachment; filename=\"trace.log\"");
    s_server.send(200, "text/plain", "");
    WebChunkPrint cp(s_server);
    trace_dump(cp);
    cp.flushChunk();
    s_server.sendContent("");
}

static void handle_trace_clear() {
    trace_clear();
    s_server.send(200, "application/json", "{\"ok\":true}");
}

/* ── 웹 OTA (앱 파티션 플래시) ─────────────────────────────────────────────
   검증된 원본(flipper-tesla-fsd .../web_dashboard.cpp handle_ota_upload/
   handle_ota_done)의 안전 체크를 이식했다: .bin 확장자, ESP32 이미지 매직
   0xE9(첫 청크), esp_ota_get_next_update_partition 로 대상 파티션·크기 확보,
   파티션 크기 초과 방지, Update.begin/write/end(true).

   신뢰경계: AP 비밀번호(NVS appw)가 네트워크 접근을 막는 유일한 경계다 — AP 에
   붙으면(WPA2) 별도 HTTP 인증 없이 웹 OTA 가 가능하다(README §2 참조).

   동시성: START 에서 g_web_ota_active=true 만 세운다. g_state 는 절대 직접
   안 쓴다(core 0). core 1 리컨실러가 다음 루프에 버스를 Listen-Only 로 내려
   플래시 쓰는 동안 물리적으로 송신을 멈춘다. */
static bool        s_ota_err = false;
static const char* s_ota_msg = nullptr;
static bool        s_ota_magic_checked = false;
static size_t      s_ota_written = 0;
static size_t      s_ota_max = 0;

static void handle_ota_upload() {
    HTTPUpload& up = s_server.upload();

    if (up.status == UPLOAD_FILE_START) {
        s_ota_err = false;
        s_ota_msg = nullptr;
        s_ota_magic_checked = false;
        s_ota_written = 0;
        s_ota_max = 0;
        Serial.printf("[OTA] 시작: %s\n", up.filename.c_str());

        if (!up.filename.endsWith(".bin")) {
            s_ota_err = true; s_ota_msg = ".bin 파일이 아님";
            return;
        }

        /* 활성 모드에선 OTA 거부 — 주입 중 재부팅이 DAS fault 를 유발(실차 확인).
           리슨으로 바꾼 뒤 업데이트하도록 안내한다. active 읽기는 스냅샷 사본. */
        if (state_snapshot().active) {
            s_ota_err = true; s_ota_msg = "활성 모드에선 OTA 불가 — 리슨 모드로 바꾼 뒤 다시 시도하세요";
            return;
        }

        /* 플래시 쓰기 전에 CAN 송신 정지 유도 (core 1 리컨실러가 처리) */
        g_web_ota_active = true;

        const esp_partition_t* part = esp_ota_get_next_update_partition(NULL);
        if (part == NULL) {
            s_ota_err = true; s_ota_msg = "OTA 파티션 없음";
            return;
        }
        s_ota_max = part->size;
        Serial.printf("[OTA] 대상 파티션 %s, 크기 %u\n", part->label, (unsigned)s_ota_max);

        if (!Update.begin(part->size, U_FLASH)) {
            Update.printError(Serial);
            s_ota_err = true; s_ota_msg = "Update.begin() 실패";
            return;
        }
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (s_ota_err) return;

        /* 첫 청크: ESP32 이미지 매직 바이트 0xE9 검증 */
        if (!s_ota_magic_checked) {
            if (up.currentSize == 0 || up.buf[0] != 0xE9u) {
                s_ota_err = true; s_ota_msg = "ESP32 이미지 매직(0xE9) 아님";
                Update.abort();
                return;
            }
            s_ota_magic_checked = true;
        }

        /* 파티션 크기 초과 방지 */
        if (s_ota_max > 0 && (s_ota_written + up.currentSize) > s_ota_max) {
            s_ota_err = true; s_ota_msg = "펌웨어가 파티션 크기 초과";
            Update.abort();
            return;
        }

        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.printError(Serial);
            s_ota_err = true; s_ota_msg = "플래시 쓰기 실패";
            return;
        }
        s_ota_written += up.currentSize;
    } else if (up.status == UPLOAD_FILE_END) {
        if (s_ota_err) { Update.abort(); return; }
        if (Update.end(true)) {
            Serial.printf("[OTA] 완료: %u 바이트\n", (unsigned)s_ota_written);
        } else {
            Update.printError(Serial);
            s_ota_err = true; s_ota_msg = "Update.end() 실패";
        }
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
        s_ota_err = true; s_ota_msg = "업로드 중단됨";
        /* 중요: 클라 연결이 업로드 도중 끊기면 프레임워크가 이 콜백을
           UPLOAD_FILE_ABORTED 로 부른 뒤 요청을 실패 처리해 handle_ota_done()
           을 아예 호출하지 않는다. 그래서 여기서 g_web_ota_active 를 직접 풀어야
           한다 — 안 그러면 플래그가 true 로 박혀 리컨실러가 버스를 영구 리슨으로
           고정하고, 재부팅 전까지 모든 주입이 조용히 죽는다. BOOT 짧게로도 복구
           안 됨(토글은 g_state.active 만 바꾸고 이 플래그는 그대로라서). */
        g_web_ota_active = false;
    }
}

static void handle_ota_done() {
    if (s_ota_err || Update.hasError()) {
        String msg = "{\"ok\":false,\"err\":\"";
        msg += s_ota_msg ? s_ota_msg : "업로드 오류";
        msg += "\"}";
        s_server.send(500, "application/json; charset=utf-8", msg);
        Update.abort();
        g_web_ota_active = false;   /* 재시도 가능하게 풀어 준다 → 리컨실러가 원래 모드 복귀 */
        return;
    }
    /* 성공: 응답을 먼저 보내 클라가 받게 한 뒤 재부팅 */
    s_server.send(200, "application/json; charset=utf-8", "{\"ok\":true}");
    Serial.println("[OTA] 성공 — CAN 안전 정리 후 재부팅");
    delay(500);
    can_shutdown_safe();   /* 재부팅 글리치가 Chassis 버스(DAS)를 교란하지 않게 */
    ESP.restart();
}

void web_begin() {
    s_server.on("/", HTTP_GET, handle_root);
    s_server.on("/api/status", HTTP_GET, handle_status);
    s_server.on("/api/set", HTTP_POST, handle_set);
    s_server.on("/api/net", HTTP_GET,  handle_net_get);
    s_server.on("/api/net", HTTP_POST, handle_net_set);
    s_server.on("/trace/get",  HTTP_GET, handle_trace_get);
    s_server.on("/trace/clear",HTTP_GET, handle_trace_clear);
    /* OTA: 업로드 콜백(handle_ota_upload)이 청크를 받고, 완료 핸들러
       (handle_ota_done)가 응답/재부팅을 처리한다 — WebServer 3인자 on() 규약. */
    s_server.on("/update", HTTP_POST, handle_ota_done, handle_ota_upload);
    s_server.onNotFound(handle_not_found);
    s_server.begin();
    Serial.println("[WEB] 서버 시작 (포트 80)");
}

void web_task(void* /*arg*/) {
    for (;;) {
        s_server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}
