#ifndef FURBLE_BOUNDARY_WEBUI_SHIM_H
#define FURBLE_BOUNDARY_WEBUI_SHIM_H

namespace Furble {

// Minimal boundary double: CompanionGatt only needs the credential-gate reload
// entry point when FURBLE_WEBUI is compiled in. The companion transport test
// counts these calls to prove a GATT password write reaches the WebUI gate
// without calling reloadSetting() directly.
class WebUI {
 public:
  static WebUI &getInstance(void);
  void reloadSetting(void);
};

}  // namespace Furble

#endif
