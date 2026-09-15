[English](README.en.md) | **한국어**

# T2CAN-ROAMING

[![Release](https://img.shields.io/github/v/release/anoblekman/t2can-roaming?sort=semver)](https://github.com/anoblekman/t2can-roaming/releases/latest)
[![License: GPL-3.0](https://img.shields.io/github/license/anoblekman/t2can-roaming)](LICENSE)
[![Board: LilyGo T-2CAN](https://img.shields.io/badge/board-LilyGo%20T--2CAN%20(ESP32--S3)-blue)](https://www.lilygo.cc/)
[![Build: PlatformIO](https://img.shields.io/badge/build-PlatformIO-orange)](https://platformio.org/)

**LilyGo T-2CAN** 보드(ESP32-S3, 듀얼 CAN)용 펌웨어로, 개인 소유의
**Tesla Model 3 Highland (HW4)** 차량에 연결하여 차주 본인 차량의 일부 운전자
보조 기능과 지역별 동작을 조정합니다. 이 프로젝트는
[`hypery11/flipper-tesla-fsd`](https://github.com/hypery11/flipper-tesla-fsd)를
T-2CAN 전용으로 다시 작성한 것입니다.

개인용 / 연구용 차량 개조 프로젝트입니다. 제품이 아니며, Tesla와 아무런 관련이
없고, **어떠한 종류의 보증도 제공하지 않습니다**.

---

## ⚠️ 안전 및 면책 고지 — 먼저 읽으세요

**연구, 교육, 개인 용도로만 사용하세요.** 본인이 소유한 차량에서만, 그리고
합법적인 경우에만 사용하세요. 관할 지역에서 요구하는 경우 공공 도로가 아닌
곳에서 사용하세요. **어떠한 보증도 없습니다. 사용에 따른 모든 위험은 본인
책임입니다.** 차량의 안전하고 합법적인 운행에 대한 책임은 전적으로 사용자에게
있습니다.

- **이 펌웨어는 운전자 주의("핸즈온") 모니터링 기능을 무력화할 수 있습니다.**
  해당 잔소리 알림을 끈다고 해서 차가 자율주행이 되는 것은 **아닙니다**.
  운전자는 여전히 주의를 유지하고, 스티어링 휠에 손을 얹고, 항상 차량을 통제할
  전적인 책임이 있습니다. 차가 스스로 운전하리라 믿고 의지하지 마세요.
- 관할 지역의 법률 및 규제 준수에 대한 책임은 사용자에게 있습니다. 운전자 보조
  동작, "핸즈온" 요구사항, 지역 잠금은 시장과 법률에 따라 다릅니다.
- 차량 버스 트래픽을 수정하면 안전 시스템에 의도치 않은 영향을 줄 수 있습니다.
  안전한 장소에서 보수적으로 테스트하고, 즉시 안전한 상태로 되돌릴 방법을
  확보하세요.

### ⛔ 테스트 중 발견된 강력한 안전 규칙 — 절대 무시하지 마세요

**CAN 프레임 `0x370`을 CAN B(Chassis) 수신 필터에 절대 추가하지 마세요.**

실제 주행 중에 `0x370`을 **Chassis** 버스로 에코했을 때, 차량이 오토파일럿을
**비정상 상태**로 래치하고 **회생 제동이 꺼지는**(계기판 경고) 현상과 동시에
발생하는 것이 관측되었습니다. Chassis 버스 하드웨어 필터는 **`0x39B` +
`0x286`만** 허용해야 하며, 그 외에는 아무것도, 그리고 절대로 `0x370`을 허용해서는
안 됩니다. 이를 방지하는 명시적인 경고 주석이
[`src/can_driver.cpp`](src/can_driver.cpp)에 있습니다(`0x370`으로 검색). 이 주석을
제거하지 마세요.

`0x370`은 **Party** 버스(CAN A)로만 에코되며, 이것이 잔소리 제거 기능을 위한
올바르고 검증된 경로입니다.

### 기본 수신(Default-listen) 안전 모델

두 CAN 컨트롤러 모두 **하드웨어 Listen-Only 모드로 부팅**되어 물리적으로 송신할
수 없습니다. 장치를 명시적으로 **Active**로 전환하기 전까지는 아무것도 주입되지
않습니다. Active 상태일 때도 주입은 여전히 게이트로 제어됩니다. 주입 게이트는
오토파일럿이 engaged 상태(`DAS_autopilotState` 3–5)이고 ≥ 1초 동안 안정적으로
유지된 경우에만 열립니다. **state 6(차내 Autopark)은 차단되므로**, 펌웨어는 차내
주차 조작 중에는 주입하지 않습니다.

---

## 빠른 시작

1. 펌웨어를 플래시합니다 — [Releases](https://github.com/anoblekman/t2can-roaming/releases/latest)의 `.bin` 또는 소스 빌드(아래 "빌드 및 플래시" 참고).
2. Wi-Fi **`T2CAN-ROAMING`** 에 접속합니다 (기본 비밀번호 **`passwd`**).
3. 브라우저에서 **`http://192.168.4.1`** 을 엽니다.
4. **접속 후 바로 비밀번호를 바꾸세요.** 대시보드의 **Tools → Network settings**에서 SSID·비밀번호를 변경합니다(WPA2, 8–63자). AP 비밀번호가 이 장치의 유일한 접근 보호입니다.
5. 부팅 시 기본은 **Listen**(무음, 송신 안 함)입니다. 주입하려면 **Active**로 전환하세요.

---

## 기능

| 기능 | 프레임 | 동작 내용 |
| --- | --- | --- |
| **스티어링 잔소리 제거** | `0x370` 에코 (EPAS3S_sysStatus) | 토크 / `handsOnLevel` 필드를 에코하여 오토스티어의 "핸들을 잡으세요" 잔소리를 억제합니다. **Party 버스(CAN A)에서만** 에코됩니다. |
| **Summon EU 거리 잠금 해제** | `0x3FD` mux1 (bit19 / bit47) | 스마트 서먼의 EU ~6 m 거리 제한을 해제하여 더 멀리서 차를 호출할 수 있게 합니다. |
| **0x3F8 실험 하네스** | `0x3F8` (UI_driverAssistControl) | 개별 `0x3F8` 비트를 탐색하기 위한, 게이트로 제어되는 옵트인 실험 하네스입니다. 기본값은 꺼짐이며, 각 실험은 비트별 토글입니다. |

여기에 더해 다음과 같은 보조 인프라가 있습니다.

- **게이트 결정 트레이스 로깅**(항상 켜짐) — 주입 게이트가 왜 열렸는지 또는 왜
  닫힌 채로 있었는지를 기록하며, 재부팅 후에도 유지되도록 LittleFS에 저장됩니다.
- **Wi-Fi AP 웹 대시보드** — 상태, 모드 제어, 네트워크 설정, 진단을 위한
  자체 호스팅 SoftAP + 웹 UI.
- **OTA 업데이트** — 웹 대시보드(Wi-Fi)를 통하거나 USB 시리얼을 통해 진행합니다.

---

## 하드웨어 및 배선

- **보드:** LilyGo **T-2CAN** (ESP32-S3, 듀얼 CAN: SPI로 연결된 **MCP2515**와
  ESP32-S3 내장 **TWAI** 컨트롤러).
- **차량 커넥터:** Tesla 20핀 **X179**.
- **버스 매핑(양쪽 모두 500 kbps):**

| 컨트롤러 | 버스 | X179 핀 |
| --- | --- | --- |
| **CAN A — MCP2515** | **Party** 버스 | 핀 **2 / 3** |
| **CAN B — TWAI** | **Chassis** 버스 | 핀 **13 / 14** |

Chassis 버스(CAN B) 수신 필터는 의도적으로 `0x39B` + `0x286`만으로 제한됩니다.
안전 섹션과 `src/can_driver.cpp`를 참고하세요.

ESP32-S3 핀 맵(MCP2515 SPI 핀, TWAI TX/RX, BOOT 버튼)은
[`src/config.h`](src/config.h)에 있습니다.

---

## 빌드 및 플래시

### 미리 빌드된 바이너리 받기

빌드 없이 최신 펌웨어를 **[Releases 페이지](https://github.com/anoblekman/t2can-roaming/releases/latest)** 에서 받으세요:

| 파일 | 용도 |
|------|-----|
| `t2can-roaming-<version>-ota.bin` | 웹 대시보드 OTA (앱 파티션) |
| `t2can-roaming-<version>-full.bin` | 시리얼 전체 플래시: `esptool.py --chip esp32s3 write_flash 0x0 <파일>` |

또는 아래에서 소스로 직접 빌드하세요.

**PlatformIO**로 빌드합니다.

```
pio run -e lilygo-t2can
```

`version_gen.py` 프리빌드 스크립트가 펌웨어 버전을 `YYYYMMDD.N`(빌드 날짜 + 그날의
빌드 횟수)으로 새겨 넣으므로, 트레이스 헤더만으로 그 트레이스가 나온 빌드를 고유하게
식별할 수 있습니다.

빌드는 **두 개**의 이미지를 생성합니다. 혼동하지 마세요.

| 파일 | 내용 | 용도 |
| --- | --- | --- |
| `firmware.bin` | **애플리케이션 이미지만** | 웹 OTA 업로드 / app 파티션에 기록 |
| `firmware-merged.bin` | 부트로더 + 파티션 테이블 + boot_app0 + app (**오프셋 0x0**의 전체 플래시 이미지) | ESP Web Tools 또는 `esptool`을 통한 최초 / 전체 복구 플래시 |

### (a) USB 케이블 (권장, 복구 경로)

```
pio run -e lilygo-t2can -t upload
```

### (b) 웹 OTA (케이블 없음)

1. 보드의 AP에 접속합니다(아래 참고).
2. 대시보드 → **Tools → Firmware update → choose file**.
3. **`firmware.bin`**을 선택합니다(`firmware-merged.bin`이 아님 — 전체 플래시
   이미지를 app 파티션에 기록하면 실패합니다).
4. 100% 완료 후 보드가 자동으로 재부팅됩니다. 약 30초 후 새로고침하세요.

OTA 업로드가 진행되는 동안에는 **CAN 송신이 자동으로 중단됩니다**(플래시가 기록되는
동안 버스가 물리적으로 Listen-Only로 내려감), 실패/취소 시 복원됩니다. OTA는 기록
전에 `.bin`을 검증합니다(ESP32 이미지 매직 바이트 `0xE9`, 대상 파티션 크기).
잘못된 이미지는 거부되고 현재 펌웨어가 유지됩니다. USB 재플래시는 항상 복구 경로로
남아 있습니다.

---

## 사용법

### AP 접속

- **SSID:** `T2CAN-ROAMING` (기본값)
- **비밀번호:** `passwd` (기본값, NVS에 저장됨)
- **대시보드:** `http://192.168.4.1`

> **기본값을 변경하세요.** AP 비밀번호(WPA2)가 *유일한* 신뢰 경계입니다. 일단
> 클라이언트가 AP에 접속하면 대시보드, OTA, 설정에 추가 HTTP 인증 없이 접근할 수
> 있습니다(이 장치는 사설 차고용 장치이며, 접근은 오직 Wi-Fi 비밀번호로만
> 제어됩니다). **Tools → Network settings**에서 **SSID와 비밀번호를 변경하세요**.
> AP 비밀번호는 8–63자여야 합니다(WPA2). 부팅 후 20초 이내에 BOOT 버튼을 5초간
> 길게 누르면 NVS가 기본값으로 공장 초기화됩니다.

### Listen → Active

| 모드 | 동작 | 안전 |
| --- | --- | --- |
| **Listen** (부팅 시 기본값) | 수신만 함. 버스에 아무것도 올리지 않음. | 버스가 물리적으로 무음 |
| **Active** | 게이트를 통과한 프레임을 송신함(주입 / 에코). | 주입 발생 |

대시보드의 **Listen / Active** 컨트롤에서, 또는 **BOOT 버튼을 짧게 눌러**(GPIO0)
Active로 전환합니다. 짧게 누르면 모드가 토글되고 NVS에 저장됩니다. 20초 / 5초
BOOT 길게 누르기는 공장 초기화를 수행합니다.

### 진단

- **게이트 트레이스**(항상 켜짐): **Tools → Gate log**(`/trace/get`,
  `/trace/clear`)에서 게이트 결정 로그를 다운로드하거나 지웁니다. 재부팅 후에도
  유지됩니다.
- **프레임 덤프**(수동): **Tools → CAN dump start/stop**을 통해 원시 CAN 프레임을
  PSRAM 링 버퍼에 캡처한 뒤 다운로드합니다. 전원이 꺼지면 사라집니다.

---

## 알려진 한계

- **잔소리 제거는 2026년형 Model 3 Highland에서 작동하지 않습니다.**
  **2024년형** Model 3 Highland에서는 작동이 확인되었지만, **2026년형** Model 3
  Highland에서는 — *동일한* 소프트웨어(`2026.20.6.1`)에서 테스트했음에도 —
  `0x370` 스푸핑을 받아들이지 **않습니다**. 근본 원인은 소프트웨어가 아니라
  **모델 연식 / 하드웨어 세대 차이**입니다. 이것은 2026년형 빌드에 대해 더 넓은
  커뮤니티가 공유하는 미해결 문제이며, 아직 이들에 대해 작동하는 `0x370` 방식은
  알려져 있지 않습니다.
- **참조 DBC는 연식별로 구분되지 않습니다.** `opendbc`의
  `tesla_model3_party.dbc`가 가장 좋은 공개 참조이지만, 공식적으로는 **MY2025**까지
  다룹니다. 실제 차량의 프레임 ID와 비트 레이아웃이 절대 기준이며, DBC 값은 절대적인
  진리가 아니라 출발점으로 취급하세요.
- 일부 동작(예: 특정 Summon 신호 필드와 `0x3F8` 실험)은 여전히 차량에서 검증
  중입니다. `0x3F8` 하네스는 설계상 실험적입니다.

---

## 저장소 구조

- `logic/` — 이식 가능하고 호스트에서 테스트 가능한 코어(프레임 파싱, 게이트/상태
  머신, 체크섬, 잔소리 / Summon / `0x3F8` 빌더). 순수 C이며 하드웨어 의존성 없음.
- `src/` — ESP32-S3 펌웨어: CAN 드라이버, Wi-Fi AP, 웹 UI, OTA, 트레이스 로그,
  설정(prefs).
- `test/` — `logic/` 코어를 위한 호스트 유닛 테스트(`test/run-tests.sh`).
- `platformio.ini` — PlatformIO 프로젝트 / 환경.
- `version_gen.py`, `merge_firmware.py` — PlatformIO 빌드 스크립트.
- `docs/upstream-comparison.md` — `opendbc` 및 기타 공개 Tesla CAN 프로젝트와의
  프레임/비트 교차 검증.

---

## 크레딧 및 감사의 말

- **[flipper-tesla-fsd](https://github.com/hypery11/flipper-tesla-fsd)** — 이
  프로젝트가 T-2CAN 전용으로 다시 작성한 원본(upstream) 프로젝트.
- **[ev-open-can-tools](https://github.com/ev-open-can-tools/ev-open-can-tools)** —
  신호 정의와 주입 정책을 교차 검증하는 데 사용한 참조 도구.
- **[opendbc](https://github.com/commaai/opendbc)** (commaai) — HW4 / Party 버스
  프레임 ID, 비트 레이아웃, 가산(additive) 체크섬을 위한 참조 DBC. 이 구현의 신호를
  독립적으로 교차 검증하는 데 사용됨.

Tesla와 Model 3은 각 소유자의 상표입니다. 이 프로젝트는 Tesla와 제휴 관계가 없으며,
Tesla의 보증이나 지원을 받지 않습니다.

---

## 라이선스

**GPL-3.0.** 이 프로젝트의 파생 원본인 `hypery11/flipper-tesla-fsd`가 GPL-3.0으로
라이선스되어 있으므로, 이 재작성 버전도 동일한 라이선스로 배포됩니다. 전체 텍스트는
[`LICENSE`](LICENSE)를 참고하세요.
