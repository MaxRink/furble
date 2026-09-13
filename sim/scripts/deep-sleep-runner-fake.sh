#!/bin/sh

set -eu

script=${2##*/}
if [ "$script" != "intervalometer-deep-sleep-start.txt" ]; then
  exit 0
fi

case "${FURBLE_SIM_DEEP_SLEEP_FAKE_MODE:-normal}" in
  normal)
    printf '%s\n' "resume_and_timed_wake_persisted" \
      > "${FURBLE_SIM_DEEP_SLEEP_EVIDENCE:?}"
    ;;
  near_deadline)
    sleep 2
    printf '%s\n' "resume_and_timed_wake_persisted" \
      > "${FURBLE_SIM_DEEP_SLEEP_EVIDENCE:?}"
    ;;
  arbitrary_evidence)
    printf '%s\n' "not-the-persistence-token" \
      > "${FURBLE_SIM_DEEP_SLEEP_EVIDENCE:?}"
    ;;
  hung)
    sleep 60
    ;;
  term_trap_zero)
    trap 'exit 0' TERM
    sleep 60
    ;;
  nonzero)
    exit 7
    ;;
  *)
    echo "unknown fake mode" >&2
    exit 2
    ;;
esac
