#include "fsd_x3f8.h"
#include "fsd_signals.h"

/* 인덱스 1..7. set=1 이면 비트 세팅(0→1), set=0 이면 클리어(1→0).
   비트 위치 = tesla_can 0x3E8 맵의 전역 비트 → byteN.bit. 우리 차 0x3F8 에 같은
   레이아웃이라는 가정(미검증). 현재값(트레이스 03 28 80 00 19 29 19 24)에서
   1번만 1(확인필요)이라 클리어, 나머지 6종은 0이라 세팅. */
typedef struct { uint8_t byte; uint8_t mask; uint8_t set; } X3f8Bit;

static const X3f8Bit X3F8[X3F8_EXP_COUNT] = {
    { 0, 0x00u, 0 },   /* 0 = off (사용 안 함) */
    { 0, 0x02u, 0 },   /* 1 ulcStalkConfirm   bit1  → clear (차선변경 확인생략) */
    { 4, 0x40u, 1 },   /* 2 undertakeAssist    bit38 → set  (추월보조) */
    { 6, 0x40u, 1 },   /* 3 alcOffHighway      bit54 → set  (고속밖 자동차선변경) */
    { 7, 0x01u, 1 },   /* 4 ulcOffHighway      bit56 → set  (고속밖 ULC) */
    { 2, 0x08u, 1 },   /* 5 nextGenACC         bit19 → set  (차세대 ACC) */
    { 4, 0x80u, 1 },   /* 6 adaptiveSetSpeed   bit39 → set  (적응형 속도설정) */
    { 5, 0x40u, 1 },   /* 7 followNavRoute     bit46 → set  (NoA 경로추종) */
};

bool fsd_x3f8_build(const FSDState* s, const CanFrame* in, CanFrame* out) {
    if (in->id != ID_UI_DRIVERASSIST || in->dlc != 8) return false;
    uint8_t mask = s->x3f8_exp;   /* 비트마스크: bit(i-1) = 신호 i 선택 */
    if (mask == 0u) return false;
    *out = *in;
    bool any = false;
    for (uint8_t i = 1u; i < X3F8_EXP_COUNT; i++) {
        if (mask & (uint8_t)(1u << (i - 1u))) {
            if (X3F8[i].set) out->data[X3F8[i].byte] |= X3F8[i].mask;
            else             out->data[X3F8[i].byte] &= (uint8_t)~X3F8[i].mask;
            any = true;
        }
    }
    return any;   /* 선택된 비트를 한 프레임에 모두 적용 (여러 개 동시 가능) */
}
