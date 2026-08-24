#!/bin/sh

set -eu
: "${FURBLE_COMPILE_LOG:?set FURBLE_COMPILE_LOG to a log file}"

invocation=${0##*/}
case "$invocation" in
  *-cc) default_compiler=clang ;;
  *) default_compiler=clang++ ;;
esac
FURBLE_REAL_COMPILER=${FURBLE_REAL_COMPILER:-$default_compiler}

previous=
for argument in "$@"; do
  if [ "$previous" = "-c" ]; then
    printf '%s\n' "$argument" >>"$FURBLE_COMPILE_LOG"
    break
  fi
  previous=$argument
done

exec "$FURBLE_REAL_COMPILER" "$@"
