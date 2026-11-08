#!/bin/bash
# make_appimage.sh: Pokopia-Native-Builder-<version>-x86_64.AppImage from the "for Linux" download (docs/LINUX.md). Runs on Linux (or WSL).
#   make_appimage.sh --zip <Pokopia-Native-for-Linux(-beta).zip> --tools <folder> --dotnet <.NET root> --rules <rules.tsv> --out <folder> [--work <folder>]
#                    [--version <v>]: the builder's version (its name, VERSION, its folder); default TOOL_VERSION, with "-beta" for a beta zip
#                    [--tk-xft <folder>]: a Tk 9.0.4 built with Xft (libtcl9tk9.0.so), libXft.so.2 and their licences, put beside python-build-standalone's
#                    own Tk (which draws with X11 bitmap fonts only) in python/lib/tk-xft; AppRun uses it when it loads, else the bundled Tk
# --tools holds the downloads, each checked against its publisher's sha256 before use: python-build-standalone's
# cpython-3.12.16+20261026-x86_64-unknown-linux-gnu-install_only.tar.gz, AppImage's appimagetool-x86_64.AppImage and its static type-2 runtime
# runtime-x86_64 (no libfuse2 needed). --dotnet is a .NET 10 runtime install (dotnet-install.sh --runtime dotnet). The contents (payload/):
# python/ with lz4, fonttools, pillow and zstandard from PyPI, dotnet/, installer/ (the download, with nxnative set up by its own setup and
# its git history left out), VERSION and ID. Everything is scanned (scan_payload.py with --rules) before it is packed: no private data, no
# game files, no video. Nothing of the game is in it.
set -euo pipefail
export PYTHONDONTWRITEBYTECODE=1   # no .pyc from this build in the contents: they would name the folder it ran in
HERE=$(cd "$(dirname "$0")" && pwd)
ZIP= TOOLS= DOTNET= RULES= OUT= VER= TKXFT= WORK=${TMPDIR:-/tmp}/pokopia-appimage
while [ $# -gt 0 ]; do
  case $1 in --zip) ZIP=$2 ;; --tools) TOOLS=$2 ;; --dotnet) DOTNET=$2 ;; --rules) RULES=$2 ;; --out) OUT=$2 ;; --work) WORK=$2 ;; --version) VER=$2 ;;
    --tk-xft) TKXFT=$2 ;;
    *) echo "make_appimage.sh: unknown $1" >&2; exit 2 ;; esac
  shift 2
done
for v in ZIP TOOLS DOTNET RULES OUT; do [ -n "${!v}" ] || { echo "make_appimage.sh: --$(echo $v | tr A-Z a-z) is needed" >&2; exit 2; }; done
PBS=cpython-3.12.16+20261026-x86_64-unknown-linux-gnu-install_only.tar.gz
check() { echo "$2  $TOOLS/$1" | sha256sum -c --quiet || { echo "make_appimage.sh: $1 does not match its published sha256" >&2; exit 1; }; }
check $PBS 6a79e922e50006b601f29e688407659da1c2b86ebcd4e9c7254cf234be8b89de
check appimagetool-x86_64.AppImage 62aebc77c2149e96b593b94da9bbd1881e0984a56399ff6d7a03b7ddd7c9ce68
check runtime-x86_64 617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46

rm -rf "$WORK"; mkdir -p "$WORK/AppDir/payload"
A=$WORK/AppDir P=$A/payload
say() { echo "[$(date +%H:%M:%S)] $*"; }

say "Python: $PBS"
tar -xzf "$TOOLS/$PBS" -C "$P"
PY=$P/python/bin/python3
"$PY" -m pip install --quiet --no-cache-dir --no-compile --only-binary=:all: --disable-pip-version-check lz4 fonttools pillow zstandard
"$PY" -m pip list --format=freeze --disable-pip-version-check 2>/dev/null | grep -iE '^(lz4|fonttools|pillow|zstandard)==' > "$WORK/python-modules.txt"
"$PY" -c 'import lz4, fontTools, PIL, zstandard, tkinter; print("python modules and tkinter: ok, Tk", tkinter.TkVersion)'
"$PY" -m pip uninstall --quiet --yes pip   # a player's Python needs no pip (its Windows launchers carry their builder's paths)
# what a player's Python never needs: the test suite, IDLE, headers, manuals; scripts whose first line names this build folder
rm -rf "$P/python/lib/python3.12/test" "$P/python/lib/python3.12/idlelib" "$P/python/include" "$P/python/share"
find "$P/python/bin" -mindepth 1 ! -name 'python3' ! -name 'python3.12' ! -name 'python' -delete
if [ -n "$TKXFT" ]; then
  say "Tk with Xft: $TKXFT (beside the bundled Tk; AppRun takes it where it loads)"
  for f in libtcl9tk9.0.so libXft.so.2 TK-LICENSE.terms LIBXFT-COPYRIGHT; do [ -f "$TKXFT/$f" ] || { echo "make_appimage.sh: $TKXFT/$f is missing" >&2; exit 1; }; done
  mkdir -p "$P/python/lib/tk-xft" && cp "$TKXFT"/{libtcl9tk9.0.so,libXft.so.2,TK-LICENSE.terms,LIBXFT-COPYRIGHT} "$P/python/lib/tk-xft/"
  if [ -n "${DISPLAY:-}" ]; then   # the same Tk as the bundled one, and drawing with Xft
    LD_LIBRARY_PATH="$P/python/lib/tk-xft" "$PY" -c 'import tkinter as t; r = t.Tk(); r.withdraw(); print("Tk", r.call("info", "patchlevel"), "font system", r.call("::tk::pkgconfig", "get", "fontsystem")); r.destroy()'
  fi
fi

say "the .NET runtime: $DOTNET"
mkdir -p "$P/dotnet" && cp -a "$DOTNET/dotnet" "$DOTNET/host" "$DOTNET/shared" "$DOTNET/LICENSE.txt" "$DOTNET/ThirdPartyNotices.txt" "$P/dotnet/"

say "the download: $(basename "$ZIP")"
mkdir -p "$WORK/unzip" && unzip -q "$ZIP" -d "$WORK/unzip"
mv "$WORK/unzip"/pokopia-native-*/ "$P/installer"
[ -n "$VER" ] || VER=$(sed -n "s/^TOOL_VERSION = '\([^']*\)'.*/\1/p" "$P/installer/nxs2.py")$(case $(basename "$ZIP") in *-beta.zip) echo -beta ;; esac)

say "nxnative: the download's own setup, then its git history left out"
(cd "$P/installer" && PATH="$P/dotnet:$PATH" DOTNET_ROOT="$P/dotnet" "$PY" -s nxs2.py setup --no-ios)
NX=$P/installer/nxnative
echo "nxnative $(git -C "$NX" rev-parse --short HEAD) with the Switch 2 patch" > "$NX/.bundled"
rm -rf "$NX/.git" "$P/installer"/setup-*.log
# what the Linux build never reads from nxnative: its documentation (screenshots of other games), fuzz and test inputs, its contributors' notes,
# and the Mac translator's debug symbols (paths of the machine it was built on)
rm -rf "$NX/docs" "$NX/dm/tools/fuzz" "$NX/dm/tools/tests" "$NX/AGENTS.md" "$NX"/dm/gfx/tools/dmsh_xlate_bin/*.pdb "$NX/dm/gfx/tools/dmsh_xlate_bin/Gommon.dll"
find "$P/installer" -name __pycache__ -type d -prune -exec rm -rf {} +

echo "$VER" > "$P/VERSION"
{ sha256sum "$ZIP" | cut -d' ' -f1; echo "$PBS"; ls "$P/dotnet/shared/Microsoft.NETCore.App"; cat "$WORK/python-modules.txt"
  [ -z "$TKXFT" ] || sha256sum "$P/python/lib/tk-xft/libtcl9tk9.0.so" | cut -d' ' -f1; } | sha256sum | cut -c1-16 > "$P/ID"
cp "$HERE/AppRun" "$A/AppRun" && chmod +x "$A/AppRun"
cp "$HERE/pokopia-native-builder.desktop" "$A/"
"$PY" "$HERE/make_icon.py" "$A/pokopia-native-builder.png" "$(fc-match -f '%{file}' 'DejaVu Sans:bold')"
ln -s pokopia-native-builder.png "$A/.DirIcon"

say "scanning the contents"
"$PY" "$HERE/scan_payload.py" "$A" "$RULES"

mkdir -p "$OUT"
NAME=Pokopia-Native-Builder-$VER-x86_64.AppImage
say "packing $NAME"
ARCH=x86_64 APPIMAGE_EXTRACT_AND_RUN=1 ${PACK_ON_ONE_CPU:+taskset -c 0 nice -n 19} "$TOOLS/appimagetool-x86_64.AppImage" --runtime-file "$TOOLS/runtime-x86_64" --no-appstream "$A" "$OUT/$NAME" > "$WORK/appimagetool.log" 2>&1 \
  || { tail -20 "$WORK/appimagetool.log"; exit 1; }
chmod +x "$OUT/$NAME"
(cd "$OUT" && sha256sum "$NAME" > "$NAME.sha256")
say "$OUT/$NAME: $(stat -c %s "$OUT/$NAME") bytes, sha256 $(cut -d' ' -f1 "$OUT/$NAME.sha256")"
cat "$WORK/python-modules.txt"
