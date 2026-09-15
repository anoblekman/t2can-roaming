#pragma once

/* 빌드 스크립트(version_gen.py)가 "YYYYMMDD.그날의빌드횟수" 를 -D 로 주입한다.
   아래 폴백은 PlatformIO 를 거치지 않는 빌드(호스트 테스트 등)용. */
#ifndef FW_VERSION
#define FW_VERSION "dev"
#endif

/* T-2CAN 핀맵 — 업스트림 flipper-tesla-fsd 의 esp32 config.h
   BOARD_LILYGO_T2CAN 블록과 동일해야 한다 (대조 확인함). */
#define MCP_CS_PIN     10
#define MCP_SCK_PIN    12
#define MCP_MOSI_PIN   11
#define MCP_MISO_PIN   13
#define MCP_RST_PIN     9    /* 원본 PIN_MCP_RST */
#define MCP_INT_PIN     8    /* 원본 PIN_MCP_INT — RST 와 헷갈리지 말 것 */
#define TWAI_TX_PIN     7
#define TWAI_RX_PIN     6
#define BOOT_BTN_PIN    0    /* BOOT 버튼, active-LOW, 내부 풀업 */

#define CAN_BITRATE_KBPS 500
#define MCP_CRYSTAL_MHZ  16   /* T-2CAN 온보드 MCP2515 = MCP_16MHZ */
