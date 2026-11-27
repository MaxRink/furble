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
# 1. Every enabled source entry is read, and only the entries that point away
#    from the archive are commented out. A one-line file is filtered entry by
#    entry. A deb822 .sources file is filtered stanza by stanza, because a
#    stanza is one repository: the whole file is never removed, so an archive
#    stanza beside a third-party one keeps working. Neither path drops anything
#    it cannot classify, and the log names every host and file it dropped.
# 2. The files are then read back, and the script fails if that leaves no
#    enabled source at all. A package index with nothing enabled cannot satisfy
#    the request, and carrying on would turn a real breakage into "unable to
#    locate package" later. When the remaining sources are ones this script
#    cannot classify, it says so instead of claiming the archive is present.
# 3. apt-get update runs with no failure suppression.
# 4. apt-get install runs with no failure suppression, then dpkg-query confirms
#    that every named package reports "install ok installed".
#
# What counts as an Ubuntu archive source
# ---------------------------------------
# A source entry is archive when every URI in it has a host under ubuntu.com
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
#     - no enabled source is left after the third-party entries are disabled,
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

# WARN_QUIET suppresses warnings for a pass that only needs the verdict, such as
# the read-back below, so one problem is reported once.
warn() {
  if [ "${WARN_QUIET:-0}" -eq 1 ]; then
    return 0
  fi
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
# The allowlist, in one place. Everything that decides whether a source is the
# Ubuntu archive calls this function, including the two rewrite paths below, so
# the rule cannot drift between them. A host matches when it is ubuntu.com or
# ends in .ubuntu.com, which covers archive., security., ports. and the cloud
# mirrors such as azure.archive.ubuntu.com.
is_archive_host() {
  host=$1
  [ -n "$host" ] || return 1
  for extra in $EXTRA_ARCHIVE_HOSTS; do
    if [ "$host" = "$extra" ]; then
      return 0
    fi
  done
  case $host in
    ubuntu.com|*.ubuntu.com) return 0 ;;
  esac
  return 1
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
      if [ ! -f "$list" ]; then
        warn "mirror list $list does not exist"
        return 0
      fi
      # A line is a URI, or a preference followed by one: the Ubuntu 26.04
      # image writes `priority:1 http://azure.archive.ubuntu.com/ubuntu/`.
      # Take the first token that is a URI rather than assuming a position, so
      # the preference form does not read as a host called "priority".
      mirrors=$(awk '
        {
          sub(/#.*/, "")
          for (i = 1; i <= NF; i++) {
            if ($i !~ /:\/\//) continue
            sub(/:[0-9]+$/, "", $i)
            print $i
            break
          }
        }' "$list")
      if [ -z "$mirrors" ]; then
        warn "mirror list $list names no mirror this script can read"
        return 0
      fi
      # Name the upstreams the first time a list is read, so a run records which
      # mirrors it used without repeating the line for every stanza that points
      # at the same list.
      marker=$WORK/logged/$(printf '%s' "$list" | tr '/ ' '__')
      if [ ! -f "$marker" ]; then
        : >"$marker"
        info "mirror list $list: $(printf '%s\n' "$mirrors" | tr '\n' ' ')"
      fi
      printf '%s\n' "$mirrors"
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

# Classify one source URI.
#   0  the Ubuntu archive, so keep it
#   1  third party, so drop it
#   2  cannot be read, so leave it enabled and let apt decide
# CLASSIFY_HOSTS carries the hosts that were seen, for the log.
classify_uri() {
  entry=$1
  CLASSIFY_HOSTS=""
  resolved=$(expand_uri "$entry")
  if [ -z "$resolved" ]; then
    return 2
  fi
  verdict=0
  for one in $resolved; do
    host=$(uri_host "$one")
    CLASSIFY_HOSTS="$CLASSIFY_HOSTS $host"
    if ! is_archive_host "$host"; then
      verdict=1
    fi
  done
  return "$verdict"
}

# The first token of a source entry that is a URI. A one-line entry is
# `deb [options] URI suite component...`, so looking for the URI is steadier
# than counting fields, and it also finds the `mirror+` indirection.
entry_uri() {
  for word in $1; do
    case $word in
      *://*|mirror+*) printf '%s\n' "$word"; return 0 ;;
    esac
  done
}

# Print the URI of every enabled entry in a source file, one per line. A
# commented-out entry is not enabled, so it never decides the outcome. This is
# parsing only: whether a URI is the archive is decided by classify_uri.
file_uris() {
  file=$1
  [ -f "$file" ] || return 0
  case $file in
    *.sources)
      awk '
        /^[[:space:]]*#/ { next }
        /^[[:space:]]*[Uu][Rr][Ii][Ss]:/ {
          sub(/^[^:]*:[[:space:]]*/, "")
          sub(/[[:space:]]*#.*/, "")
          print
        }' "$file"
      ;;
    *)
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

# Rewrite a one-line source file into $WORK/rewritten, commenting out exactly
# the entries that point away from the archive, so an archive entry that shares
# the file survives. Sets RW_CHANGED when something was dropped and RW_OUTSIDE
# to the hosts that were, for the log.
rewrite_one_line_file() {
  file=$1
  name=$(basename "$file")
  : >"$WORK/rewritten"
  RW_CHANGED=0
  RW_OUTSIDE=""
  while IFS= read -r line || [ -n "$line" ]; do
    case $line in
      deb|deb\ *|deb-src|deb-src\ *)
        uri=$(entry_uri "$line")
        rc=0
        [ -z "$uri" ] || classify_uri "$uri" || rc=$?
        case $rc in
          1)
            printf '#%s\n' "$line" >>"$WORK/rewritten"
            RW_OUTSIDE="$RW_OUTSIDE$CLASSIFY_HOSTS"
            RW_CHANGED=1
            continue
            ;;
          2)
            warn "cannot resolve $uri in $name; leaving the entry enabled"
            ;;
        esac
        ;;
    esac
    printf '%s\n' "$line" >>"$WORK/rewritten"
  done <"$file"
}

# Print a buffered deb822 stanza, commented out when it points away from the
# archive and kept otherwise.
flush_stanza() {
  [ -s "$WORK/stanza" ] || return 0
  if [ "$STANZA_DROP" -eq 1 ] && [ "$STANZA_OPAQUE" -eq 0 ]; then
    sed 's/^/#/' "$WORK/stanza" >>"$WORK/rewritten"
    RW_OUTSIDE="$RW_OUTSIDE$STANZA_HOSTS"
    RW_CHANGED=1
  else
    cat "$WORK/stanza" >>"$WORK/rewritten"
  fi
  : >"$WORK/stanza"
  STANZA_DROP=0
  STANZA_OPAQUE=0
  STANZA_HOSTS=""
}

# Rewrite a deb822 .sources file into $WORK/rewritten, commenting out every
# stanza that points away from the archive and keeping the archive stanzas
# beside it. A deb822 file is a sequence of blank-line separated stanzas, each
# naming one repository, so a stanza is the smallest unit that can be dropped
# without losing anything else. Dropping the whole file instead would take
# archive stanzas with it while the step still reported success.
rewrite_deb822_file() {
  file=$1
  name=$(basename "$file")
  : >"$WORK/rewritten"
  : >"$WORK/stanza"
  RW_CHANGED=0
  RW_OUTSIDE=""
  STANZA_DROP=0
  STANZA_OPAQUE=0
  STANZA_HOSTS=""
  while IFS= read -r line || [ -n "$line" ]; do
    case $line in
      *[![:space:]]*)
        printf '%s\n' "$line" >>"$WORK/stanza"
        case $line in
          [Uu][Rr][Ii][Ss]:*)
            for uri in ${line#*:}; do
              rc=0
              classify_uri "$uri" || rc=$?
              case $rc in
                1)
                  STANZA_DROP=1
                  STANZA_HOSTS="$STANZA_HOSTS$CLASSIFY_HOSTS"
                  ;;
                2)
                  STANZA_OPAQUE=1
                  warn "cannot resolve $uri in $name; leaving the stanza enabled"
                  ;;
              esac
            done
            ;;
        esac
        ;;
      *)
        flush_stanza
        printf '\n' >>"$WORK/rewritten"
        ;;
    esac
  done <"$file"
  flush_stanza
}

mkdir -p "$WORK/logged"

kept_files=0
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
  [ -n "$(file_uris "$source_file")" ] || continue

  case $source_file in
    *.sources) rewrite_deb822_file "$source_file" ;;
    *) rewrite_one_line_file "$source_file" ;;
  esac

  # Never install an empty rewrite: a broken filter must leave the file alone
  # and let the update fail honestly, not blank the source list.
  if [ "$RW_CHANGED" -eq 1 ] && [ -s "$WORK/rewritten" ]; then
    if ! cmp -s "$source_file" "$WORK/rewritten"; then
      $SUDO cp "$WORK/rewritten" "$source_file"
      dropped=$((dropped + 1))
      info "disabled third-party source(s) in $(basename "$source_file"):$RW_OUTSIDE"
    fi
  fi

  # Read the file back after the rewrite, so what it still enables is what
  # decides whether anything is left to install from. That keeps the check
  # honest for both file formats instead of trusting the rewrite. The rewrite
  # above already reported anything unreadable, so this pass stays quiet.
  WARN_QUIET=1
  for uri in $(file_uris "$source_file"); do
    rc=0
    classify_uri "$uri" || rc=$?
    case $rc in
      0) kept_archive=1; kept_files=1 ;;
      2) kept_files=1 ;;
    esac
  done
  WARN_QUIET=0
done <"$WORK/files"

[ "$kept_files" -eq 1 ] ||
  die "no enabled apt source is left under $APT_ETC; refusing to install against an empty package index (set CI_APT_ARCHIVE_HOSTS if the archive moved)"

if [ "$kept_archive" -eq 0 ]; then
  warn "no Ubuntu archive source is identifiable, but an unreadable source is still enabled, so apt-get update decides"
fi

info "kept $kept_files source file(s), dropped $dropped third-party source file(s)"

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
