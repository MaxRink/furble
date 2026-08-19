#ifndef FURBLE_UI_AUDIT_H
#define FURBLE_UI_AUDIT_H

#if defined(FURBLE_SIM) || defined(FURBLE_CONSOLE)

#include <lvgl.h>

namespace Furble {
namespace UIAudit {

/** Print a JSON Lines layout report for the supplied LVGL screen. */
void dump(lv_obj_t *root);

}  // namespace UIAudit
}  // namespace Furble

#endif

#endif
