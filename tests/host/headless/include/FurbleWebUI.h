#ifndef FURBLE_HEADLESS_WEBUI_H
#define FURBLE_HEADLESS_WEBUI_H

namespace Furble {
class WebUI {
 public:
  static WebUI &getInstance(void);
  static void initEarly(void);
  static void init(void);
};
}  // namespace Furble

#endif
