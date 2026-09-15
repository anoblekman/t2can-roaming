#pragma once

/* AP 전용 — 원본의 STA(집 WiFi) 경로는 이식하지 않는다.
   이유: 부팅 10초 블로킹, 연결 성공 시 AP 종료, 실패 시 재접속 없음 —
   차량에 상시 붙어 있는 보드에는 세 가지 다 해롭다. */
void wifi_ap_start();
