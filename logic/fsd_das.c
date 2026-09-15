#include "fsd_das.h"
#include "fsd_signals.h"

static uint8_t bits(const CanFrame* f, int byte, uint8_t shift, uint8_t mask) {
    return (uint8_t)((f->data[byte] >> shift) & mask);
}

static bool flag(const CanFrame* f, int byte, uint8_t mask) {
    return (f->data[byte] & mask) != 0u;
}

bool fsd_das_handle(FSDState* s, const CanFrame* f, uint32_t now_ms) {
    if (f->dlc != 8) return false;
    if (f->id != ID_DAS_STATUS_B && f->id != ID_DAS_STATUS_A) return false;
    if (f->id == ID_DAS_STATUS_B) s->das_39b_seen = true;
    else if (s->das_39b_seen)     return false;

    uint8_t prev_state = s->das_ap_state;

    s->das_ap_state    = bits(f, DAS_AP_STATE_BYTE, 0, DAS_AP_STATE_MASK);
    /* flipper HW4 기본 판독 위치(byte1 상위 4비트). 이 차는 정본이 byte0[3:0]
       이지만, flipper 가 왜 자동주차에서 무사했는지 검증하려고 관측만 해 둔다. */
    s->das_ap_state_b1 = (uint8_t)((f->data[1] >> 4) & 0x0Fu);
    s->das_speed_limit = bits(f, DAS_SPEED_LIMIT_BYTE, 0, DAS_SPEED_LIMIT_MASK);
    s->das_hands_on    = bits(f, DAS_HANDS_ON_BYTE, DAS_HANDS_ON_SHIFT,
                              DAS_HANDS_ON_MASK);
    /* DAS_autoLaneChangeState : 46|5 -> byte5 bit6..7 + byte6 bit0..2 */
    s->das_lane_change = (uint8_t)(((f->data[5] >> 6) & 0x03u) |
                                   ((f->data[6] & 0x07u) << 2));

    s->summon_obstacle     = flag(f, DAS_SUMMON_OBSTACLE_BYTE, DAS_SUMMON_OBSTACLE_MASK);
    s->summon_cleared_gate = flag(f, DAS_SUMMON_GATE_BYTE,     DAS_SUMMON_GATE_MASK);
    s->autopark_ready      = flag(f, DAS_AUTOPARK_READY_BYTE,  DAS_AUTOPARK_READY_MASK);
    s->auto_parked         = flag(f, DAS_AUTO_PARKED_BYTE,     DAS_AUTO_PARKED_MASK);
    s->summon_fwd_leash    = flag(f, DAS_FWD_LEASH_BYTE,       DAS_FWD_LEASH_MASK);
    s->summon_rvs_leash    = flag(f, DAS_RVS_LEASH_BYTE,       DAS_RVS_LEASH_MASK);
    s->summon_available    = flag(f, DAS_SUMMON_AVAIL_BYTE,    DAS_SUMMON_AVAIL_MASK);

    /* 잔소리 결합 = 3~5. 6(자동주차)·8/9(중단)·14/15 는 결합이 아니므로 타이머 리셋 —
       거기서 3 으로 돌아와도 1초 안정 대기를 다시 거친다. */
    bool engaged      = s->das_ap_state >= DAS_APSTATE_ENGAGED && s->das_ap_state <= DAS_APSTATE_NAG_MAX;
    bool prev_engaged = prev_state      >= DAS_APSTATE_ENGAGED && prev_state      <= DAS_APSTATE_NAG_MAX;
    if (engaged) {
        if (!prev_engaged) s->ap_engaged_since_ms = (now_ms == 0u) ? 1u : now_ms;
    } else {
        s->ap_engaged_since_ms = 0u;
    }

    s->das_seen    = true;
    s->das_seen_ms = now_ms;
    return true;
}

/* ev-open summonInjectionRequestConfirmsSession */
static bool spr_confirms(uint8_t r) {
    switch (r) {
        case 1: case 2: case 4: case 5: case 6: case 7: case 8:
        case 10: case 11: case 12:
            return true;
        default:
            return false;
    }
}

bool fsd_aux_handle(FSDState* s, const CanFrame* f, uint32_t now_ms) {
    if (f->id == ID_UI_DRIVERASSIST && f->dlc >= 4) {
        uint8_t r = (uint8_t)((f->data[UI_SELFPARK_BYTE] >> UI_SELFPARK_SHIFT) & UI_SELFPARK_MASK);
        s->self_park_req = r;
        if (f->dlc == 8) {
            s->spr_frame_seen = true;
            s->spr_frame_ms   = now_ms;
            for (int i = 0; i < 8; i++) s->x3f8_raw[i] = f->data[i];   /* 리드백 */
            s->x3f8_seen = true;
            if (spr_confirms(r)) {
                s->spr_confirmed    = true;
                s->spr_confirmed_ms = now_ms;
            } else if (r == 3u || r == 9u) {
                s->spr_confirmed = false;   /* 취소 */
            }
        }
        s->summoning = s->di_aca && s->spr_confirmed;
        return true;
    }
    if (f->id == ID_DI_SYSTEMSTATUS && f->dlc >= 7) {
        bool aca = (f->data[DI_ACA_BYTE] & DI_ACA_MASK) != 0u;
        /* ACA 상승인데 확정이 없거나 오래됨 → 서먼 아닌 자율제어(AP/TACC/자동주차) */
        if (!s->di_aca && aca &&
            (!s->spr_confirmed || (uint32_t)(now_ms - s->spr_confirmed_ms) > SUMMON_REQ_LEAD_MS))
            s->spr_confirmed = false;
        if (s->di_aca && !aca) s->spr_confirmed = false;   /* ACA 하강엣지 → 에피소드 종료 */
        s->di_aca = aca;
        if (f->dlc == 8) {
            s->di_seen    = true;
            s->di_seen_ms = now_ms;
            s->di_gear    = (uint8_t)((f->data[DI_GEAR_BYTE] >> DI_GEAR_SHIFT) & DI_GEAR_MASK);
        }
        s->summoning = s->di_aca && s->spr_confirmed;
        return true;
    }
    if (f->id == ID_DI_SPEED && f->dlc == 8) {
        s->di_speed_raw  = (uint16_t)((f->data[1] >> 4) | ((uint16_t)f->data[2] << 4));
        s->speed_seen    = true;
        s->speed_seen_ms = now_ms;
        return true;
    }
    return false;
}

bool fsd_ota_handle(FSDState* s, const CanFrame* f) {
    if (f->id != ID_GTW_CAR_STATE) return false;
    if (f->dlc <= GTW_OTA_BYTE)    return false;

    uint8_t raw = (uint8_t)(f->data[GTW_OTA_BYTE] & GTW_OTA_MASK);
    if (raw == GTW_OTA_VALUE) {
        s->ota_clear = 0u;
        if (s->ota_assert < OTA_ASSERT_FRAMES) s->ota_assert++;
        if (s->ota_assert >= OTA_ASSERT_FRAMES) s->ota_in_progress = true;
    } else {
        s->ota_assert = 0u;
        if (s->ota_clear < OTA_CLEAR_FRAMES) s->ota_clear++;
        if (s->ota_clear >= OTA_CLEAR_FRAMES) s->ota_in_progress = false;
    }
    return true;
}
