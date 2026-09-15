#include "fsd_state.h"
#include "fsd_signals.h"
#include <string.h>

void fsd_state_init(FSDState* s) {
    memset(s, 0, sizeof(*s));
    s->nag_kill             = true;
    s->wave                 = NAG_BASIC;
    s->das_prev_hands_on    = 0xFFu;   /* 센티넬: 첫 프레임을 상승엣지로 보지 않음 */
    s->nag_torq_walk        = NAG_TORQUE_CENTER;
    s->nag_prng             = 0x12345678u;
    s->ota_clear            = OTA_CLEAR_FRAMES;
    s->di_speed_raw         = DI_SPEED_SNA_RAW;   /* 수신 전엔 SNA — 정지로 보지 않음 */
    s->f_prev_das  = 0xFFu;
    s->f_last_raw  = NAG_TORQUE_CENTER;
    s->f_mild_walk = NAG_TORQUE_CENTER;
}
