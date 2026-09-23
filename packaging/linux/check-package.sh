#!/bin/bash
# Checks a deb or rpm package in a plain container of the distribution it is
# built for (CI Build, .github/actions/docker-package) (#226):
#
# 1. It ships only what the application needs at runtime: no static
#    libraries, headers (except the Plugin SDK header), CMake or pkg-config
#    files. It does ship the command line tool beside the application (#430).
# 2. It declares the distribution's Qt packages with the Qt it was built
#    against ($QT_VERSION) as the minimum version.
# 3. The package manager installs it (dry run) when the distribution has that
#    Qt; when the distribution's Qt is older, it must refuse because of Qt, and
#    only because of Qt. Users of such a distribution take the AppImage.
#
# Usage: QT_VERSION=6.11.2 check-package.sh <package.deb|package.rpm>
set -euo pipefail

package=${1:?usage: QT_VERSION=<version> $0 <package.deb|package.rpm>}
: "${QT_VERSION:?QT_VERSION must name the Qt version the package was built against}"

fail() {
    echo "::error::$(basename "$package"): $*"
    exit 1
}

# version_ge A B: A >= B. dpkg compares Debian versions; the rpm containers
# have no comparison command without rpmdevtools, and sort -V agrees with rpm on
# the plain dotted Qt versions compared here.
version_ge() {
    case $kind in
        deb) dpkg --compare-versions "$1" ge "$2" ;;
        rpm) [ "$(printf '%s\n%s\n' "$1" "$2" | sort -V | head -n 1)" = "$2" ] ;;
    esac
}

case $package in
    *.deb)
        kind=deb
        files=$(dpkg-deb --fsys-tarfile "$package" | tar -t | sed 's|^\./|/|')
        requires=$(dpkg-deb --field "$package" Depends | tr ',' '\n' | sed 's/^ *//')
        qt_requirement="libqt6core6t64 (>= $QT_VERSION) | libqt6core6 (>= $QT_VERSION)"
        ;;
    *.rpm)
        kind=rpm
        files=$(rpm -qlp "$package")
        requires=$(rpm -qpR "$package")
        qt_requirement="qt6-qtbase >= $QT_VERSION"
        ;;
    *) fail "not a .deb or .rpm package" ;;
esac

echo "Files:"
echo "$files"
echo "Declared dependencies:"
echo "$requires"

# 1. Contents
unwanted=$(echo "$files" | grep -E '\.(a|la|pc)$|/include/|/lib(64)?/cmake/|/lib(64)?/pkgconfig/' \
    | grep -Ev '^/usr/include/?$|^/usr/include/logsquirl/?$|^/usr/include/logsquirl/logsquirl_plugin_api\.h$' || true)
if [ -n "$unwanted" ]; then
    fail "ships build-time files of its dependencies: $(echo "$unwanted" | tr '\n' ' ')"
fi
echo "$files" | grep -qx '/usr/bin/logsquirl' || fail "does not ship /usr/bin/logsquirl"
# The command line tool ships beside the application (#430).
echo "$files" | grep -qx '/usr/bin/logsquirl_grep' || fail "does not ship /usr/bin/logsquirl_grep"

# 2. Declared Qt dependency
echo "$requires" | grep -qxF "$qt_requirement" \
    || fail "does not declare '$qt_requirement'"
if echo "$requires" | grep -i 'qt6' | grep -vF "$QT_VERSION" | grep -q .; then
    fail "declares a Qt dependency without the minimum version $QT_VERSION"
fi

# 3. Install
log=$(mktemp)
case $kind in
    deb)
        apt-get update -qq
        # The installed-or-candidate version of the distribution's QtCore.
        distro_qt=$(apt-cache policy libqt6core6t64 libqt6core6 2>/dev/null \
            | sed -n 's/^ *Candidate: *\([0-9][^+~-]*\).*/\1/p' | sort -V | tail -n 1)
        try_install() { apt-get install --dry-run -y "$package"; }
        ;;
    rpm)
        distro_qt=$(dnf info --available qt6-qtbase 2>/dev/null \
            | sed -n 's/^Version *: *\([0-9.]*\).*/\1/p' | sort -V | tail -n 1)
        try_install() { dnf install -y --setopt tsflags=test "$package"; }
        ;;
esac
echo "Distribution Qt: ${distro_qt:-none}, required: $QT_VERSION"

if try_install > "$log" 2>&1; then
    cat "$log"
    if [ -z "$distro_qt" ] || ! version_ge "$distro_qt" "$QT_VERSION"; then
        fail "installed although the distribution's Qt (${distro_qt:-none}) is older than $QT_VERSION"
    fi
    echo "Installs with the distribution's Qt $distro_qt."
else
    cat "$log"
    if [ -n "$distro_qt" ] && version_ge "$distro_qt" "$QT_VERSION"; then
        fail "does not install, although the distribution has Qt $distro_qt"
    fi
    grep -qi 'qt6' "$log" || fail "does not install, and not because of its Qt dependency"
    echo "::warning::$(basename "$package") needs Qt $QT_VERSION, the distribution has ${distro_qt:-none}: the package manager refuses it as intended, users of this distribution need the AppImage (#226)."
fi
