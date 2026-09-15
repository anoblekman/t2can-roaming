#include "fsd_apctl.h"
#include "fsd_gate.h"
#include "fsd_signals.h"

bool fsd_apctl_is_mux1(const CanFrame* f) {
    if (f->id != ID_AP_CONTROL) return false;
    if (f->dlc != 8)            return false;
    return (uint8_t)(f->data[AP_MUX_BYTE] & AP_MUX_MASK) == AP_MUX1;
}

bool fsd_apctl_should_send(const FSDState* s, uint32_t now_ms) {
    if (!fsd_can_transmit(s)) return false;
    if (s->nag_kill      && fsd_ap_ok(s, now_ms))     return true;
    if (s->summon_unlock && fsd_summon_ok(s, now_ms)) return true;
    return false;
}

void fsd_apctl_build(const CanFrame* in, CanFrame* out) {
    *out = *in;
    out->data[AP_BIT19_BYTE] = (uint8_t)(out->data[AP_BIT19_BYTE] & ~AP_BIT19_MASK);
    out->data[AP_BIT47_BYTE] = (uint8_t)(out->data[AP_BIT47_BYTE] |  AP_BIT47_MASK);
}
