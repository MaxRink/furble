#!/bin/sh
#
# ci-apt-install.sh - install Ubuntu archive packages on a CI runner whose
# image may carry a broken third-party apt repository.
#
# The failure this exists for
# ---------------------------
# Run 45916366927, job "Simulator power regression" (PR 540), died in the step
# that installs clang and libsdl2-dev:
#
#   Err:2 https://packages.microsoft.com/ubuntu/26.04/prod resolute InRelease
#     403  Forbidden [IP: 13.107.246.40 443]
#   E: Failed to fetch .../dists/resolute/InRelease  403  Forbidden
#   E: The repository packages.microsoft.com/ubuntu/26.04/prod resolute
#      InRelease is no longer signed.
#   ##[error]Process completed with exit code 100
#
# Nothing in the job uses that repository. apt-get update fails the whole step
# when any configured source cannot be fetched, so an unrelated repository
# killed the job before a single test ran.
#
# Why the fix is not `apt-get update || true`
# -------------------------------------------
# Suppressing the exit status would also hide a genuinely broken Ubuntu
# archive. The install that follows would then resolve against a stale package
# index, which can quietly keep an older compiler than the one the job names,
# or fail much later with a message that points at the wrong thing. This script
# removes the sources the job does not need and then runs update, install and a
# per-package verification with no failure suppression anywhere.
#
# How
# ---
# 1. Every enabled source entry is read. One-line files have the entries that
#    point away from the archive commented out, so an archive entry sharing the
#    file survives. A deb822 .sources file is moved out of the apt
#    configuration, because its stanzas cannot be dropped entry by entry. Both
#    land in a temporary work directory that is removed when the script exits,
#    and the log names every entry and file that was dropped.
# 2. The script fails if that leaves no Ubuntu archive source. A package index
#    with no archive cannot satisfy the request, and carrying on would turn a
#    real breakage into "unable to locate package" later.
# 3. apt-get update runs with no failure suppression.
# 4. apt-get install runs with no failure suppression, then dpkg-query confirms
#    that every named package reports "install ok installed".
#
# What counts as an Ubuntu archive source
# ---------------------------------------
# A source is archive when every URI in it has a host under ubuntu.com
# (archive., security., ports., azure.archive., ...). Everything else is
# third-party for this purpose, including Canonical-hosted PPAs on
# ppa.launchpadcontent.net, packages.microsoft.com, dl.google.com,
# apt.postgresql.org and cli.github.com. The workflows that call this script
# install only archive packages, so a third-party source can only hurt them.
#
# The archive allowlist can be widened, never bypassed, with
# CI_APT_ARCHIVE_HOSTS (space separated). Widening it re-enables the fetch that
# is allowed to fail the step, which is the honest direction.
#
# Usage:
#   sh tools/ci-apt-install.sh clang libsdl2-dev
#
# Exit status
#   A step fails when:
#     - no archive source is left after the third-party ones are disabled,
#     - apt-get update fails for a source that is still enabled (a real archive
#       outage),
#     - apt-get install fails for any named package,
#     - a named package does not report "install ok installed" afterwards.
#
# Environment
#   CI_APT_ARCHIVE_HOSTS  extra hosts to treat as Ubuntu archive (additive)
#   CI_APT_ROOT           operate on a fake root instead of / (tests only)
#
# The only caller-visible promise is that success means every named package is
# installed from a source that was fetched, or failed loudly trying.

set -eu

PROG=ci-apt-install

die() {
  printf '%s: FAIL: %s\n' "$PROG" "$*" >&2
  exit 1
}

# Diagnostics go to stderr, all of them, so a failing run reads in the order it
# happened. The first real run mixed stdout and stderr and printed the failure
# above the line that explained it.
info() {
  printf '%s: %s\n' "$PROG" "$*" >&2
}

warn() {
  printf '%s: warning: %s\n' "$PROG" "$*" >&2
}

[ "$#" -gt 0 ] || die "no packages named (usage: sh tools/ci-apt-install.sh <package> ...)"

for _argument in "$@"; do
  case $_argument in
    -*) die "unexpected option: $_argument" ;;
    *[!A-Za-z0-9+.:-]*) die "not a plain package name: $_argument" ;;
  esac
done

command -v apt-get >/dev/null 2>&1 || die "apt-get is not installed"
command -v dpkg-query >/dev/null 2>&1 || die "dpkg-query is not installed"
command -v awk >/dev/null 2>&1 || die "awk is not installed"
command -v mktemp >/dev/null 2>&1 || die "mktemp is not installed"

# apt itself refuses to run unprivileged, so sudo is only needed for the apt
# and file commands below. A runner image is passwordless sudo; a container
# running as root needs none.
if [ "$(id -u)" -eq 0 ]; then
  SUDO=
else
  command -v sudo >/dev/null 2>&1 || die "not root and sudo is not installed"
  SUDO=sudo
fi

ROOT=${CI_APT_ROOT:-}
ROOT=${ROOT%/}
APT_ETC=$ROOT/etc/apt
SOURCES_LIST=$APT_ETC/sources.list
SOURCES_PARTS=$APT_ETC/sources.list.d

# Extra archive hosts are additive. An empty value means the ubuntu.com rule
# alone decides, which is the behaviour every GitHub runner image needs.
EXTRA_ARCHIVE_HOSTS=${CI_APT_ARCHIVE_HOSTS:-}

WORK=$(mktemp -d "${TMPDIR:-/tmp}/ci-apt-install.XXXXXX") || die "cannot create a work directory"
trap 'rm -rf "$WORK"' EXIT INT TERM

# The allowlist lives in one place. Everything else, in shell and in awk, asks
# this function, so the rule cannot drift between the two.
is_archive_host() {
  [ -n "$1" ] || return 1
  awk -v host="$1" -v extra="$EXTRA_ARCHIVE_HOSTS" 'BEGIN {
    if (host == "ubuntu.com" || host ~ /\.ubuntu\.com$/) exit 0
    count = split(extra, allowed, " ")
    for (i = 1; i <= count; i++) {
      if (host == allowed[i]) exit 0
    }
    exit 1
  }'
}

# `mirror+file:PATH` names a file whose lines are the real mirror URIs. Ubuntu
# cloud images use it instead of a hostname, so ubuntu.sources carries
# `URIs: mirror+file:/etc/apt/apt-mirrors.txt` and the host a naive reader sees
# is the literal string "mirror+file". Print the URIs an entry really fetches
# from, or nothing at all when the indirection cannot be read.
expand_uri() {
  entry=$1
  case $entry in
    mirror+file:*)
      list=${entry#mirror+file:}
      list=${list#//}
      case $list in
        /*) list=$ROOT$list ;;
      esac
      [ -f "$list" ] || return 0
      while IFS= read -r line; do
        line=${line%%#*}
        case $line in
          *://*) printf '%s\n' "${line%:[0-9]*}" ;;
        esac
      done <"$list"
      ;;
    mirror+*)
      # A remote mirror list. Fetching it is the failure mode this script
      # exists to avoid, and guessing the host would drop a source the job
      # may need.
      return 0
      ;;
    *)
      printf '%s\n' "$entry"
      ;;
  esac
}

uri_host() {
  host=$1
  case $host in
    *://*) host=${host#*://} ;;
  esac
  host=${host%%/*}
  host=${host%%\?*}
  host=${host%%:*}
  printf '%s\n' "$host"
}

# Print the URI of every enabled entry in a source file, one per line. A
# commented-out entry is not enabled, so it never decides the outcome.
file_uris() {
  file=$1
  [ -f "$file" ] || return 0
  case $file in
    *.sources)
      # deb822. One URI list per stanza, possibly with several URIs.
      awk '
        /^[[:space:]]*[Uu][Rr][Ii][Ss]:/ {
          sub(/^[^:]*:[[:space:]]*/, "")
          sub(/[[:space:]]*#.*/, "")
          print
        }' "$file"
      ;;
    *)
      # One-line format. The optional [options] block may hold several words.
      awk '
        /^[[:space:]]*#/ { next }
        $1 == "deb" || $1 == "deb-src" {
          index_ = 2
          if (substr($index_, 1, 1) == "[") {
            while (index_ <= NF && substr($index_, length($index_), 1) != "]") {
              index_++
            }
            index_++
          }
          if (index_ <= NF) print $index_
        }' "$file"
      ;;
  esac
}

# Comment out the entries in a one-line source file that point away from the
# Ubuntu archive, keeping the archive entries that share the file.
filter_one_line_file() {
  awk -v extra="$EXTRA_ARCHIVE_HOSTS" '
    function is_archive(host,   count, allowed, i) {
      if (host == "" ) return 0
      if (host == "ubuntu.com" || host ~ /\.ubuntu\.com$/) return 1
      count = split(extra, allowed, " ")
      for (i = 1; i <= count; i++) {
        if (host == allowed[i]) return 1
      }
      return 0
    }
    function host_of(uri) {
      sub(/^[^:]+:\/\//, "", uri)
      sub(/\/.*/, "", uri)
      sub(/:.*/, "", uri)
      return uri
    }
    /^[[:space:]]*#/ { print; next }
    {
      uri = ""
      if ($1 == "deb" || $1 == "deb-src") {
        index_ = 2
        if (substr($index_, 1, 1) == "[") {
          while (index_ <= NF && substr($index_, length($index_), 1) != "]") {
            index_++
          }
          index_++
        }
        if (index_ <= NF) uri = $index_
      }
      if (uri != "" && !is_archive(host_of(uri))) {
        print "#" $0
        printf "ci-apt-install: dropped third-party source %s in %s\n", \
          host_of(uri), FILENAME > "/dev/stderr"
        next
      }
      print
    }' "$1"
}

mkdir -p "$WORK/disabled"

kept_archive=0
dropped=0

# Collect the source files first. Reading them from a list keeps the loop in
# this shell, so the counters below survive it.
: >"$WORK/files"
if [ -f "$SOURCES_LIST" ]; then
  printf '%s\n' "$SOURCES_LIST" >>"$WORK/files"
fi
if [ -d "$SOURCES_PARTS" ]; then
  for candidate in "$SOURCES_PARTS"/*.list "$SOURCES_PARTS"/*.sources; do
    if [ -f "$candidate" ]; then
      printf '%s\n' "$candidate" >>"$WORK/files"
    fi
  done
fi

while IFS= read -r source_file; do
  uris=$(file_uris "$source_file")
  [ -n "$uris" ] || continue

  outside=""
  archive_entries=0
  opaque=0
  for uri in $uris; do
    resolved=$(expand_uri "$uri")
    if [ -z "$resolved" ]; then
      opaque=1
      continue
    fi
    for one in $resolved; do
      host=$(uri_host "$one")
      if is_archive_host "$host"; then
        archive_entries=$((archive_entries + 1))
      else
        outside="$outside $host"
      fi
    done
  done

  if [ "$opaque" -eq 1 ]; then
    # A source this script cannot see through is left exactly as it is. If it
    # is broken, apt-get update names it and the step fails honestly, which is
    # better than disabling something the job may need.
    warn "cannot resolve the mirror list in $(basename "$source_file"); leaving it enabled"
    kept_archive=1
    continue
  fi

  if [ -z "$outside" ]; then
    kept_archive=1
    continue
  fi

  case $source_file in
    *.sources)
      # deb822 describes a repository, and a stanza cannot be dropped entry by
      # entry without rewriting the file structure. Move the file out of the
      # apt configuration instead, so apt sees a clean directory. If it also
      # listed the archive then the allowlist is wrong for this image, and the
      # archive check below names that rather than installing from nowhere.
      $SUDO mv "$source_file" "$WORK/disabled/$(basename "$source_file")"
      info "disabled $(basename "$source_file"): third-party host(s)$outside"
      dropped=$((dropped + 1))
      ;;
    *)
      # One-line format, so the offending entries can be commented out and any
      # archive entry that shares the file survives.
      filter_one_line_file "$source_file" >"$WORK/rewritten"
      # Never install an empty rewrite: a broken filter must leave the file
      # alone and let the update fail honestly, not blank the source list.
      if [ -s "$WORK/rewritten" ] && ! cmp -s "$source_file" "$WORK/rewritten"; then
        $SUDO cp -p "$source_file" "$WORK/disabled/$(basename "$source_file")"
        $SUDO cp "$WORK/rewritten" "$source_file"
        dropped=$((dropped + 1))
        if [ "$archive_entries" -gt 0 ]; then
          kept_archive=1
        fi
      fi
      ;;
  esac
done <"$WORK/files"

[ "$kept_archive" -eq 1 ] ||
  die "no Ubuntu archive source is enabled under $APT_ETC; refusing to install against an empty package index (set CI_APT_ARCHIVE_HOSTS if the archive moved)"

info "kept the Ubuntu archive, disabled $dropped unrelated source(s)"

# No `|| true`, no `--fix-missing`, no `-o Acquire::AllowInsecureRepositories`.
# A failure here is a failure of a source this job needs, and it must fail the
# step. Acquire::Retries rides out one bad minute on the archive itself.
info "apt-get update"
$SUDO apt-get update -o Acquire::Retries=3 ||
  die "apt-get update failed for a source this job needs"

info "apt-get install --no-install-recommends -y $*"
$SUDO apt-get install --no-install-recommends -y "$@" ||
  die "apt-get install failed"

# apt-get install can exit 0 without the package being configured, for example
# when a dependency is already unpacked but broken. The install is only proven
# by asking dpkg, so the step fails here rather than at the first compile.
for name in "$@"; do
  if ! status=$(dpkg-query -W -f='${Status}' "$name" 2>/dev/null); then
    status=""
  fi
  case $status in
    "install ok installed") ;;
    *) die "$name is not installed after apt-get (dpkg status: ${status:-absent})" ;;
  esac
done

info "installed and verified: $*"
