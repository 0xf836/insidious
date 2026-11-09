#!/usr/bin/env bash
# Run inside a disposable official distribution container. /src is read-only.
set -euo pipefail
export LC_ALL=C.UTF-8
mkdir -p /results
date -u +%Y-%m-%dT%H:%M:%SZ > /results/started-at.txt
exec > >(tee /results/build.log) 2>&1
trap 'status=$?; cp -a /work/build/Testing /results/ 2>/dev/null || true; exit "$status"' EXIT
[[ "$(uname -m)" == x86_64 ]]
source /etc/os-release
case "${1:?Expected arch or fedora}" in
    arch)
        [[ "$ID" == arch ]]
        pacman -Syu --noconfirm --needed base-devel cmake ninja git qt6-base libvips imagemagick libheif libde265 libjxl libexif rust cargo libarchive zip python desktop-file-utils appstream
        pacman -Q > /results/packages.txt
        cp /etc/pacman.conf /etc/pacman.d/mirrorlist /results/
        ;;
    fedora)
        [[ "$ID" == fedora && "$VERSION_ID" == 44 ]]
        # H.264 is outside the viewer allowlist; avoid the optional Cisco repository.
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False upgrade --refresh
        # RPM Fusion supplies the system libheif HEVC plugins.
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False install https://mirrors.rpmfusion.org/free/fedora/rpmfusion-free-release-44.noarch.rpm
        dnf -y --disable-repo=fedora-cisco-openh264 --setopt=install_weak_deps=False install gcc-c++ cmake ninja-build git qt6-qtbase-devel vips-devel vips-magick vips-heif libheif-freeworld vips-jxl libjxl-devel libexif-devel rust cargo rustfmt clippy libarchive-devel zip python3 desktop-file-utils appstream
        rpm -qa | sort > /results/packages.txt
        cp -a /etc/yum.repos.d /results/
        ;;
    *) exit 2 ;;
esac
cat /etc/os-release | tee /results/os-release
uname -m
cmake --version
c++ --version
pkg-config --modversion Qt6Core vips-cpp libarchive
# Copy a source export to writable container storage; never write build output to /src.
mkdir -p /work/src
tar -C /src --exclude=./.git --exclude=./build --exclude=./.ci-results -cf - . | tar -C /work/src -xf -
cd /work/src
python3 -m unittest discover -s tests -p 'ci_policy_tests.py' -v
python3 tools/ci/checks.py validate
export CARGO_TARGET_DIR=/work/build/cargo
cargo fetch --locked --manifest-path rust/jxl_bridge/Cargo.toml
cargo fmt --manifest-path rust/jxl_bridge/Cargo.toml -- --check
cargo clippy --frozen --manifest-path rust/jxl_bridge/Cargo.toml -- -D warnings
cmake -S . -B /work/build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DINSIDIOUS_BUILD_MEMORY_AUDIT=OFF
cmake --build /work/build --parallel 2
ctest --test-dir /work/build --output-on-failure --output-junit /results/ctest.xml
/work/build/insidious --version
ldd /work/build/insidious > /results/ldd.txt
