#!/usr/bin/env bash
# Run only inside a disposable target container. /input contains prepare.py output.
set -euo pipefail
[[ -f /.dockerenv ]] || { printf 'Run in a disposable Docker container only\n' >&2; exit 2; }
export LC_ALL=C.UTF-8
export CMAKE_BUILD_PARALLEL_LEVEL=2
mkdir -p /results /work
exec > >(tee /results/package-check.log) 2>&1
(cd /input && sha256sum -c SHA256SUMS)
source /src/tools/packaging/assertions.sh
source /etc/os-release
case "${1:?Expected arch or fedora}" in
    arch)
        [[ "$ID" == arch ]]
        pacman -Syu --noconfirm --needed base-devel cmake ninja pkgconf git zip python qt6-base libvips imagemagick libarchive desktop-file-utils appstream xorg-server-xvfb xorg-xauth weston
        id builder >/dev/null 2>&1 || useradd --create-home builder
        cp /input/PKGBUILD /work/
        cp /input/insidious-*.tar.xz /work/
        chown -R builder:builder /work
        cd /work
        runuser -u builder -- env SRCDEST=/work MAKEFLAGS=-j2 makepkg --noconfirm --cleanbuild --force
        cp insidious-*.pkg.tar.zst /results/
        pacman -U --noconfirm insidious-*.pkg.tar.zst
        pacman -Ql insidious > /results/installed-files.txt
        pacman -Q > /results/packages.txt
        ;;
    fedora)
        [[ "$ID" == fedora && "$VERSION_ID" == 44 ]]
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False upgrade --refresh
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False install gcc-c++ cmake ninja-build git-core zip python3 qt6-qtbase-devel vips-devel vips-magick libarchive-devel desktop-file-utils appstream rpm-build xorg-x11-server-Xvfb xorg-x11-xauth weston
        mkdir -p /work/rpmbuild/{SOURCES,SPECS}
        cp /input/insidious-*.tar.xz /work/rpmbuild/SOURCES/
        cp /input/insidious.spec /work/rpmbuild/SPECS/
        rpmbuild -ba --define '_topdir /work/rpmbuild' --define '_smp_mflags -j2' /work/rpmbuild/SPECS/insidious.spec
        find /work/rpmbuild/RPMS /work/rpmbuild/SRPMS -name '*.rpm' -exec cp {} /results/ \;
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False install /work/rpmbuild/RPMS/x86_64/insidious-*.rpm
        rpm -ql insidious > /results/installed-files.txt
        rpm -qa | sort > /results/packages.txt
        ;;
    *) exit 2 ;;
esac
# Installed resources and runtime dependencies, rather than build-tree resources.
ldd /usr/bin/insidious > /results/ldd.txt
reject_matches 'not found' /results/ldd.txt
insidious --version | tee /results/version.txt
desktop-file-validate /usr/share/applications/io.github._0xf836.Insidious.desktop
appstreamcli validate --no-net /usr/share/metainfo/io.github._0xf836.Insidious.metainfo.xml
cp /usr/share/icons/hicolor/128x128/apps/io.github._0xf836.Insidious.png '/work/image spaces ü.png'
mkdir -p /work/config /work/cache /work/data
export XDG_CONFIG_HOME=/work/config XDG_CACHE_HOME=/work/cache XDG_DATA_HOME=/work/data
set +e
QT_QPA_PLATFORM=xcb xvfb-run -a timeout 5 insidious '/work/image spaces ü.png' > /results/launch.log 2>&1
status=$?
set -e
[[ "$status" == 124 ]]
reject_matches 'could not connect|could not load.*(platform|xcb|wayland)|no Qt platform|error while loading shared libraries|symbol lookup error' /results/launch.log
# Native Wayland startup without an X server or GPU.
export XDG_RUNTIME_DIR=/work/runtime WAYLAND_DISPLAY=insidious-test
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
weston --backend=headless --renderer=pixman --socket="$WAYLAND_DISPLAY" \
    --idle-time=0 --no-config --log=/results/weston.log &
weston_pid=$!
trap 'kill "$weston_pid" 2>/dev/null || true' EXIT
for attempt in {1..50}; do
    [[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]] && break
    sleep 0.1
done
[[ -S "$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY" ]]
set +e
QT_QPA_PLATFORM=wayland timeout 5 insidious '/work/image spaces ü.png' > /results/wayland-launch.log 2>&1
status=$?
set -e
[[ "$status" == 124 ]]
reject_matches 'could not connect|failed to create.*display|no Qt platform|error while loading shared libraries|symbol lookup error' /results/wayland-launch.log
kill "$weston_pid"
wait "$weston_pid" || true
trap - EXIT
# Opening an image must not create history, cache or log files.
[[ -z "$(find /work/cache /work/data -type f -print -quit)" ]]
# Removal must preserve a user's existing settings.
mkdir -p /work/config/Insidious
printf '[General]\npackageRemovalSentinel=true\n' > /work/config/Insidious/Insidious.conf
case "$1" in
    arch) pacman -R --noconfirm insidious ;;
    fedora) rpm -e insidious ;;
esac
[[ ! -e /usr/bin/insidious ]]
[[ ! -e /usr/share/applications/io.github._0xf836.Insidious.desktop ]]
[[ ! -e /usr/share/metainfo/io.github._0xf836.Insidious.metainfo.xml ]]
grep -qx 'packageRemovalSentinel=true' /work/config/Insidious/Insidious.conf
printf 'Package installation, X11/Wayland startup and removal checks passed\n'
