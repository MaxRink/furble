#ifndef FURBLE_WEBUI_PROTOCOL_H
#define FURBLE_WEBUI_PROTOCOL_H

#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

namespace Furble {
namespace WebUIProtocol {

/** Accept JSON with an optional media type parameter. */
inline bool isJSONContentType(std::string_view value) {
  constexpr std::string_view JSON = "application/json";
  if (value.substr(0, JSON.size()) != JSON) {
    return false;
  }
  value.remove_prefix(JSON.size());
  while (!value.empty() && ((value.front() == ' ') || (value.front() == '\t'))) {
    value.remove_prefix(1);
  }
  return value.empty() || (value.front() == ';');
}

/** Reject browser cross-origin requests. Non-browser clients omit Origin. */
inline bool sameOrigin(std::string_view host, std::string_view origin) {
  if (host.empty() || (host.find_first_of("\r\n") != std::string_view::npos)) {
    return false;
  }
  return origin.empty() || (origin == (std::string("https://") + std::string(host)));
}

/** Decode a JSON number without narrowing fractions, infinities, or overflow. */
inline bool unsignedInteger(double value, uint32_t maximum, uint32_t &result) {
  if (!std::isfinite(value) || (value < 0) || (value > maximum) || (std::floor(value) != value)) {
    return false;
  }
  result = static_cast<uint32_t>(value);
  return true;
}

/** Accept only the expiry belonging to the current, elapsed hold. */
inline bool holdExpired(int64_t deadlineUs, int64_t nowUs, bool timerActive) {
  return (deadlineUs > 0) && (nowUs >= deadlineUs) && !timerActive;
}

}  // namespace WebUIProtocol
}  // namespace Furble

#endif
