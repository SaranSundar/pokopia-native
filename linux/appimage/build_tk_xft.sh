#!/bin/bash
# build_tk_xft.sh <downloads folder> <work folder>: the Tk that make_appimage.sh --tk-xft takes (<work folder>/out). python-build-standalone's
# Tk 9.0.4 draws with X11 core fonts only (one family, the bitmap "fixed"), so the builder's window had tiny bitmap text on Linux. This builds
# the same Tk 9.0.4 with Xft, linked against the system's fontconfig and X11 at run time, with libXft shipped beside it.
# The downloads folder holds, each checked here before use (SourceForge publishes size and MD5 for the Tcl/Tk sources, no SHA-256; the .debs
# come from apt-get download, which checks them against Ubuntu's signed index):
#   tcl9.0.4-src.tar.gz and tk9.0.4-src.tar.gz from https://prdownloads.sourceforge.net/tcl/ (tcl-lang.org's download links)
#   libxft-dev, libxft2, libfontconfig-dev, libfreetype-dev of Ubuntu 22.04 (headers and libXft.so.2; the build links fontconfig, freetype,
#   Xrender and X11 against the system's own)
# The result: a neutral prefix (nothing of the build machine in its strings), RUNPATH $ORIGIN (libXft.so.2 beside it), the soname
# libtcl9tk9.0.so (AppRun preloads it; _tkinter's RPATH names the bundled Tk), no Xss, its script library attached as Tk 9 does.
# Builds at nice 19 with one job. Needs gcc, make, dpkg-deb, the X11 and Xrender headers (libx11-dev, libxrender-dev).
set -euo pipefail
DL=$(cd "$1" && pwd); X=$2
[ -e "$X" ] && { echo "build_tk_xft.sh: $X exists; give a new folder" >&2; exit 2; }
check() {   # name size md5 sha256: the published size and MD5, and the SHA-256 recorded when it was first fetched
  [ "$(stat -c %s "$DL/$1")" = "$2" ] && echo "$3  $DL/$1" | md5sum -c --quiet && echo "$4  $DL/$1" | sha256sum -c --quiet ||
    { echo "build_tk_xft.sh: $1 does not match" >&2; exit 1; }
}
check tcl9.0.4-src.tar.gz 11866337 6c409d091c8f85f8601957bb6e77c8b3 d0aed49230bc02a65c1e0229e65f34590a4b037ec40d546f32573b467f7551ea
check tk9.0.4-src.tar.gz 4613019 1b133d4ddf3dec2c01340cdc68c87096 d7a146d2917eb8b5cc95276dbf0e3d03c7464d2b19c1675357857c989301dbb4
for d in libxft-dev_2.3.4-1_amd64.deb:96061bbbfb0ec1b6fffa4e982ed52a8e2ede169710802ecb0d938d36443c8c7c \
         libxft2_2.3.4-1_amd64.deb:2f1f300e21c97023e0a8418d68e5eba095d2c5872ceab740f5d70278d94284fd \
         libfontconfig-dev_2.13.1-4.2ubuntu5_amd64.deb:2dfd75329c954d7453bf95a6057151aba638159172b7700cfa95b4901d0cbde7 \
         libfreetype-dev_2.11.1+dfsg-1ubuntu0.4_amd64.deb:e55191ef71f3312907d1d99e485fe9c2132b76feb256f9cfcb40c8aef47a5649; do
  echo "${d#*:}  $DL/${d%%:*}" | sha256sum -c --quiet || { echo "build_tk_xft.sh: ${d%%:*} does not match" >&2; exit 1; }
done

mkdir -p "$X/src" "$X/deb" "$X/bin" "$X/out"; X=$(cd "$X" && pwd); R=$X/deb; T=$X/src/tcl9.0.4/unix
tar -xzf "$DL/tcl9.0.4-src.tar.gz" -C "$X/src" --no-same-owner; tar -xzf "$DL/tk9.0.4-src.tar.gz" -C "$X/src" --no-same-owner
for p in "$DL"/*.deb; do dpkg-deb -x "$p" "$R"; done   # unpacked only: no package scripts run, nothing installed
# xft and fontconfig for Tk's configure, from the unpacked packages; anything else to the real pkg-config
cat > "$X/bin/pkg-config" <<SH
#!/bin/sh
case "\$*" in
  *xft*|*fontconfig*)
    case "\$*" in
      *--cflags*) echo "-I$R/usr/include -I$R/usr/include/freetype2" ;;
      *--libs*) echo "-L$R/usr/lib/x86_64-linux-gnu -lXft -l:libfontconfig.so.1 -l:libfreetype.so.6 -lXrender -lX11" ;;
      *--modversion*) echo 2.3.4 ;;
    esac; exit 0 ;;
esac
exec /usr/bin/pkg-config "\$@"
SH
chmod +x "$X/bin/pkg-config"
export PATH="$X/bin:$PATH" LD_LIBRARY_PATH=$T   # Tk's build runs Tcl's tclsh (from the Tcl tree) to attach its script library

# Tcl: only its core is needed (headers, tclConfig.sh, tclsh); its bundled packages' configure is not, so make stops there with an error
(cd "$T" && nice -n 19 ./configure --prefix=/opt/pokopia-native/tk --enable-shared > "$X/tcl-configure.log" 2>&1 &&
   { nice -n 19 make -j 1 binaries > "$X/tcl-make.log" 2>&1 || true; })
[ -f "$T/libtcl9.0.so" ] && [ -x "$T/tclsh" ] || { echo "build_tk_xft.sh: Tcl did not build (see $X/tcl-make.log)" >&2; exit 1; }
cd "$X/src/tk9.0.4/unix"
nice -n 19 ./configure --prefix=/opt/pokopia-native/tk --with-tcl="$T" --enable-shared --enable-xft --disable-xss > "$X/tk-configure.log" 2>&1
grep -q '^checking whether to use xft... yes' "$X/tk-configure.log" || { echo "build_tk_xft.sh: Tk's configure found no Xft" >&2; exit 1; }
nice -n 19 make -j 1 'LD_SEARCH_FLAGS=-Wl,-rpath,\$$ORIGIN' 'TK_SHLIB_LD_EXTRAS=-Wl,-soname,libtcl9tk9.0.so' > "$X/tk-make.log" 2>&1

cp libtcl9tk9.0.so "$X/out/"; cp "$R/usr/lib/x86_64-linux-gnu/libXft.so.2.3.4" "$X/out/libXft.so.2"
cp ../license.terms "$X/out/TK-LICENSE.terms"; cp "$R/usr/share/doc/libxft2/copyright" "$X/out/LIBXFT-COPYRIGHT"
readelf -d "$X/out/libtcl9tk9.0.so" | grep -q 'Library soname: \[libtcl9tk9.0.so\]' || { echo "build_tk_xft.sh: no soname" >&2; exit 1; }
readelf -d "$X/out/libtcl9tk9.0.so" | grep -q 'Library runpath: \[\$ORIGIN\]' || { echo "build_tk_xft.sh: RUNPATH is not \$ORIGIN" >&2; exit 1; }
if strings "$X/out/libtcl9tk9.0.so" | grep -qE '/home/|/Users/|/mnt/'; then echo "build_tk_xft.sh: the library names a local folder" >&2; exit 1; fi
echo "Tk with Xft: $X/out ($(unzip -l "$X/out/libtcl9tk9.0.so" 2>/dev/null | grep -c 'tk_library/.*\.tcl') script files attached; needs glibc" \
     "$(objdump -T "$X/out/libtcl9tk9.0.so" | grep -oE 'GLIBC_[0-9.]+' | sort -V | tail -1))"
