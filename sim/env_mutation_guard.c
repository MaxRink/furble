#define _GNU_SOURCE

#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef int (*setenv_fn)(const char *, const char *, int);
typedef int (*unsetenv_fn)(const char *);

static setenv_fn real_setenv;
static unsetenv_fn real_unsetenv;
static _Thread_local int resolving;
static const char *guard_target;

static void resolve_symbols(void) {
  if (real_setenv != NULL && real_unsetenv != NULL) {
    return;
  }
  if (resolving) {
    static const char message[] = "sim env guard: dlsym recursion\n";
    write(STDERR_FILENO, message, sizeof(message) - 1);
    _exit(127);
  }
  resolving = 1;
  dlerror();
  real_setenv = (setenv_fn)dlsym(RTLD_NEXT, "setenv");
  real_unsetenv = (unsetenv_fn)dlsym(RTLD_NEXT, "unsetenv");
  const char *error = dlerror();
  resolving = 0;
  if (error != NULL || real_setenv == NULL || real_unsetenv == NULL) {
    fprintf(stderr, "sim env guard: cannot resolve libc environment API: %s\n",
            error == NULL ? "missing symbol" : error);
    _exit(127);
  }
}

// "growth" rejects any mutation that can reallocate glibc's environ array while
// SDL is live: adding a variable, or removing one. Replacing an existing value
// only swaps one pointer in place, so it stays allowed. The SDL main loop calls
// getenv thousands of times a second without a lock, and a growing setenv
// frees the array that walk is reading (issue 283).
static int growth_mode(void) {
  return guard_target != NULL && strcmp(guard_target, "growth") == 0;
}

static void reject_after_sdl(const char *operation, const char *name) {
  if (guard_target == NULL || name == NULL) {
    return;
  }
  if (growth_mode()) {
    if (strcmp(operation, "setenv") == 0 && getenv(name) != NULL) {
      return;
    }
  } else if (strcmp(name, guard_target) != 0) {
    return;
  }
  if (SDL_WasInit(0) == 0) {
    return;
  }
  fprintf(stderr, "sim env guard: %s(%s) after SDL initialization\n", operation, name);
  _exit(86);
}

__attribute__((constructor)) static void initialize_guard(void) {
  guard_target = getenv("FURBLE_SIM_ENV_GUARD_TARGET");
  if (guard_target == NULL
      || (strcmp(guard_target, "FURBLE_SIM_PREFS") != 0
          && strcmp(guard_target, "FURBLE_SIM_RESTART_STEP") != 0 && !growth_mode())) {
    fprintf(stderr, "sim env guard: invalid FURBLE_SIM_ENV_GUARD_TARGET\n");
    _exit(127);
  }
  resolve_symbols();
  fprintf(stderr, "sim env guard: armed %s\n", guard_target);
}

int setenv(const char *name, const char *value, int overwrite) {
  resolve_symbols();
  reject_after_sdl("setenv", name);
  return real_setenv(name, value, overwrite);
}

int unsetenv(const char *name) {
  resolve_symbols();
  reject_after_sdl("unsetenv", name);
  return real_unsetenv(name);
}
