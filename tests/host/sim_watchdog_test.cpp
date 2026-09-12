#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <poll.h>
#include <pthread.h>
#include <sys/wait.h>
#include <unistd.h>

#include "watchdog.h"

uint64_t furble_sim_scheduler_progress(void) {
  return 0;
}

void furble_sim_report_tasks(std::string &) {}

namespace {

bool altStackIsInstalled(stack_t *state) {
  if (sigaltstack(nullptr, state) != 0) {
    std::cerr << "sigaltstack query failed: " << std::strerror(errno) << '\n';
    return false;
  }
  return (state->ss_flags & SS_DISABLE) == 0 && state->ss_sp != nullptr && state->ss_size != 0;
}

bool check(bool condition, const char *message) {
  if (!condition) {
    std::cerr << message << '\n';
  }
  return condition;
}

bool checkRegisteredThreadStack(void) {
  bool installed = false;
  bool preserved = false;
  std::thread worker([&installed, &preserved]() {
    std::vector<unsigned char> suppliedStorage(SIGSTKSZ);
    stack_t supplied {};
    supplied.ss_sp = suppliedStorage.data();
    supplied.ss_size = suppliedStorage.size();
    if (sigaltstack(&supplied, nullptr) != 0) {
      return;
    }
    stack_t before {};
    if (!altStackIsInstalled(&before)) {
      return;
    }
    Furble::Sim::watchdogRegisterThread("watchdog-test-worker");
    stack_t after {};
    installed = altStackIsInstalled(&after);
    preserved = installed && after.ss_sp == before.ss_sp && after.ss_size == before.ss_size;
    Furble::Sim::watchdogUnregisterThread();
    stack_t disabled {};
    disabled.ss_flags = SS_DISABLE;
    sigaltstack(&disabled, nullptr);
  });
  worker.join();
  return check(installed, "registered worker has no alternate signal stack")
         && check(preserved, "watchdog replaced a preinstalled alternate signal stack");
}

bool checkFreshRegisteredThreadStack(void) {
  bool installed = false;
  std::thread worker([&installed]() {
    Furble::Sim::watchdogRegisterThread("watchdog-fresh-worker");
    stack_t state {};
    installed = altStackIsInstalled(&state);
    Furble::Sim::watchdogUnregisterThread();
  });
  worker.join();
  return check(installed, "fresh registered worker has no alternate signal stack");
}

bool checkFatalChild(void) {
  int pipeEnds[2];
  if (!check(pipe(pipeEnds) == 0, "pipe failed")) {
    return false;
  }
  const pid_t child = fork();
  if (!check(child >= 0, "fork failed")) {
    close(pipeEnds[0]);
    close(pipeEnds[1]);
    return false;
  }
  if (child == 0) {
    close(pipeEnds[0]);
    if (dup2(pipeEnds[1], STDERR_FILENO) < 0) {
      _exit(98);
    }
    close(pipeEnds[1]);
    Furble::Sim::watchdogInstallCrashHandler();
    Furble::Sim::watchdogPhase("watchdog-test-phase");
    Furble::Sim::watchdogScenarioStep("watchdog-test-step");
    raise(SIGSEGV);
    _exit(99);
  }

  close(pipeEnds[1]);
  std::string output;
  char buffer[512];
  bool timedOut = false;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    if (remaining.count() <= 0) {
      timedOut = true;
      break;
    }
    pollfd pollState {pipeEnds[0], POLLIN | POLLHUP, 0};
    const int ready = poll(&pollState, 1, static_cast<int>(remaining.count()));
    if (ready == 0) {
      timedOut = true;
      break;
    }
    if (ready < 0 && errno == EINTR) {
      continue;
    }
    if (ready < 0) {
      timedOut = true;
      break;
    }
    const ssize_t count = read(pipeEnds[0], buffer, sizeof(buffer));
    if (count > 0) {
      output.append(buffer, static_cast<size_t>(count));
      continue;
    }
    if (count < 0 && errno == EINTR) {
      continue;
    }
    break;
  }
  close(pipeEnds[0]);
  if (timedOut) {
    kill(child, SIGKILL);
  }

  int status = 0;
  if (!check(waitpid(child, &status, 0) == child, "waitpid failed")) {
    return false;
  }
  return check(!timedOut, "fatal child did not finish its diagnostic")
         && check(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
                  "fatal child did not re-raise SIGSEGV")
         && check(output.find("SIM CRASH: SIGSEGV") != std::string::npos,
                  "fatal child omitted signal banner")
         && check(output.find("SIM CRASH: phase: watchdog-test-phase") != std::string::npos,
                  "fatal child omitted phase")
         && check(output.find("SIM CRASH: scenario step: watchdog-test-step") != std::string::npos,
                  "fatal child omitted scenario step");
}

}  // namespace

int main(void) {
  Furble::Sim::watchdogInstallCrashHandler();
  stack_t first {};
  if (!altStackIsInstalled(&first)) {
    return 1;
  }
  Furble::Sim::watchdogInstallCrashHandler();
  stack_t second {};
  if (!check(altStackIsInstalled(&second), "caller has no alternate signal stack")
      || !check(first.ss_sp == second.ss_sp && first.ss_size == second.ss_size,
                "repeated install changed the caller alternate stack")
      || !checkFreshRegisteredThreadStack() || !checkRegisteredThreadStack()
      || !checkFatalChild()) {
    return 1;
  }
  return 0;
}
