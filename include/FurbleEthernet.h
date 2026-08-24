#ifndef FURBLE_ETHERNET_H
#define FURBLE_ETHERNET_H

#include <functional>
#include <string>

namespace Furble {

class Ethernet {
 public:
  /** Events emitted by the concrete Ethernet transport. */
  class Transport {
   public:
    enum class Event {
      LINK_UP,
      LINK_DOWN,
      GOT_IP,
    };

    using EventCallback = std::function<void(Event, const std::string &)>;

    virtual ~Transport() = default;

    /** Install the event callback and prepare the transport. */
    virtual bool init(EventCallback callback) = 0;

    /** Start the transport after init has completed. */
    virtual bool start(void) = 0;

    /** Stop the transport and release its resources. */
    virtual void stop(void) = 0;
  };

  using NetworkUpCallback = std::function<void(const std::string &)>;

  Ethernet() = delete;
  ~Ethernet() = delete;

  /** Initialize the Waveshare W5500 transport. */
  static bool init(void);

  /** Initialize with an injected transport, used by host tests. */
  static bool init(Transport &transport);

  /** Register the transport-agnostic network-up callback. */
  static void setNetworkUpCallback(NetworkUpCallback callback);

  /** Stop the transport and clear the network state. */
  static void stop(void);

  /** Return true after link-up has supplied a usable IP address. */
  static bool isConnected(void);

  /** Return the current IPv4 address, or an empty string when offline. */
  static std::string getIP(void);
};

using EthernetTransport = Ethernet::Transport;

}  // namespace Furble

#endif
