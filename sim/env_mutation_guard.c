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
typedef int (*putenv_fn)(char *);
typedef int (*clearenv_fn)(void);

static setenv_fn real_setenv;
static unsetenv_fn real_unsetenv;
static putenv_fn real_putenv;
static clearenv_fn real_clearenv;
static _Thread_local int resolving;
static const char *guard_target;

static void resolve_symbols(void) {
  if (real_setenv != NULL && real_unsetenv != NULL && real_putenv != NULL
      && real_clearenv != NULL) {
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
  real_putenv = (putenv_fn)dlsym(RTLD_NEXT, "putenv");
  real_clearenv = (clearenv_fn)dlsym(RTLD_NEXT, "clearenv");
  const char *error = dlerror();
  resolving = 0;
  if (error != NULL || real_setenv == NULL || real_unsetenv == NULL || real_putenv == NULL
      || real_clearenv == NULL) {
    fprintf(stderr, "sim env guard: cannot resolve libc environment API: %s\n",
            error == NULL ? "missing symbol" : error);
    _exit(127);
  }
}

// "growth" rejects every structural environment change while SDL is live. The
// SDL main loop calls getenv thousands of times a second without a lock.
// Adding a variable reallocates environ and frees the array that walk is
// reading (issue 283). Removing one shifts the array in place without a reader
// lock, so a concurrent walk can miss an entry; reject it too, and clearenv
// with it. Replacing an existing value only swaps one pointer, so it stays
// allowed.
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

// putenv("NAME=value") adds or replaces like setenv; glibc treats a string
// without '=' as an unset of that name.
int putenv(char *string) {
  resolve_symbols();
  char name[256];
  const char *equals = strchr(string, '=');
  const size_t length = equals == NULL ? strlen(string) : (size_t)(equals - string);
  if (length < sizeof(name)) {
    memcpy(name, string, length);
    name[length] = '\0';
    reject_after_sdl(equals == NULL ? "unsetenv" : "setenv", name);
  } else {
    reject_after_sdl("unsetenv", string);
  }
  return real_putenv(string);
}

int clearenv(void) {
  resolve_symbols();
  if (guard_target != NULL && SDL_WasInit(0) != 0) {
    fprintf(stderr, "sim env guard: clearenv() after SDL initialization\n");
    _exit(86);
  }
  return real_clearenv();
}
