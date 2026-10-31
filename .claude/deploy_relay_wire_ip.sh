#!/usr/bin/env bash
# relay-wire-ip 서버 게시 (검증용, 2026-10-11, D1 보수판). 사용: COMMIT=<sha> bash deploy_relay_wire_ip.sh <step>
# step: stage | before | backup | upload | swap | check | rollback   — 한 번에 하나씩, 앞 단계 출력을 확인한 뒤 다음.
# 원격 쪽에 셸 변수를 넘기지 않는다: 파일별 명령을 로컬에서 명시적으로 펼쳐 문자열로 만들고, 실행 전에 그대로 출력한다.
set -u
STEP="${1:?step}"; COMMIT="${COMMIT:?set COMMIT}"
KEY=~/.ssh/remote60_deploy; HOST=gnlink@192.168.0.6; DIR=/opt/gnlink/remote60-directory
STAGE=.claude/server_deploy_$COMMIT; mkdir -p "$STAGE"
SSH="ssh -o BatchMode=yes -i $KEY $HOST"
RT='XDG_RUNTIME_DIR=/run/user/$(id -u)'
MODS="server.js relay_diag.js accounts.js device_credentials.js update_manifest.js version_compare.js wake_target.js package.json"
STAMP_FILE="$STAGE/backup_stamp.txt"

run_remote() { echo ">>> remote: $1"; $SSH "$1"; }

case "$STEP" in
stage)
  for f in $MODS; do git show "$COMMIT:apps/directory/$f" > "$STAGE/$f" || exit 1; done
  (cd "$STAGE" && sha256sum $MODS | tee SHA256SUMS.local) ;;
before)
  run_remote "cd $DIR && sha256sum $MODS 2>&1; $RT systemctl --user show remote60-directory -p MainPID -p ActiveEnterTimestamp" | tee "$STAGE/nas_before.txt" ;;
backup)
  STAMP=$(date -u +%Y%m%dT%H%M%SZ); echo "$STAMP" > "$STAMP_FILE"
  run_remote "cp -a $DIR /opt/gnlink/remote60-directory-backup-$STAMP && ls -d /opt/gnlink/remote60-directory-backup-$STAMP" | tee -a "$STAGE/nas_before.txt" ;;
upload)
  for f in $MODS; do scp -q -i $KEY "$STAGE/$f" "$HOST:$DIR/$f.tmp" || exit 1; done
  run_remote "cd $DIR && sha256sum $(for f in $MODS; do printf '%s.tmp ' "$f"; done)" | sed 's/\.tmp$//' | tee "$STAGE/nas_tmp_hashes.txt"
  echo ">>> 위 해시를 SHA256SUMS.local 과 대조한 뒤 swap" ;;
swap)
  CMD="cd $DIR"; for f in $MODS; do CMD="$CMD && mv -f $f.tmp $f"; done
  CMD="$CMD && $RT systemctl --user restart remote60-directory && sleep 2 && $RT systemctl --user is-active remote60-directory && $RT systemctl --user show remote60-directory -p MainPID -p ActiveEnterTimestamp && sha256sum $MODS"
  run_remote "$CMD" | tee "$STAGE/nas_after.txt" ;;
check)
  run_remote "journalctl --user -u remote60-directory -n 25 --no-pager" | tee "$STAGE/journal_after.txt"
  echo ">>> https:"; curl -s -o /dev/null -w '%{http_code}\n' https://gnlink.shotan.net/healthz ;;
rollback)
  STAMP=$(cat "$STAMP_FILE"); B=/opt/gnlink/remote60-directory-backup-$STAMP
  CMD="cd $DIR"; for f in $MODS; do [ "$f" = relay_diag.js ] && continue; CMD="$CMD && cp -a $B/$f $f"; done
  CMD="$CMD && rm -f relay_diag.js && $RT systemctl --user restart remote60-directory && sleep 2 && $RT systemctl --user is-active remote60-directory"
  echo ">>> rollback 은 백업($B)에 relay_diag.js 가 없으므로 그 파일은 삭제한다. directory-data.json 은 건드리지 않는다."
  run_remote "$CMD" ;;
*) echo "unknown step"; exit 2 ;;
esac
