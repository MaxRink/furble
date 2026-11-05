#ifndef FURBLE_BOUNDARY_LVGL_H
#define FURBLE_BOUNDARY_LVGL_H

#include <cstdint>

struct _lv_timer_t;
using lv_timer_t = _lv_timer_t;

using lv_timer_cb_t = void (*)(lv_timer_t *);

lv_timer_t *lv_timer_create(lv_timer_cb_t callback, uint32_t period_ms, void *user_data);
void lv_timer_resume(lv_timer_t *timer);
void lv_timer_pause(lv_timer_t *timer);
void *lv_timer_get_user_data(lv_timer_t *timer);

namespace BoundaryHost {
void serviceTimers(void);
uint32_t timerFireCount(void);
}  // namespace BoundaryHost

#endif
