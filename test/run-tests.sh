#!/usr/bin/env bash
# 호스트 단위 테스트 실행기.
#
# WinLibs MinGW-w64 는 winget 으로 설치되어 있는데, 셸 세션에 따라 PATH 에
# 잡히지 않는 경우가 있다. 여기서 직접 붙여 준다.
# 다른 환경에서는 PATH 의 gcc / make 를 그대로 쓴다.
set -e

WINLIBS="$LOCALAPPDATA/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"
[ -d "$WINLIBS" ] && export PATH="$PATH:$WINLIBS"

command -v gcc >/dev/null 2>&1 || { echo "ERROR: gcc 를 찾을 수 없습니다"; exit 127; }
MAKE="$(command -v make 2>/dev/null || command -v mingw32-make 2>/dev/null)"
[ -n "$MAKE" ] || { echo "ERROR: make 를 찾을 수 없습니다"; exit 127; }

cd "$(dirname "$0")"
exec "$MAKE" "$@"
