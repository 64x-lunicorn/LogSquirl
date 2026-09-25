#!/bin/sh
# Runs inside a clean fedora:44 or oraclelinux:10 container (#381): dnf itself
# reads the DNF repository of the built site, from a file: URL, as a user
# would after adding the .repo file. Every release is offered, the signed
# metadata raises no warning, the newest RPM installs, and an older release
# upgrades to it. Usage: check-dnf-repo.sh FLAVOR   (site in /site, read-only)
set -eu
flavor=$1
repo=/etc/yum.repos.d/logsquirl.repo
sed -e "s|^baseurl=.*|baseurl=file:///site/dnf/${flavor}|" \
    -e "s|^gpgkey=.*|gpgkey=file:///site/logsquirl-packages.asc|" \
    "/site/logsquirl-${flavor}.repo" > "$repo"
grep -q '^repo_gpgcheck=1$' "$repo"

fail() { echo "::error::$1"; exit 1; }

# sh has no pipefail: a `| tee` would hide a failed makecache.
status=0
dnf --assumeyes makecache >/tmp/makecache.log 2>&1 || status=$?
cat /tmp/makecache.log
[ "$status" -eq 0 ] || fail "dnf cannot read the ${flavor} repository."
if grep -Eiq 'warning|gpg check FAILED|signature' /tmp/makecache.log; then
  fail "dnf warns about the ${flavor} repository."
fi

# Every RPM of the site is offered, oldest to newest.
rpms=$(ls "/site/dnf/${flavor}"/*.rpm | sort -V)
for rpm in $rpms; do
  version=$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "$rpm")
  dnf --quiet list --showduplicates logsquirl | grep -q "${version}" \
    || fail "dnf does not offer logsquirl ${version} (${flavor})."
done

newest=$(ls "/site/dnf/${flavor}"/*.rpm | sort -V | tail -n 1)
oldest=$(ls "/site/dnf/${flavor}"/*.rpm | sort -V | head -n 1)
newest_version=$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "$newest")
oldest_version=$(rpm -qp --qf '%{VERSION}-%{RELEASE}' "$oldest")

# What dnf downloads is the release asset.
# dnf5 (Fedora) has `download` built in; dnf4 (Oracle Linux) needs the plugin.
dnf download --help >/dev/null 2>&1 || dnf --assumeyes install 'dnf-command(download)' >/dev/null
mkdir /tmp/download
(cd /tmp/download && dnf --assumeyes download "logsquirl-${newest_version}")
[ "$(sha256sum /tmp/download/logsquirl-*.rpm | cut -d' ' -f1)" = "$(sha256sum "$newest" | cut -d' ' -f1)" ] \
  || fail "dnf downloaded a ${flavor} RPM that is not the release asset."

if [ "$oldest_version" != "$newest_version" ]; then
  dnf --assumeyes install "logsquirl-${oldest_version}"
  [ "$(rpm -q --qf '%{VERSION}-%{RELEASE}' logsquirl)" = "$oldest_version" ] \
    || fail "dnf did not install logsquirl ${oldest_version}."
  dnf --assumeyes upgrade logsquirl
else
  dnf --assumeyes install logsquirl
fi
[ "$(rpm -q --qf '%{VERSION}-%{RELEASE}' logsquirl)" = "$newest_version" ] \
  || fail "logsquirl is not at ${newest_version} after the install or upgrade."
