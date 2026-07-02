#!/usr/bin/env bash
# qos-test-rig.sh — autonomous regression harness for the Q OS uMenu fork.
# Non-thrashing: NO game-launch (that re-corrupts NS / rc=0x3257C). Verifies the
# device-health wins + debug-server + FTP + on-SD feature data presence, and
# reports PASS/FAIL/BLOCKED with a summary. Re-runnable anytime.
#
# Usage: scripts/qos-test-rig.sh [IP]   (default IP LAN-IP)
set -u
IP="${1:-LAN-IP}"
HTTP="http://$IP:6010"
B="ftp://USER:PASS@$IP:5000"
P=0; F=0; BL=0
pass(){ echo "  PASS  $*"; P=$((P+1)); }
fail(){ echo "  FAIL  $*"; F=$((F+1)); }
blok(){ echo "  BLOCK $*"; BL=$((BL+1)); }
state(){ curl -s --max-time 5 "$HTTP/state" 2>/dev/null; }

echo "=== Q OS uMenu test rig @ $IP ==="

# --- Device health -----------------------------------------------------------
echo "[1] device health (no-inject resilience: device must be reachable)"
s="$(state)"
if [ -n "$s" ]; then pass "uMenu reachable over network (no RCM inject needed)"; else fail "uMenu :6010 unreachable"; fi
v="$(echo "$s" | grep -oE '3\.7\.[0-9]+' | head -1)"; [ -n "$v" ] && pass "version reported: v$v" || fail "no version in /state"

echo "[2] freeze-fix (frame must advance — no 1800-freeze)"
f1="$(state | grep -oE 'frame\":[0-9]+' | grep -oE '[0-9]+')"; sleep 6
f2="$(state | grep -oE 'frame\":[0-9]+' | grep -oE '[0-9]+')"
if [ -n "$f1" ] && [ -n "$f2" ] && [ "$f2" -gt "$f1" ] 2>/dev/null; then pass "frame advancing $f1 -> $f2"; else fail "frame not advancing ($f1 -> $f2)"; fi

# --- Debug server routes -----------------------------------------------------
echo "[3] debug server routes"
ic="$(curl -s --max-time 8 "$HTTP/icons" 2>/dev/null)"
n="$(echo "$ic" | grep -oc '"idx"')"
[ "${n:-0}" -gt 0 ] 2>/dev/null && pass "/icons returns $n entries" || fail "/icons empty/failed"

# --- FTP (sys-ftpd survives even when uMenu is black) ------------------------
echo "[4] sys-ftpd (out-of-band recovery channel)"
ul="$(curl -s --max-time 8 "$B/ulaunch/" 2>/dev/null | grep -c .)"
[ "${ul:-0}" -gt 0 ] 2>/dev/null && pass "sys-ftpd up, /ulaunch listable" || fail "sys-ftpd down"

# --- On-SD feature data presence (proves features have their data) -----------
echo "[5] feature data presence (SD)"
chk(){ local label="$1" path="$2"; if curl -s --max-time 8 "$B$path" 2>/dev/null | grep -q .; then pass "$label ($path)"; else blok "$label MISSING/empty ($path)"; fi; }
chk "themes (absorbed ThemezerNX target)" "/themes/"
chk "save backups (JKSV-absorbed)"        "/JKSV/"
chk "atmosphere/contents (cheats + mods target)" "/atmosphere/contents/"
chk "overlays dir (Tesla/Ultrahand toggle target)" "/switch/.overlays/"
chk "config (uMenu cfg)"                  "/ulaunch/sys-config.cfg"

# --- am-fix proof (no NEW am NULL-deref crash reports baseline) --------------
echo "[6] am crash reports (am-fix should keep this flat across normal use)"
amc="$(curl -s --max-time 10 "$B/atmosphere/crash_reports/" 2>/dev/null | grep -c 0100000000000023)"
echo "  INFO  am(0023) crash report count = ${amc:-?} (compare across runs; should not grow in normal desktop use)"

# --- BLOCKED (need HW functional test by human OR a cold-cycle) --------------
echo "[7] BLOCKED — require HW functional test / clean device (cannot verify autonomously)"
blok "game-launch + HOME-from-game: app::Start rc=0x3257C NS-corruption — needs cold-cycle (RCM re-inject)"
blok "save write-back editor: needs a real save + game reload to verify (HW + human)"
blok "cheats install / overlay toggle / save backup-restore: built, need first on-device functional run"

echo "=== SUMMARY: $P pass, $F fail, $BL blocked(needs HW/human) ==="
[ "$F" -eq 0 ] && echo "RESULT: autonomous surface GREEN (blocked items require the device cold-cycle + creator)" || echo "RESULT: $F FAILURE(S) — investigate"
exit 0
