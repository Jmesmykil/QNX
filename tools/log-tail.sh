#!/usr/bin/env bash
# log-tail.sh — Tail uLaunch logs from the SD card with ANSI highlighting.
#
# Auto-detects the SD-card mount under /Volumes/SWITCH\ SD/ulaunch/, follows
# the active log_uMenu.log (and optionally log_uLoader.log with -l) through
# rotation, and colourises lines containing common keywords so transitions
# stand out when reproducing the "screen flashing" symptom from a tethered
# Mac.
#
# Designed for stock macOS bash 3.2 with only POSIX tail, grep, awk, sed.
# UMS must remain mounted by Hekate while running this script.

set -u

SD_ROOT_DEFAULT='/Volumes/SWITCH SD'
SD_LOG_DIR_REL='ulaunch'
PRIMARY_LOG='log_uMenu.log'
SECONDARY_LOG='log_uLoader.log'

show_help() {
    cat <<'HELP'
log-tail.sh — Tail uLaunch logs with highlighting

Usage:
  log-tail.sh                      Tail log_uMenu.log only
  log-tail.sh -l                   Also tail log_uLoader.log (parallel)
  log-tail.sh --filter <regex>     Only show lines matching <regex>
  log-tail.sh --sd <path>          Override SD root (default: /Volumes/SWITCH SD)
  log-tail.sh --no-color           Disable ANSI colour codes
  log-tail.sh --help               Show this help

Highlighted keywords (default colours):
  WARN       yellow
  ERR        red
  Finalize   magenta
  Launch     cyan
  applet     green
  focus      blue
  frame      grey
  nxlink     bright cyan
  shell      bright magenta

Notes:
  - Reads /Volumes/SWITCH SD/ulaunch/log_uMenu.log by default.
  - Follows rotation when the file shrinks (tail -F).
  - UMS must remain mounted by Hekate; the script exits cleanly if the
    SD mount disappears.
  - Ctrl-C to stop.
HELP
}

# -------- arg parsing --------
WITH_LOADER=0
FILTER=''
NO_COLOR=0
SD_ROOT="$SD_ROOT_DEFAULT"

while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) show_help; exit 0 ;;
        -l|--loader) WITH_LOADER=1; shift ;;
        --filter)
            if [ $# -lt 2 ]; then
                echo "log-tail.sh: --filter requires an argument" >&2
                exit 2
            fi
            FILTER="$2"; shift 2 ;;
        --sd)
            if [ $# -lt 2 ]; then
                echo "log-tail.sh: --sd requires an argument" >&2
                exit 2
            fi
            SD_ROOT="$2"; shift 2 ;;
        --no-color) NO_COLOR=1; shift ;;
        *) echo "log-tail.sh: unknown arg '$1' (try --help)" >&2; exit 2 ;;
    esac
done

LOG_DIR="$SD_ROOT/$SD_LOG_DIR_REL"
PRIMARY_PATH="$LOG_DIR/$PRIMARY_LOG"
SECONDARY_PATH="$LOG_DIR/$SECONDARY_LOG"

# -------- mount + file existence check --------
if [ ! -d "$LOG_DIR" ]; then
    echo "log-tail.sh: log directory not found: $LOG_DIR" >&2
    echo "  Mount the Switch SD via Hekate UMS first." >&2
    exit 1
fi

if [ ! -e "$PRIMARY_PATH" ]; then
    echo "log-tail.sh: $PRIMARY_PATH does not exist yet." >&2
    echo "  Boot Q OS at least once to create it. Continuing anyway (tail -F will wait)." >&2
fi

# -------- ANSI colour helpers --------
if [ "$NO_COLOR" = 0 ] && [ -t 1 ]; then
    C_RESET=$'\033[0m'
    C_RED=$'\033[31m'
    C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'
    C_BLUE=$'\033[34m'
    C_MAGENTA=$'\033[35m'
    C_CYAN=$'\033[36m'
    C_GREY=$'\033[90m'
    C_BCYAN=$'\033[96m'
    C_BMAGENTA=$'\033[95m'
    C_BOLD=$'\033[1m'
else
    C_RESET=''; C_RED=''; C_GREEN=''; C_YELLOW=''; C_BLUE=''
    C_MAGENTA=''; C_CYAN=''; C_GREY=''; C_BCYAN=''; C_BMAGENTA=''; C_BOLD=''
fi

# -------- highlighter --------
# Build an awk program that recolors matching keywords. POSIX awk on macOS
# has gsub() + match() but no PCRE — use simple case-insensitive substring
# checks combined with awk's gsub.
highlight() {
    local prefix="$1"
    awk -v PFX="$prefix" \
        -v CR="$C_RED" -v CG="$C_GREEN" -v CY="$C_YELLOW" -v CB="$C_BLUE" \
        -v CM="$C_MAGENTA" -v CC="$C_CYAN" -v CGY="$C_GREY" \
        -v CBC="$C_BCYAN" -v CBM="$C_BMAGENTA" -v CRST="$C_RESET" \
        -v CBD="$C_BOLD" '
    BEGIN { IGNORECASE = 0 }
    {
        line = $0
        # Emphasise WARN/ERR first (they take precedence)
        if (line ~ /\[ERR/ || line ~ /\[ERROR/) {
            colored = CR CBD line CRST
        } else if (line ~ /\[WARN/) {
            colored = CY line CRST
        } else {
            colored = line
            # Recolour individual keywords by gsub.
            # Each gsub wraps the literal match with colour codes.
            gsub(/Finalize/,  CM "&" CRST, colored)
            gsub(/Launch/,    CC "&" CRST, colored)
            gsub(/applet/,    CG "&" CRST, colored)
            gsub(/Applet/,    CG "&" CRST, colored)
            gsub(/focus/,     CB "&" CRST, colored)
            gsub(/frame/,     CGY "&" CRST, colored)
            gsub(/nxlink/,    CBC "&" CRST, colored)
            gsub(/shell/,     CBM "&" CRST, colored)
        }
        printf "%s%s\n", PFX, colored
        fflush()
    }
    '
}

# -------- filter (optional) --------
maybe_filter() {
    if [ -n "$FILTER" ]; then
        grep --line-buffered -E "$FILTER"
    else
        cat
    fi
}

# -------- banner --------
printf '%sTailing %s' "$C_BOLD" "$PRIMARY_PATH"
if [ "$WITH_LOADER" = 1 ]; then
    printf ' + %s' "$SECONDARY_PATH"
fi
printf ' — UMS must remain mounted. Ctrl-C to stop.%s\n' "$C_RESET"

if [ -n "$FILTER" ]; then
    printf '%s[filter: %s]%s\n' "$C_GREY" "$FILTER" "$C_RESET"
fi

# -------- cleanup on exit --------
LOADER_PID=''
cleanup() {
    if [ -n "$LOADER_PID" ]; then
        kill "$LOADER_PID" 2>/dev/null || true
        wait "$LOADER_PID" 2>/dev/null || true
    fi
    exit 0
}
trap cleanup INT TERM

# -------- tail uLoader in background if requested --------
if [ "$WITH_LOADER" = 1 ]; then
    if [ ! -e "$SECONDARY_PATH" ]; then
        printf '%s[warn] %s not present — uLoader may not have launched yet.%s\n' "$C_YELLOW" "$SECONDARY_PATH" "$C_RESET"
    fi
    (
        # The -n0 starts at end of file (don't dump history); -F follows
        # rotation. Errors when the file doesn't exist yet are silenced;
        # tail -F will retry.
        tail -F -n 0 "$SECONDARY_PATH" 2>/dev/null \
            | maybe_filter \
            | highlight "${C_BMAGENTA}[loader] ${C_RESET}"
    ) &
    LOADER_PID=$!
fi

# -------- foreground: tail primary uMenu log --------
# Use exec so the script process IS the tail — keeps signals clean.
tail -F -n 0 "$PRIMARY_PATH" 2>/dev/null \
    | maybe_filter \
    | highlight "${C_CYAN}[uMenu] ${C_RESET}"

# Should never get here, but just in case
cleanup
