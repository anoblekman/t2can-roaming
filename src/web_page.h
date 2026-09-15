#pragma once
#include <Arduino.h>

/* ──────────────────────────────────────────────────────────────
   이 파일이 기기가 실제로 서빙하는 대시보드 UI 의 정본이다.
   문서용으로 이 UI 의 정적 미리보기를 docs/ui-mockup.html (한국어) 및
   docs/ui-mockup-en.html (영문) 로 생성해 둔다 — src/web_page.h 에서
   뽑아낸 것으로, 동적 필드는 대표 예시값으로 박아 서버 없이 열린다.
   CSS 는 iOS 시스템 컬러 / Health 풍 계측 카드 / Settings 풍 그룹 리스트 /
   51x31 스위치 / 세그먼티드 컨트롤 / 라이트·다크 테마를 쓴다. 동적 예시값
   (50 km/h, 결합(3), 카운터 숫자 등) 자리는 /api/status 폴링 JS 가 채우는
   id 로 되어 있다.

   목업에 남아 있던 "EU 제한 해제 지속 전송"·"차량 모드" 문구는 이미 정리돼
   있어(현재 목업 본문에 없음) 추가로 지울 게 없었다. "도구" 섹션은 실제로
   배선된 엔드포인트만 담는다: CAN 덤프·게이트 로그·펌웨어 업데이트(관리자
   인증). "네트워크 설정" 섹션은 /api/net 으로 SSID·AP비번·관리자비번을 바꾼다
   (Task 17). 공장 초기화는 BOOT 5초 롱프레스(하드웨어)로만 — 대시보드 버튼은
   두지 않는다.

   보드 AP 에는 인터넷이 없다: 폰트/스크립트/스타일 전부 이 파일 안에 인라인.
   외부 CDN·원격 폰트 없음. -apple-system 은 다운로드 없이 아이폰 SF Pro 를
   그대로 쓴다.
   ────────────────────────────────────────────────────────────── */
const char WEB_PAGE[] PROGMEM = R"rawliteral(<!doctype html>
<html lang="ko">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<title>T2CAN-ROAMING</title>
<style>
:root {
  --blue:   #007AFF;
  --green:  #34C759;
  --red:    #FF3B30;
  --orange: #FF9500;
  --gray:   #8E8E93;

  --bg:        #F2F2F7;
  --card:      #FFFFFF;
  --label:     #000000;
  --label2:    rgba(60,60,67,0.60);
  --label3:    rgba(60,60,67,0.30);
  --sep:       rgba(60,60,67,0.29);
  --fill:      rgba(120,120,128,0.12);
  --switch-off:rgba(120,120,128,0.16);
}
@media (prefers-color-scheme: dark) {
  :root {
    --bg:        #000000;
    --card:      #1C1C1E;
    --label:     #FFFFFF;
    --label2:    rgba(235,235,245,0.60);
    --label3:    rgba(235,235,245,0.30);
    --sep:       rgba(84,84,88,0.65);
    --fill:      rgba(120,120,128,0.24);
    --switch-off:rgba(120,120,128,0.32);
  }
}

* { box-sizing: border-box; -webkit-tap-highlight-color: transparent; }
html { -webkit-text-size-adjust: 100%; }
body {
  margin: 0;
  background: var(--bg);
  color: var(--label);
  font: 17px/1.294 -apple-system, BlinkMacSystemFont, "SF Pro Text",
        "Apple SD Gothic Neo", "Segoe UI", system-ui, sans-serif;
  padding: max(8px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right))
           max(32px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left));
  max-width: 560px;
  margin-inline: auto;
}
/* 폴링 실패(보드 연결 끊김) 표시. 상단 헤더를 없애 문구를 놓을 자리가 없으므로
   화면 전체를 흐리게 한다 — 값이 멈춘 채 그대로 보여 살아있는 것으로 오해하는
   것을 막는 게 목적이다. */
body.stale { opacity: 0.45; transition: opacity 0.2s; }

.cards { display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin: 8px 0 8px; }
.card {
  background: var(--card);
  border-radius: 14px;
  padding: 14px 15px 15px;
  display: flex; flex-direction: column; gap: 7px;
}
.card .hd {
  display: flex; align-items: center; gap: 6px;
  font-size: 15px; font-weight: 600; letter-spacing: -0.24px;
}
.card .hd .dot { width: 9px; height: 9px; border-radius: 50%; flex: none; }
.card .big {
  font-size: 26px; font-weight: 700; letter-spacing: 0.36px;
  font-variant-numeric: tabular-nums; line-height: 1.1;
}
.card .note { font-size: 13px; color: var(--label2); letter-spacing: -0.08px; }
.on  { color: var(--green); }
.off { color: var(--gray); }
.bad { color: var(--red); }


.hdr {
  font-size: 13px; font-weight: 400; color: var(--label2);
  text-transform: uppercase; letter-spacing: 0.5px;
  padding: 26px 16px 7px;
}
.grp { background: var(--card); border-radius: 10px; overflow: hidden; }
.row {
  display: flex; align-items: center; gap: 12px;
  min-height: 44px; padding: 10px 16px;
  position: relative;
}
.row + .row::before {
  content: ""; position: absolute; top: 0; left: 16px; right: 0;
  height: 1px; transform: scaleY(0.5); transform-origin: top;
  background: var(--sep);
}
.row .lb { flex: 1; min-width: 0; letter-spacing: -0.41px; }
.row .lb .d { font-size: 13px; color: var(--label2); margin-top: 2px; letter-spacing: -0.08px; }
.row .val {
  color: var(--label2); font-variant-numeric: tabular-nums;
  letter-spacing: -0.41px; text-align: right;
}
.row .val.mono { font-family: "SF Mono", ui-monospace, Menlo, monospace; font-size: 15px; }
.foot { font-size: 13px; color: var(--label2); padding: 7px 16px 0; letter-spacing: -0.08px; }
.chev { color: var(--label3); font-size: 17px; margin-left: -4px; }
.tint { color: var(--blue); }
.danger { color: var(--red); }
.tbtn {
  color: var(--blue); font-size: 15px; font-weight: 600;
  padding: 6px 14px; border-radius: 8px; background: var(--fill);
  cursor: pointer; user-select: none; letter-spacing: -0.24px; flex: none;
}

.fld {
  flex: 1; min-width: 0; text-align: right;
  border: none; background: transparent; color: var(--label);
  font: inherit; padding: 0; margin: 0;
  -webkit-appearance: none; appearance: none; outline: none;
}
.fld::placeholder { color: var(--label3); }

.sw {
  width: 51px; height: 31px; border-radius: 16px; flex: none;
  background: var(--switch-off); position: relative;
  transition: background .25s;
  cursor: pointer;
}
.sw.on { background: var(--green); }
.sw::after {
  content: ""; position: absolute; top: 2px; left: 2px;
  width: 27px; height: 27px; border-radius: 50%; background: #fff;
  box-shadow: 0 3px 8px rgba(0,0,0,.15), 0 1px 1px rgba(0,0,0,.16);
  transition: transform .25s;
}
.sw.on::after { transform: translateX(20px); }

.seg {
  display: grid; grid-auto-flow: column; grid-auto-columns: 1fr;
  background: var(--fill); border-radius: 9px; padding: 2px; gap: 2px;
}
.seg span {
  text-align: center; padding: 6px 4px; border-radius: 7px;
  font-size: 13px; font-weight: 500; letter-spacing: -0.08px;
  color: var(--label);
  cursor: pointer;
  user-select: none;
}
.seg span.sel {
  background: var(--card); font-weight: 600;
  box-shadow: 0 3px 8px rgba(0,0,0,.12), 0 3px 1px rgba(0,0,0,.04);
}
.segrow { padding: 12px 16px; }
.segrow .cap {
  font-size: 13px; color: var(--label2); margin-bottom: 8px; letter-spacing: -0.08px;
}

.ota-track { height: 8px; border-radius: 4px; background: var(--fill); overflow: hidden; }
.ota-fill { height: 100%; width: 0%; background: var(--blue); transition: width .15s; }
.ota-fill.ok { background: var(--green); }
.ota-fill.bad { background: var(--red); }

/* 하단 버전 표기 — 페이지 맨 아래 한 줄, 작게 회색. */
.ver {
  text-align: center; font-size: 12px; color: var(--label3);
  padding: 24px 0 4px; font-variant-numeric: tabular-nums;
}
</style>
</head>
<body>

<!-- ── 계측 카드 ─────────────────────────────────────────── -->
<div class="cards">

  <div class="card">
    <div class="hd"><span class="dot" id="dot-drive" style="background:var(--gray)"></span>주행</div>
    <div class="big off" id="big-drive">-</div>
    <div class="note" id="note-drive">-</div>
  </div>

  <div class="card">
    <div class="hd"><span class="dot" id="dot-summon" style="background:var(--gray)"></span>서먼</div>
    <div class="big off" id="big-summon">-</div>
    <div class="note" id="note-summon">-</div>
  </div>

</div>

<!-- ── 동작 모드 ─────────────────────────────────────────── -->
<div class="hdr">동작 모드</div>
<div class="grp">
  <div class="segrow">
    <div class="seg">
      <span id="seg-listen" onclick="setAct(false)">리슨</span>
      <span id="seg-active" onclick="setAct(true)">활성</span>
    </div>
  </div>
</div>
<!-- ── 기능 ──────────────────────────────────────────────── -->
<div class="hdr">기능</div>
<div class="grp">

  <div class="row">
    <div class="lb">잔소리 제거<div class="d">빠른 차선 변경 포함</div></div>
    <div class="sw" id="sw-nag" onclick="toggle('nag')"></div>
  </div>

  <div class="segrow">
    <div class="cap">파형</div>
    <div class="seg">
      <span id="seg-wave0" onclick="setWave(0)">기본</span>
      <span id="seg-wave1" onclick="setWave(1)">버스트</span>
      <span id="seg-wave2" onclick="setWave(2)">EPAS 충실</span>
    </div>
  </div>

  <div class="row" style="border-top:1px solid var(--sep)">
    <div class="lb">서먼 언락<div class="d">주차 중 EU 거리 제한 해제</div></div>
    <div class="sw" id="sw-smn" onclick="toggle('smn')"></div>
  </div>

</div>
<!-- ── 차량 상태 ─────────────────────────────────────────── -->
<div class="hdr">차량 상태</div>
<div class="grp">
  <div class="row"><div class="lb">DAS 신선도</div><div class="val" id="v-dasage">-</div></div>
  <div class="row"><div class="lb">AP 상태</div><div class="val" id="v-ap">-</div></div>
  <div class="row"><div class="lb">손 요구 단계</div><div class="val" id="v-hands">-</div></div>
  <div class="row"><div class="lb">제한속도</div><div class="val" id="v-speed">-</div></div>
  <div class="row"><div class="lb">차선변경 상태</div><div class="val" id="v-lane">-</div></div>
</div>
<!-- ── 버스 ──────────────────────────────────────────────── -->
<div class="hdr">버스</div>
<div class="grp">
  <div class="row"><div class="lb">CAN A <span style="color:var(--label2)">Party · 2/3</span></div><div class="val mono" id="v-canA">-</div></div>
  <div class="row"><div class="lb">CAN B <span style="color:var(--label2)">Chassis · 13/14</span></div><div class="val mono" id="v-canB">-</div></div>
</div>
<!-- ── 주입 ──────────────────────────────────────────────── -->
<div class="hdr">주입</div>
<div class="grp">
  <div class="row"><div class="lb">0x370 에코</div><div class="val mono" id="v-echo">-</div></div>
  <div class="row"><div class="lb">0x3FD 주입</div><div class="val mono" id="v-apctl">-</div></div>
  <div class="row"><div class="lb">Tesla OTA</div><div class="val" id="v-ota">-</div></div>
</div>

<!-- ── 도구 ──────────────────────────────────────────────── -->
<div class="hdr">도구</div>
<div class="grp">

  <a class="row" href="/trace/get" onclick="return dlTrace(event)" style="text-decoration:none;color:inherit">
    <div class="lb tint">게이트 로그 다운로드</div><div class="chev">›</div>
  </a>

  <div class="row" onclick="clearTrace()">
    <div class="lb danger">게이트 로그 비우기</div><div class="chev">›</div>
  </div>

  <div class="row">
    <div class="lb">펌웨어 업데이트<div class="d" id="ota-state"></div></div>
    <span class="tbtn" onclick="otaPick()">파일 선택</span>
    <input type="file" id="ota-file" accept=".bin" style="display:none" onchange="otaUpload(this)">
  </div>

  <div class="row" id="ota-progress-row" style="display:none">
    <div class="lb">
      <div class="ota-track"><div class="ota-fill" id="ota-fill"></div></div>
    </div>
    <div class="val mono" id="ota-pct">0%</div>
  </div>

</div>
<!-- ── 네트워크 설정 ─────────────────────────────────────── -->
<div class="hdr">네트워크 설정</div>
<div class="grp">
  <div class="row">
    <div class="lb" style="flex:none">네트워크 이름(SSID)</div>
    <input class="fld" id="net-ssid" type="text" maxlength="31" autocomplete="off" placeholder="T2CAN-ROAMING">
  </div>
  <div class="row">
    <div class="lb" style="flex:none">AP 비밀번호</div>
    <input class="fld" id="net-appw" type="password" maxlength="63" autocomplete="off" placeholder="8~63자 (WPA2)">
  </div>
  <div class="row">
    <div class="lb" id="net-msg">저장하면 재부팅됩니다</div>
    <span class="tbtn" onclick="saveNet()">저장</span>
  </div>
</div>

<!-- ── 0x3F8 실험 (관측·검증) ─────────────────────────────── -->
<div class="hdr">0x3F8 실험 · 관측/검증</div>
<div class="grp">
  <div class="row"><div class="lb" style="color:var(--orange);font-size:13px;line-height:1.4">⚠ 비트 위치 미검증 · 여러 개 동시 주입 가능(연관 기능은 조합 필요, 단 어느 비트 효과인지 구분은 어려움) · AP 주행(상태 3~5) 중에만 · 안전한 장소에서</div></div>
  <div class="row"><div class="lb">차선변경 깜빡이 확인 생략<div class="d" id="x3-cur-1">-</div></div><div class="sw" id="x3-sw-1" onclick="setX3f8(1)"></div></div>
  <div class="row"><div class="lb">안쪽 차선 추월 보조<div class="d" id="x3-cur-2">-</div></div><div class="sw" id="x3-sw-2" onclick="setX3f8(2)"></div></div>
  <div class="row"><div class="lb">고속도로 밖 자동 차선변경<div class="d" id="x3-cur-3">-</div></div><div class="sw" id="x3-sw-3" onclick="setX3f8(3)"></div></div>
  <div class="row"><div class="lb">고속도로 밖 확인없는 차선변경<div class="d" id="x3-cur-4">-</div></div><div class="sw" id="x3-sw-4" onclick="setX3f8(4)"></div></div>
  <div class="row"><div class="lb">차세대 자동 속도조절(크루즈)<div class="d" id="x3-cur-5">-</div></div><div class="sw" id="x3-sw-5" onclick="setX3f8(5)"></div></div>
  <div class="row"><div class="lb">제한속도 자동 반영<div class="d" id="x3-cur-6">-</div></div><div class="sw" id="x3-sw-6" onclick="setX3f8(6)"></div></div>
  <div class="row"><div class="lb">내비 경로 자동주행<div class="d" id="x3-cur-7">-</div></div><div class="sw" id="x3-sw-7" onclick="setX3f8(7)"></div></div>
  <div class="row"><div class="lb">주입 횟수</div><div class="val mono" id="x3-tx">0</div></div>
</div>

<div class="ver" id="v-ver">-</div>
<script>
var last = {
  act:false, nag:true, wave:0, smn:false,
  ap:0, hands:0, speed:0, lane:0,
  rxA:0, rxB:0, errA:0, errB:0,
  nag_tx:0, ap_tx:0,
  ota:false, ver:'-', gate:'- / -',
  das_age:999999,
  x3f8_exp:0, x3f8_tx:0, x3f8_seen:false, x3f8:[0,0,0,0,0,0,0,0]
};

/* 0x3F8 실험 신호표: byte/mask = 리드백용 비트 위치, t = 주입 목표값(0/1). */
var X3 = [
  null,
  {b:0, m:0x02, t:0},   /* 1 차선변경 확인생략 */
  {b:4, m:0x40, t:1},   /* 2 추월보조 */
  {b:6, m:0x40, t:1},   /* 3 고속밖 자동차선변경 */
  {b:7, m:0x01, t:1},   /* 4 고속밖 ULC */
  {b:2, m:0x08, t:1},   /* 5 차세대 ACC */
  {b:4, m:0x80, t:1},   /* 6 적응형 속도설정 */
  {b:5, m:0x40, t:1}    /* 7 NoA 경로추종 */
];
/* x3f8_exp 는 비트마스크. 각 토글이 해당 비트(1<<(i-1))를 독립 on/off → 여러 개
   동시 가능(연관 기능 조합 검증용). */
function setX3f8(i) {
  var m = (last.x3f8_exp || 0) ^ (1 << (i - 1));
  last.x3f8_exp = m;
  render(last);
  postSet('x3f8', m);
}
function renderX3() {
  var raw = last.x3f8 || [0,0,0,0,0,0,0,0];
  var m = last.x3f8_exp || 0;
  for (var i = 1; i <= 7; i++) {
    var sig = X3[i];
    var cur = (raw[sig.b] & sig.m) ? 1 : 0;
    var el = document.getElementById('x3-cur-' + i);
    el.textContent = '현재 ' + (cur ? 'ON' : 'OFF') + ' → 주입 시 ' + (sig.t ? 'ON' : 'OFF');
    var on = (m >> (i - 1)) & 1;
    document.getElementById('x3-sw-' + i).className = 'sw' + (on ? ' on' : '');
  }
  document.getElementById('x3-tx').textContent = fmtNum(last.x3f8_tx);
}

function fmtNum(n) {
  n = n || 0;
  try { return n.toLocaleString('ko-KR'); } catch (e) { return String(n); }
}

function apStateText(v) {
  switch (v) {
    case 0: return '꺼짐';
    case 1: return '대기';
    case 2: return '제안';
    default: return '결합';
  }
}

/* "설명 (숫자)" 표기 헬퍼 (AP 상태와 동일 스타일). 표에 없으면 '알 수 없음 (N)'. */
function labeled(map, v) {
  var t = map[v];
  return (t === undefined ? '알 수 없음' : t) + ' (' + v + ')';
}

var HANDS_MAP = {
  0:'요구없음', 1:'감지됨', 2:'미감지', 3:'시각경고', 4:'차임1', 5:'차임2',
  6:'감속', 7:'스트럭아웃', 8:'중단', 15:'SNA'
};
var LANE_MAP = {
  0:'비활성', 1:'차선없음', 2:'센서무효', 3:'추종중', 4:'고속도로진출', 5:'속도제한',
  6:'좌측만가능', 7:'우측만가능', 8:'양쪽가능', 9:'좌진행', 10:'우진행',
  11:'좌측장애대기', 12:'우측장애대기', 13:'좌전방대기', 14:'우전방대기',
  15:'좌중단(측면)', 16:'우중단(측면)', 17:'시야불량중단', 18:'상태불량중단',
  19:'깜빡이꺼짐중단', 20:'기타중단', 21:'실선', 22:'좌TTC차단', 23:'좌TTC+USS차단',
  24:'우TTC차단', 25:'우TTC+USS차단', 26:'좌차선유형차단', 27:'우차선유형차단',
  28:'손대기', 29:'타임아웃중단', 30:'미션무효중단', 31:'SNA'
};

/* 제한속도: DBC DAS_fusedSpeedLimit factor 5 (raw*5 = km/h).
   31 -> 없음, 0 -> 미확인, 그 외 raw*5 km/h.
   (단위 플래그는 599 DI_uiSpeedUnits 라 미반영 — km/h 고정 표기) */
function speedText(raw) {
  if (raw === 31) return '없음';
  if (raw === 0)  return '미확인';
  return (raw * 5) + ' km/h';
}

function render(d) {
  var parts = String(d.gate || ' / ').split(' / ');
  var txPart = parts[0] || '';
  var smPart = parts[1] || '';
  var txOk = txPart.indexOf('통과') >= 0;
  var smOk = smPart.indexOf('통과') >= 0;

  document.getElementById('v-ver').textContent = d.ver;

  document.getElementById('seg-listen').className = d.act ? '' : 'sel';
  document.getElementById('seg-active').className = d.act ? 'sel' : '';

  var dotDrive  = document.getElementById('dot-drive');
  var bigDrive  = document.getElementById('big-drive');
  if (!d.act) {
    dotDrive.style.background = 'var(--gray)';
    bigDrive.textContent = '리슨'; bigDrive.className = 'big off';
  } else if (txOk) {
    dotDrive.style.background = 'var(--green)';
    bigDrive.textContent = '주입 중'; bigDrive.className = 'big on';
  } else {
    dotDrive.style.background = 'var(--orange)';
    bigDrive.textContent = '차단'; bigDrive.className = 'big bad';
  }
  document.getElementById('note-drive').textContent = txPart;

  var dotSummon = document.getElementById('dot-summon');
  var bigSummon = document.getElementById('big-summon');
  if (smOk) {
    dotSummon.style.background = 'var(--green)';
    bigSummon.textContent = '가능'; bigSummon.className = 'big on';
  } else {
    dotSummon.style.background = 'var(--gray)';
    bigSummon.textContent = '대기'; bigSummon.className = 'big off';
  }
  document.getElementById('note-summon').textContent = smPart;

  for (var i = 0; i < 3; i++) {
    document.getElementById('seg-wave' + i).className = (d.wave === i) ? 'sel' : '';
  }
  document.getElementById('sw-nag').className = 'sw' + (d.nag ? ' on' : '');
  document.getElementById('sw-smn').className = 'sw' + (d.smn ? ' on' : '');

  document.getElementById('v-ap').textContent = apStateText(d.ap) + ' (' + d.ap + ')';
  document.getElementById('v-hands').textContent = labeled(HANDS_MAP, d.hands);
  document.getElementById('v-speed').textContent = speedText(d.speed);
  document.getElementById('v-lane').textContent = labeled(LANE_MAP, d.lane);

  var ageEl = document.getElementById('v-dasage');
  var age = (d.das_age === undefined) ? 999999 : d.das_age;
  /* 0x39B 는 이 차에서 2Hz(프레임 간격 500ms)라 신선도가 0~500ms 를 톱니처럼
     왕복한다. 임계가 500 이면 정상 동작인데도 빨강이 떴다 — 게이트가 쓰는
     SUMMON_FRESH_MS(1000ms)와 같은 기준으로 본다. */
  if (age < 1000) {
    ageEl.textContent = '정상 (' + age + 'ms)';
    ageEl.className = 'val on';
  } else {
    ageEl.textContent = '오래됨 (' + age + 'ms)';
    ageEl.className = 'val bad';
  }

  document.getElementById('v-canA').textContent = fmtNum(d.rxA) + ' / 오류 ' + fmtNum(d.errA);
  document.getElementById('v-canB').textContent = fmtNum(d.rxB) + ' / 오류 ' + fmtNum(d.errB);

  document.getElementById('v-echo').textContent = fmtNum(d.nag_tx);
  document.getElementById('v-apctl').textContent = fmtNum(d.ap_tx);
  document.getElementById('v-ota').textContent = d.ota ? '감지됨 — 송신 정지' : '감지 안 됨';

  renderX3();
}

function postSet(key, value) {
  fetch('/api/set', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ key: key, value: value })
  }).catch(function () {});
}

function toggle(key) {
  last[key] = !last[key];
  render(last);
  postSet(key, last[key]);
}

function setAct(v) {
  last.act = v;
  render(last);
  postSet('act', v);
}

function setWave(v) {
  last.wave = v;
  render(last);
  postSet('wave', v);
}

/* ── 도구: 게이트 로그 ─────────────────────────
   파일명 시각은 보드에 RTC 가 없어 브라우저 시계로 만든다. */
function pad2(n) { return (n < 10 ? '0' : '') + n; }
function stamp() {
  var d = new Date();
  return '' + d.getFullYear() + pad2(d.getMonth() + 1) + pad2(d.getDate()) +
         '_' + pad2(d.getHours()) + pad2(d.getMinutes()) + pad2(d.getSeconds());
}
function dlTrace(e) {
  e.currentTarget.setAttribute('download', 'trace_' + stamp() + '.log');
  return true;
}
function clearTrace() {
  if (!confirm('게이트 로그를 비울까요? 되돌릴 수 없습니다.')) return;
  fetch('/trace/clear').catch(function () {});
}

async function poll() {
  try {
    var r = await fetch('/api/status', { cache: 'no-store' });
    var d = await r.json();
    last = d;
    render(d);
    document.body.classList.remove('stale');
  } catch (e) {
    document.body.classList.add('stale');
  }
}

/* ── 도구: 웹 OTA ─────────────────────────────────────────
   XMLHttpRequest 로 /update 에 multipart/form-data POST. upload.onprogress 로
   진행률 표시. 성공하면 보드가 재부팅하므로 응답만 확인하고 안내한다. */
function otaPick() { document.getElementById('ota-file').click(); }

function otaUpload(input) {
  var f = input.files && input.files[0];
  if (!f) return;
  if (!/\.bin$/i.test(f.name)) { alert('firmware.bin 파일을 선택하세요.'); input.value = ''; return; }

  var stateEl = document.getElementById('ota-state');
  var rowEl   = document.getElementById('ota-progress-row');
  var fillEl  = document.getElementById('ota-fill');
  var pctEl   = document.getElementById('ota-pct');
  rowEl.style.display = 'flex';
  fillEl.className = 'ota-fill';
  fillEl.style.width = '0%';
  pctEl.textContent = '0%';
  stateEl.textContent = '업로드 중… (CAN 송신 정지)';

  var fd = new FormData();
  fd.append('firmware', f, f.name);
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/update', true);
  xhr.upload.onprogress = function (e) {
    if (!e.lengthComputable) return;
    var p = Math.round(e.loaded / e.total * 100);
    fillEl.style.width = p + '%';
    pctEl.textContent = p + '%';
  };
  xhr.onload = function () {
    if (xhr.status === 200) {
      fillEl.className = 'ota-fill ok';
      fillEl.style.width = '100%';
      pctEl.textContent = '완료';
      stateEl.textContent = '재부팅 중… 30초 후 새로고침 하세요';
    } else {
      fillEl.className = 'ota-fill bad';
      var msg = '';
      try { msg = (JSON.parse(xhr.responseText) || {}).err || ''; } catch (e) {}
      stateEl.textContent = '실패: ' + (msg || ('HTTP ' + xhr.status));
    }
    input.value = '';
  };
  xhr.onerror = function () {
    /* 재부팅으로 소켓이 끊기면 여기로도 온다 — 성공 케이스와 구분이 안 되므로
       진행률이 100% 근처였으면 재부팅으로 간주해 안내한다. */
    fillEl.className = 'ota-fill ok';
    stateEl.textContent = '연결 끊김 — 재부팅으로 보이면 30초 후 새로고침';
    input.value = '';
  };
  xhr.send(fd);
}

/* ── 네트워크 설정 ────────────────────────────────────────
   GET /api/net 로 SSID 만 프리필한다(비번칸은 항상 빈칸 — 서버가 비번을
   돌려주지 않는다). 저장은 POST /api/net → 성공 시 보드가 재부팅한다. */
function loadNet() {
  fetch('/api/net', { cache: 'no-store' }).then(function (r) { return r.json(); })
    .then(function (j) {
      if (j && j.ssid != null) document.getElementById('net-ssid').value = j.ssid;
    }).catch(function () {});
}

function saveNet() {
  var ssid   = document.getElementById('net-ssid').value;
  var appw   = document.getElementById('net-appw').value;
  var msgEl  = document.getElementById('net-msg');
  fetch('/api/net', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ ssid: ssid, ap_pw: appw })
  }).then(function (r) {
    return r.json().then(function (j) { return { status: r.status, j: j }; });
  }).then(function (res) {
    if (res.status === 200 && res.j && res.j.ok) {
      msgEl.textContent = '저장됨 — 재부팅 중. 새 이름/비번으로 다시 접속하세요.';
      msgEl.className = 'lb on';
    } else {
      var err = res.j && res.j.err;
      var m = '저장 실패';
      if (err === 'ssid') m = 'SSID 는 1~31자여야 합니다.';
      else if (err === 'appw_len') m = 'AP 비밀번호는 8~63자여야 합니다(WPA2).';
      msgEl.textContent = m;
      msgEl.className = 'lb danger';
    }
  }).catch(function () {
    /* 재부팅으로 소켓이 응답 전에 끊길 수 있다 — 저장이 됐을 가능성이 크다. */
    msgEl.textContent = '요청 전송됨 — 재부팅으로 연결이 끊겼을 수 있습니다.';
    msgEl.className = 'lb on';
  });
}

loadNet();
poll();
setInterval(poll, 1000);
</script>

</body>
</html>
)rawliteral";
