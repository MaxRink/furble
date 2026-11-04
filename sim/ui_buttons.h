#ifndef FURBLE_SIM_UI_BUTTONS_H
#define FURBLE_SIM_UI_BUTTONS_H

namespace Furble::Sim {

/**
 * Create the optional on-screen button window.
 *
 * Call from the SDL main thread after the panel window exists. The window is
 * interactive-only: scripted and fuzz runs, a headless SDL video driver, and
 * FURBLE_SIM_BUTTONS=0 all leave it disabled. Returns true when the window is
 * created and receiving input.
 */
bool uiButtonsInit(void);

/**
 * Service the on-screen button window: lazily initialize it, redraw after a
 * press/release/hover change, and destroy it after the user closes it.
 *
 * Call from the SDL main thread once per panel loop iteration.
 */
void uiButtonsTick(void);

/**
 * Release any held virtual button and destroy the window. Safe to call when
 * the window was never created or has already been closed.
 */
void uiButtonsShutdown(void);

/**
 * True while the control window exists and can receive clicks. The UI task
 * reads this to keep the simulated panel awake; it is safe to call from
 * another thread.
 */
bool uiButtonsActive(void);

}  // namespace Furble::Sim

#endif
