#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <SDL2/SDL.h>

#include <driver/uart.h>

#include "capture.h"
#include "clock.h"
#include "driver.h"

namespace Furble::Sim {
namespace {

enum class StepType {
  WAIT,
  KEY,
  CAPTURE,
  UART_DUMP,
  EXIT,
};

struct Step {
  StepType type;
  uint32_t milliseconds = 0;
  SDL_Keycode key = SDLK_UNKNOWN;
  std::string name;
};

std::vector<Step> steps;
std::string captureDirectory = ".pio/furble-sim-captures";
size_t stepIndex = 0;
uint32_t waitUntil = 0;
uint32_t releaseAt = 0;
SDL_Keycode pressedKey = SDLK_UNKNOWN;
bool configured = false;

SDL_Keycode keyCode(const std::string &name) {
  if (name == "up") {
    return SDLK_UP;
  }
  if (name == "down") {
    return SDLK_DOWN;
  }
  if (name == "left") {
    return SDLK_LEFT;
  }
  if (name == "right") {
    return SDLK_RIGHT;
  }
  if (name == "return" || name == "enter") {
    return SDLK_RETURN;
  }
  return SDLK_UNKNOWN;
}

void pushKey(SDL_Keycode key, bool pressed) {
  SDL_Event event {};
  event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.keysym.sym = key;
  event.key.keysym.mod = KMOD_NONE;
  SDL_PushEvent(&event);
}

void readScript(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    std::cerr << "Could not open simulator script: " << path << '\n';
    std::exit(2);
  }

  std::string line;
  while (std::getline(file, line)) {
    const size_t comment = line.find('#');
    if (comment != std::string::npos) {
      line.resize(comment);
    }
    std::istringstream input(line);
    std::string command;
    input >> command;
    if (command.empty()) {
      continue;
    }

    if (command == "wait" || command == "advance") {
      Step step;
      step.type = StepType::WAIT;
      input >> step.milliseconds;
      steps.push_back(step);
    } else if (command == "key" || command == "press") {
      std::string name;
      input >> name;
      Step step;
      step.type = StepType::KEY;
      step.key = keyCode(name);
      if (step.key == SDLK_UNKNOWN) {
        std::cerr << "Unknown simulator key: " << name << '\n';
        std::exit(2);
      }
      steps.push_back(step);
    } else if (command == "capture") {
      Step step;
      step.type = StepType::CAPTURE;
      input >> step.name;
      steps.push_back(step);
    } else if (command == "uart-dump") {
      Step step;
      step.type = StepType::UART_DUMP;
      steps.push_back(step);
    } else if (command == "exit") {
      Step step;
      step.type = StepType::EXIT;
      steps.push_back(step);
    } else {
      std::cerr << "Unknown simulator script command: " << command << '\n';
      std::exit(2);
    }
  }
}

std::string capturePath(const std::string &name) {
  if (name.size() >= 4 && name.substr(name.size() - 4) == ".png") {
    return captureDirectory + "/" + name;
  }
  return captureDirectory + "/" + name + ".png";
}

}  // namespace

void configure(int argc, char **argv) {
  if (configured) {
    return;
  }
  configured = true;

  std::string script;
  for (int i = 1; i < argc; ++i) {
    const std::string argument = argv[i];
    if (argument == "--script" && i + 1 < argc) {
      script = argv[++i];
    } else if (argument == "--capture-dir" && i + 1 < argc) {
      captureDirectory = argv[++i];
    } else if (argument == "--help") {
      std::cout << "furble-sim [--script FILE] [--capture-dir DIR]\n";
      std::exit(0);
    }
  }

  if (!script.empty()) {
    readScript(script);
  }
}

void driverTick(void) {
  if (stepIndex >= steps.size()) {
    return;
  }

  const uint32_t now = clockMillis();
  if (pressedKey != SDLK_UNKNOWN) {
    if (now < releaseAt) {
      return;
    }
    pushKey(pressedKey, false);
    pressedKey = SDLK_UNKNOWN;
    ++stepIndex;
    return;
  }

  Step &step = steps[stepIndex];
  switch (step.type) {
    case StepType::WAIT:
      if (waitUntil == 0) {
        waitUntil = now + step.milliseconds;
      } else if (now >= waitUntil) {
        waitUntil = 0;
        ++stepIndex;
      }
      break;

    case StepType::KEY:
      pushKey(step.key, true);
      pressedKey = step.key;
      releaseAt = now + 80;
      break;

    case StepType::CAPTURE:
      if (!captureFrame(capturePath(step.name))) {
        std::cerr << "Could not capture simulator frame: " << step.name << '\n';
        std::exit(1);
      }
      std::cout << "Captured " << capturePath(step.name) << '\n';
      ++stepIndex;
      break;

    case StepType::UART_DUMP:
      // Print every captured GPS UART command, assertable by the caller.
      for (const auto &payload : furble_sim_uart_writes()) {
        std::string line = payload;
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
          line.pop_back();
        }
        std::cout << "uart-tx " << line << '\n';
      }
      furble_sim_uart_clear_writes();
      ++stepIndex;
      break;

    case StepType::EXIT:
      std::exit(0);
  }
}

}  // namespace Furble::Sim
