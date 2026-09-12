#pragma once
namespace Furble {
class Control {
 public:
  enum state_t { STATE_IDLE, STATE_ACTIVE };
  static Control &getInstance();
  state_t getState() const;
};
}  // namespace Furble
