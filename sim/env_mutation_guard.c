#define _GNU_SOURCE

#include <dlfcn.h>
#include <SDL2/SDL.h>
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

static void reject_after_sdl(const char *operation, const char *name) {
  if (SDL_WasInit(0) == 0) {
    return;
  }
  fprintf(stderr, "sim env guard: %s(%s) after SDL initialization\n", operation,
          name == NULL ? "<null>" : name);
  _exit(86);
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
