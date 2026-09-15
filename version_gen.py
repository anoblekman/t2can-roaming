# version_gen.py — PlatformIO pre-build extra_script.
#
# FW_VERSION 을 "YYYYMMDD.N" 형식으로 빌드 때마다 만들어 -D 로 주입한다.
#   YYYYMMDD = 빌드한 날짜(로컬)
#   N        = 그날의 빌드 횟수 (날짜가 바뀌면 1 부터 다시)
#
# 왜: 고정 "1.0.0" 이던 동안은 OTA 를 올려도 대시보드나 트레이스 헤더만 봐선
# 어떤 빌드가 올라갔는지 구분할 수 없었다. 실차 로그를 받아 분석할 때 "이 로그가
# 어느 펌웨어에서 나왔나"가 매번 추측이었다. 이제 트레이스 첫 줄이 빌드를 특정한다.
#
# 카운터는 저장소 루트 .build_number 에 "YYYYMMDD N" 한 줄로 둔다.
# .gitignore 대상 — 빌드마다 바뀌므로 추적하면 diff 소음만 난다.
Import("env")
import datetime
import os

_COUNTER = os.path.join(env.subst("$PROJECT_DIR"), ".build_number")


def _next_version():
    today = datetime.date.today().strftime("%Y%m%d")
    count = 0
    try:
        with open(_COUNTER, "r") as fh:
            saved_day, saved_count = fh.read().split()
        if saved_day == today:
            count = int(saved_count)
        # 날짜가 다르면 count 는 0 으로 둔 채 아래서 1 이 된다 (일자별 리셋).
    except (IOError, OSError, ValueError):
        count = 0          # 파일 없음/손상 → 오늘 첫 빌드로 취급
    count += 1
    try:
        with open(_COUNTER, "w") as fh:
            fh.write("%s %d" % (today, count))
    except (IOError, OSError):
        pass               # 쓰기 실패해도 빌드는 계속한다 (버전만 안 올라감)
    return "%s.%d" % (today, count)


version = _next_version()
print("[version] FW_VERSION = %s" % version)
env.Append(CPPDEFINES=[("FW_VERSION", env.StringifyMacro(version))])
