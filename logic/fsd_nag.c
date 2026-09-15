#include "fsd_nag.h"
#include "fsd_checksum.h"
#include "fsd_signals.h"

static uint32_t nag_rand(FSDState* s) {
    uint32_t x = s->nag_prng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    s->nag_prng = x;
    return x;
}

static int16_t clamp_torque(int16_t raw) {
    if (raw > NAG_TORQUE_RAW_MAX) return NAG_TORQUE_RAW_MAX;
    if (raw < NAG_TORQUE_RAW_MIN) return NAG_TORQUE_RAW_MIN;
    return raw;
}

static bool in_burst_pause(uint32_t now_ms) {
    uint32_t cycle = NAG_BURST_MS + NAG_PAUSE_MS;
    return (now_ms % cycle) >= NAG_BURST_MS;
}

/* 공통 프레임 조립. hands_byte 는 파형이 정한 byte4 값. */
static void assemble(const CanFrame* in, CanFrame* out, int16_t torque,
                     uint8_t hands_byte) {
    out->id  = ID_EPAS_STATUS;
    out->dlc = 8;
    out->data[0] = in->data[0];
    out->data[1] = in->data[1];
    out->data[EPAS_TORQUE_HI_BYTE] =
        (uint8_t)((in->data[EPAS_TORQUE_HI_BYTE] & EPAS_TORQUE_HI_KEEP) |
                  (uint8_t)((torque >> 8) & EPAS_TORQUE_HI_MASK));
    out->data[EPAS_TORQUE_LO_BYTE] = (uint8_t)(torque & 0xFFu);
    out->data[EPAS_HANDS_BYTE]     = hands_byte;
    out->data[5] = in->data[5];
    uint8_t cnt = (uint8_t)((in->data[EPAS_COUNTER_BYTE] + 1u) & EPAS_COUNTER_MASK);
    out->data[EPAS_COUNTER_BYTE] =
        (uint8_t)((in->data[EPAS_COUNTER_BYTE] & EPAS_COUNTER_KEEP) | cnt);
    out->data[7] = tesla_additive_checksum(ID_EPAS_STATUS, out->data, 7);
}

static bool nag_faithful(FSDState* s, const CanFrame* in, CanFrame* out,
                         uint32_t now_ms) {
    uint8_t das = s->das_hands_on;
    bool is_strong   = (das == 3u || das == 4u || das == 5u);
    bool prev_strong = (s->f_prev_das == 3u || s->f_prev_das == 4u ||
                        s->f_prev_das == 5u);

    if (s->f_prev_das != 1u && das == 1u) s->f_stage1_ms = now_ms;
    if (das != 1u) s->f_stage1_ms = 0u;
    if (s->f_prev_das != 2u && das == 2u) s->f_stage2_ms = now_ms;
    if (das != 2u) { s->f_stage2_ms = 0u; s->f_s2_hold_until_ms = 0u;
                     s->f_s2_level2 = false; }
    if (!prev_strong && is_strong) s->f_strong_ms = now_ms;
    if (!is_strong) s->f_strong_ms = 0u;
    s->f_prev_das = das;

    if (s->das_ap_state < 2u || das == 0u || das == 8u || das == 15u) {
        s->f_last_raw = NAG_TORQUE_CENTER;
        return false;
    }

    int dir = (s->steering_deg > 0.0f) ? -1 : 1;
    int16_t torque;

    if (das == 1u) {
        if (s->f_stage1_ms != 0u && (now_ms - s->f_stage1_ms) < 500u) {
            torque = s->f_last_raw;
        } else {
            s->f_last_raw = NAG_TORQUE_CENTER;
            return false;
        }
    } else if (das == 2u) {
        if (s->f_stage2_ms != 0u && (now_ms - s->f_stage2_ms) < 2000u) return false;
        if (now_ms < s->f_s2_hold_until_ms) {
            torque = s->f_s2_hold_raw;
        } else {
            int16_t lo = (dir < 0) ? 1848 : 2098;
            int16_t hi = (dir < 0) ? 1998 : 2248;
            if (s->f_mild_walk < lo || s->f_mild_walk > hi)
                s->f_mild_walk = (int16_t)((lo + hi) / 2);
            s->f_mild_walk = (int16_t)(s->f_mild_walk +
                                       (int16_t)((int)(nag_rand(s) % 25u) - 12));
            if (s->f_mild_walk < lo) s->f_mild_walk = lo;
            if (s->f_mild_walk > hi) s->f_mild_walk = hi;
            torque = s->f_mild_walk;
            int16_t a = (torque >= NAG_TORQUE_CENTER)
                            ? (int16_t)(torque - NAG_TORQUE_CENTER)
                            : (int16_t)(NAG_TORQUE_CENTER - torque);
            bool l2 = (a >= 200);
            if (l2 && !s->f_s2_level2) {
                s->f_s2_hold_until_ms = now_ms + 1000u;
                s->f_s2_hold_raw      = torque;
            }
            s->f_s2_level2 = l2;
        }
    } else {
        if (s->f_strong_ms != 0u && (now_ms - s->f_strong_ms) < 1000u) return false;
        uint32_t active = (s->f_strong_ms == 0u) ? 0u
                                                 : (now_ms - s->f_strong_ms - 1000u);
        uint16_t phase = (uint16_t)(active % 1500u);
        int16_t mag = 210;
        if (phase < 500u) mag = (int16_t)(((int32_t)phase * 210) / 500);
        torque = (int16_t)(NAG_TORQUE_CENTER + dir * mag);
    }

    s->f_last_raw = torque;
    torque = clamp_torque(torque);
    /* 충실 파형은 handsOnLevel 을 건드리지 않는다 (#122) */
    assemble(in, out, torque, in->data[EPAS_HANDS_BYTE]);
    s->nag_echo_count++;
    return true;
}

bool fsd_nag_build(FSDState* s, const CanFrame* in, CanFrame* out,
                   uint32_t now_ms) {
    if (in->dlc != 8) return false;
    if (s->wave == NAG_FAITHFUL) return nag_faithful(s, in, out, now_ms);
    if (s->wave == NAG_BURST && in_burst_pause(now_ms)) return false;

    uint8_t hands_on = (uint8_t)((in->data[EPAS_HANDS_BYTE] >> EPAS_HANDS_SHIFT) &
                                 EPAS_HANDS_MASK);
    if (hands_on == EPAS_HANDS_OK) return false;

    uint8_t das      = s->das_hands_on;
    uint8_t das_prev = s->das_prev_hands_on;
    s->das_prev_hands_on = das;
    if (s->das_seen &&
        (das == DAS_HANDS_NOT_REQUIRED || das == DAS_HANDS_SUSPENDED))
        return false;

    /* 그립 펄스 재무장: EPAS 요구 상승 또는 DAS 단계 상승 */
    bool das_escalation = s->das_seen && (das_prev == 0xFFu || das > das_prev);
    bool demand_now     = (hands_on == 0u || hands_on == 3u);
    if ((das_escalation || (demand_now && !s->nag_demand_active)) &&
        s->nag_exc_frames == 0u) {
        s->nag_exc_frames       = (uint8_t)(3u + (nag_rand(s) % 3u));
        s->nag_frames_until_exc = (uint16_t)(125u + (nag_rand(s) % 100u));
    }
    s->nag_demand_active = demand_now;

    int16_t torque;
    if (s->nag_exc_frames > 0u) {
        torque = (int16_t)(2350 + (int)(nag_rand(s) % 41u) - 20);
        s->nag_exc_frames--;
    } else {
        int16_t step = (int16_t)((int)(nag_rand(s) % 31u) - 15);
        s->nag_torq_walk = (int16_t)(s->nag_torq_walk + step);
        if (s->nag_torq_walk < 2150) s->nag_torq_walk = 2150;
        if (s->nag_torq_walk > 2290) s->nag_torq_walk = 2290;
        torque = s->nag_torq_walk;
        if (s->nag_frames_until_exc > 0u) {
            s->nag_frames_until_exc--;
        } else {
            s->nag_exc_frames       = (uint8_t)(3u + (nag_rand(s) % 3u));
            s->nag_frames_until_exc = (uint16_t)(125u + (nag_rand(s) % 100u));
        }
    }

    torque = clamp_torque(torque);
    uint8_t hands_byte =
        (uint8_t)((in->data[EPAS_HANDS_BYTE] & (uint8_t)~EPAS_HANDS_CLEAR) |
                  EPAS_HANDS_SPOOF);
    assemble(in, out, torque, hands_byte);
    s->nag_echo_count++;
    return true;
}
