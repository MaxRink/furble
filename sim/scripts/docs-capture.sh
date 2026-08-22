#!/bin/sh

# Regenerate the docs/img walkthrough screenshots across the boards x themes x
# pages matrix. This rebuilds the simulator once per panel class (without the
# rig build so the shipped header title renders), then drives the capture
# scenarios for each theme with every optional feature enabled.
#
# Output layout:
#   docs/img/<page>.png                  default theme, StickS3, primary detail set
#   docs/img/dark-<page>.png             dark theme, StickS3, primary detail set
#   docs/img/<board>/<theme>/<page>.png  representative gallery, every cell
#   docs/img/<board>/boot-splash.png     boot splash, one per board (theme neutral)
#
# Env overrides:
#   FURBLE_LVGL_DIR / FURBLE_DEP_ROOT  passed through to sim/build.sh
#   FURBLE_SIM_BOARDS      space list of boards to build (default: s3 stickc core)
#   FURBLE_SIM_BUILD_ROOT  where per-board build trees live (default sim/build-docs)
#   SDL_VIDEODRIVER        pixel readback needs a real surface; on Linux wrap the
#                          whole script in xvfb-run with SDL_VIDEODRIVER=x11.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
IMG="$ROOT/docs/img"
BUILD_ROOT=${FURBLE_SIM_BUILD_ROOT:-"$ROOT/sim/build-docs"}
BOARDS=${FURBLE_SIM_BOARDS:-"s3 stickc core"}

# The three shipped themes, as "slug<TAB>name". The name is the exact
# Settings::THEME string; the slug names the output subdirectory.
THEMES="default	Default
dark	Dark
mono	Mono Furble"

: "${SDL_AUDIODRIVER:=dummy}"
export SDL_AUDIODRIVER

# Board -> (furble board macro, M5GFX board, non-touch layout). The narrow Stick
# boards have no touch panel, so the Remote page must render the non-touch
# floating-indicator layout; the Core is a touch board and keeps the touch grid.
board_furble() {
  case "$1" in
    s3) echo FURBLE_M5STICKS3 ;;
    stickc) echo FURBLE_M5STICKC ;;
    core) echo FURBLE_M5COREX ;;
  esac
}
board_m5gfx() {
  case "$1" in
    s3) echo board_M5StickS3 ;;
    stickc) echo board_M5StickC ;;
    core) echo board_M5Stack ;;
  esac
}
board_notouch() {
  case "$1" in
    core) echo 0 ;;
    *) echo 1 ;;
  esac
}

build_board() {
  board=$1
  echo "=== building sim for $board ==="
  FURBLE_SIM_RIG=0 \
  FURBLE_SIM_BUILD_DIR="$BUILD_ROOT/$board" \
  FURBLE_SIM_FURBLE_BOARD="$(board_furble "$board")" \
  FURBLE_SIM_M5GFX_BOARD="$(board_m5gfx "$board")" \
    sh "$ROOT/sim/build.sh"
}

# Run one capture scenario with the optional-feature env applied. Args:
#   $1 board  $2 script  $3 out-dir  $4 theme-name  [$5 splash-png]
run_capture() {
  board=$1
  script=$2
  outdir=$3
  theme=$4
  splash=${5:-}
  mkdir -p "$outdir"
  env \
    FURBLE_SIM_THEME="$theme" \
    FURBLE_SIM_IR=1 \
    FURBLE_SIM_FEEDBACK=1 \
    FURBLE_SIM_SD=1 \
    FURBLE_SIM_NO_TOUCH="$(board_notouch "$board")" \
    ${splash:+FURBLE_SIM_CAPTURE_SPLASH="$splash"} \
    "$BUILD_ROOT/$board/furble-sim" --script "$ROOT/sim/scripts/$script" --out "$outdir"
}

for board in $BOARDS; do
  build_board "$board"

  # Boot splash is drawn straight to the panel and does not depend on the theme,
  # so capture it once per board via the splash env hook. smoke.txt just needs to
  # reach exit; its own captures land in a scratch dir we discard.
  run_capture "$board" smoke.txt "$BUILD_ROOT/$board/scratch" Default \
    "$IMG/$board/boot-splash.png"

  # Per-theme representative gallery for this board.
  printf '%s\n' "$THEMES" | while IFS='	' read -r slug name; do
    [ -n "$slug" ] || continue
    run_capture "$board" docs-gallery.txt "$IMG/$board/$slug" "$name"
  done
done

# Primary detailed set: StickS3, default and dark themes, flat into docs/img so
# the per-section walkthrough embeds resolve.
run_capture s3 docs-screenshots.txt "$IMG" Default "$IMG/boot-splash.png"
run_capture s3 docs-screenshots-dark.txt "$IMG" Dark

# Text size gallery: StickS3, default theme, each Text size option. run_capture
# inherits FURBLE_SIM_TEXTSIZE from the environment (env adds to it, it does not
# clear it), so the same representative pages render at each size under
# docs/img/textsize/<size>/. The s3 build already exists from the loop above.
for size in small normal large; do
  FURBLE_SIM_TEXTSIZE="$size" \
    run_capture s3 docs-textsize.txt "$IMG/textsize/$size" Default
done

echo "docs screenshots regenerated under $IMG"
