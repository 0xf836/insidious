# Maintainer: Insidious contributors

pkgname=insidious-git
pkgver=0.1.0.r1.gf977965
pkgrel=1
pkgdesc='Fast, privacy-conscious image viewer for Linux'
arch=('x86_64')
url='https://github.com/0xf836/insidious'
license=('MIT')
depends=(
    'qt6-base>=6.6'
    'libvips>=8.18.3'
    'libarchive'
)
makedepends=(
    'cmake'
    'git'
    'ninja'
    'pkgconf'
)
checkdepends=('zip')
provides=('insidious')
conflicts=('insidious')
source=("insidious::git+${url}.git")
b2sums=('SKIP')

pkgver() {
    cd "${srcdir}/insidious"

    local project_version
    IFS= read -r project_version < version.txt || return 1
    [[ $(wc -l < version.txt) -eq 1 ]] || return 1
    [[ "$project_version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] || return 1
    printf '%s.r%s.g%s' \
        "${project_version}" \
        "$(git rev-list --count HEAD)" \
        "$(git rev-parse --short=7 HEAD)"
}

build() {
    cmake -S "${srcdir}/insidious" -B build -G Ninja \
        -DCMAKE_BUILD_TYPE=None \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build build
}

check() {
    ctest --test-dir build --output-on-failure
}

package() {
    DESTDIR="${pkgdir}" cmake --install build
}
