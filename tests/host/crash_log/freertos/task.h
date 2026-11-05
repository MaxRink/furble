#ifndef FURBLE_HOST_CRASH_FREERTOS_TASK_H
#define FURBLE_HOST_CRASH_FREERTOS_TASK_H

inline char *pcTaskGetName(void *) {
  return const_cast<char *>("ui");
}

#endif
