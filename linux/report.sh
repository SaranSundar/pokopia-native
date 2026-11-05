#!/bin/bash
# report.sh [build folder]: one archive with what a Pokopia Native for Linux tester sends back (docs/LINUX.md): the logs of the last runs and of the
# build, and facts about this computer (CPU, GPU, driver, distribution, memory, display). It never takes saves, game files, shader files or the dump, and it
# rewrites your home folder, user name and computer name out of everything it keeps. It shows you every file before it writes the archive.
#   report.sh                     the game's logs (~/.cache/pokopia-native) and this computer's facts
#   report.sh ~/Games/Pokopia_native   also that build folder's stage logs (logs/*.log, never anything else from it)
set -uo pipefail
BUILD=${1:-}
STAMP=$(date +%Y%m%d-%H%M%S)
OUT=${REPORT_DIR:-$HOME}/pokopia-report-$STAMP.tar.gz
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
R=$T/pokopia-report-$STAMP; mkdir -p "$R/logs" "$R/build-logs"
ME=$(id -un); HOST=$(hostname 2>/dev/null || cat /etc/hostname 2>/dev/null || echo "")
# The user name as a word everywhere, unless it says nothing about anyone: the Steam Deck's "deck", or one too short to tell from ordinary words
NAMEWORD=$ME; { [ "$ME" = deck ] || [ ${#ME} -lt 3 ]; } && NAMEWORD=

# ---- facts about this computer (commands that are missing are skipped)
{
  echo "report.sh $STAMP"
  echo "== system"; (. /etc/os-release 2>/dev/null; echo "distribution: ${PRETTY_NAME:-?} (${ID:-?} ${VERSION_ID:-?})")
  [ -f /etc/steamos-release ] && echo "SteamOS: $(cat /etc/steamos-release)"
  echo "kernel: $(uname -srm)"; echo "glibc: $(ldd --version 2>/dev/null | head -1)"
  echo "session: ${XDG_SESSION_TYPE:-?} desktop ${XDG_CURRENT_DESKTOP:-?}$( [ -n "${GAMESCOPE_WAYLAND_DISPLAY:-}" ] && echo ', Gamescope (Game Mode)')"
  echo "== cpu"; lscpu 2>/dev/null | grep -E '^(Model name|CPU\(s\)|Thread|Core|Socket|Flags)' | sed -E 's/^(Flags:).*/\1 '"$(grep -o -w -E 'avx2|fma|bmi2|f16c|movbe' /proc/cpuinfo | sort -u | tr '\n' ' ')"'/'
  echo "== memory"; free -h 2>/dev/null; echo "vm.max_map_count: $(cat /proc/sys/vm/max_map_count 2>/dev/null)"
  echo "== gpu"; lspci -nn 2>/dev/null | grep -E 'VGA|3D|Display'
  echo "== vulkan"; vulkaninfo --summary 2>/dev/null | sed -n '/Devices:/,$p' | grep -E 'GPU|deviceName|deviceType|driverName|driverInfo|apiVersion|conformanceVersion' || echo "vulkaninfo not installed (it is in the vulkan-tools package)"
  echo "== tools"; python3 --version 2>&1; ~/.dotnet/dotnet --list-runtimes 2>/dev/null | grep NETCore || dotnet --list-runtimes 2>/dev/null | grep NETCore
  echo "== disks"; df -h "$HOME" ${BUILD:+"$BUILD"} 2>/dev/null
} > "$R/system.txt" 2>&1

# ---- logs: the game's own (newest five runs), the launcher's, and a build folder's stage logs. Nothing else is read.
LOGROOT=${XDG_CACHE_HOME:-$HOME/.cache}/pokopia-native
if [ -d "$LOGROOT" ]; then
  find "$LOGROOT" -type f \( -name 'nxrt-*.log' -o -name '*.crash.txt' \) -printf '%T@ %p\n' 2>/dev/null | sort -rn | head -n 10 | cut -d' ' -f2- |
    while read -r f; do cp "$f" "$R/logs/"; done
fi
if [ -n "$BUILD" ] && [ -d "$BUILD/logs" ]; then
  for f in "$BUILD"/logs/*.log "$BUILD"/logs/*.txt; do [ -f "$f" ] && cp "$f" "$R/build-logs/"; done
  [ -f "$BUILD/build-log.txt" ] && cp "$BUILD/build-log.txt" "$R/build-logs/"
fi

# ---- anonymise: home folder, user and computer names, other people's addresses; room codes are secrets
for f in $(find "$R" -type f); do
  sed -i -E \
    -e "s#$HOME#~#g" -e "s#/home/$ME\b#~#g" -e "s#/var/home/$ME\b#~#g" ${NAMEWORD:+-e "s#\b$NAMEWORD\b#<user>#g"} \
    ${HOST:+-e "s#\b$HOST\b#<computer>#g"} \
    -e 's#\b([0-9A-TV-Z]{4}-){3}[0-9A-TV-Z]{4}\b#<room code>#g' \
    -e 's#\b([0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2}\b#<mac address>#g' \
    "$f"
  # what was typed into the game's keyboard (the character's name): the log keeps that a box was asked for, not the text
  perl -i -pe "if (/swkbd:/) { s/initial '.*?' limit/initial '<text>' limit/; s/OK '.*'\$/OK '<typed text>'/ }" "$f" 2>/dev/null || true
  # public IPv4 addresses on the logs' network lines (private, loopback and link-local ones say only that a LAN is there); system.txt names none, and
  # its versions (a kernel's 6.1.52.1, Vulkan's conformance 1.3.6.0) look like addresses
  [ "${f##*/}" = system.txt ] || perl -i -pe 'if (/coop|ldn|link ?play|join|relay|connect|room|socket|udp|tcp|address|peer|\blan\b/i) { s/\b((\d{1,3})\.(\d{1,3})\.\d{1,3}\.\d{1,3})\b/(($2==10)||($2==127)||($2==192&&$3==168)||($2==172&&$3>=16&&$3<=31)||($2==169&&$3==254)||($2==100&&$3>=64&&$3<=127)) ? $1 : "<internet address>"/ge }' "$f" 2>/dev/null || true
done

# ---- refuse anything that should never be in it: game data, saves, shaders, or a name left behind
BAD=$(find "$R" -type f \( -iname '*.bin' -o -iname '*.img' -o -iname '*.nso' -o -iname '*.nca' -o -iname '*.nsp' -o -iname '*.spv' -o -iname '*.ctl' \
          -o -iname '*.code' -o -iname '*.so' -o -iname '*.db' -o -iname '*.dds' -o -iname '*.png' \) -print)
LEFT=$(grep -rlI -e "$HOME" -e "/home/$ME" ${HOST:+-e "$HOST"} "$R" 2>/dev/null; [ -n "$NAMEWORD" ] && grep -rlI -w -e "$NAMEWORD" "$R" 2>/dev/null)
if [ -n "$BAD$LEFT" ]; then echo "report.sh: stopped, these must not leave this computer:"; echo "$BAD$LEFT"; exit 1; fi

echo "The report will hold these files (nothing else):"
(cd "$T" && find . -type f -printf '  %P (%s bytes)\n' | sort)
echo
echo "Read them first if you like: $R"
read -r -p "Write $OUT? [y/N] " ok
case $ok in y|Y|yes) ;; *) echo "nothing written"; exit 0 ;; esac
tar -czf "$OUT" -C "$T" "pokopia-report-$STAMP" && echo "written: $OUT  (send this file; it has no saves, no game files and no names)"
