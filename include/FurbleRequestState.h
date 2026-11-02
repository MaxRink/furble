#ifndef FURBLE_REQUEST_STATE_H
#define FURBLE_REQUEST_STATE_H
#include <freertos/semphr.h>
#include <atomic>
#include <new>
namespace Furble {
struct RequestState {
  std::atomic<unsigned> refs {1};
  SemaphoreHandle_t done = xSemaphoreCreateBinary();
  const char *token = nullptr;
  void retain() { refs.fetch_add(1, std::memory_order_relaxed); }
  void release() {
    if (refs.fetch_sub(1, std::memory_order_acq_rel) == 1)
      delete this;
  }
  ~RequestState() {
    if (done != nullptr)
      vSemaphoreDelete(done);
  }
};
struct RequestResult {
  RequestState *state = nullptr;
  RequestResult() noexcept : state(new (std::nothrow) RequestState()) {
    if (state != nullptr && state->done == nullptr) {
      state->release();
      state = nullptr;
    }
  }
  ~RequestResult() {
    if (state != nullptr)
      state->release();
  }
  RequestResult(const RequestResult &) = delete;
  RequestResult &operator=(const RequestResult &) = delete;
};
}  // namespace Furble
#endif
