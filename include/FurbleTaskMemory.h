#ifndef FURBLE_TASK_MEMORY_H
#define FURBLE_TASK_MEMORY_H

#include <cstddef>

namespace Furble {
namespace TaskMemory {

/**
 * Derived: what one task costs beyond its stack. The largest StaticTask_t in
 * sim/coverage/board_heap.json is 348 bytes (Waveshare DWARF; 336 elsewhere),
 * and the stack and TCB are two heap blocks of 16 bytes each (4-byte TLSF
 * header plus 12 bytes of comprehensive poisoning). 348 + 32 = 380, rounded
 * up to 384. Every task admission gate uses this one value.
 */
inline constexpr size_t TASK_OVERHEAD_BYTES = 384;

/**
 * The per-camera `Control::Target` task stack. `Control::addActive()` creates
 * it and the memory governor admits it; tools/stack_budget.json reads it here.
 */
inline constexpr size_t CAMERA_TARGET_STACK_BYTES = 4096;

}  // namespace TaskMemory
}  // namespace Furble

#endif
