#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "BtDebugJournal.h"

using Furble::BtDebugEvent;
using Furble::BtDebugEventKind;
using Furble::BtDebugJournal;

namespace {
void collect(const BtDebugEvent &event, void *context) {
  static_cast<std::vector<BtDebugEvent> *>(context)->push_back(event);
}

bool check(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAIL: %s\n", message);
  }
  return condition;
}
}  // namespace

int main() {
  BtDebugJournal &journal = BtDebugJournal::instance();
  journal.clear();
  journal.setEnabled(true);

  BtDebugEvent connect;
  connect.timestamp_ms = 100;
  connect.kind = BtDebugEventKind::GAP_CONNECT;
  connect.success = true;
  connect.address_type = 3;
  connect.identity_type = 0;
  connect.generation = 9;
  journal.record(connect);

  BtDebugEvent subscribe;
  subscribe.timestamp_ms = 101;
  subscribe.kind = BtDebugEventKind::GATT;
  subscribe.success = true;
  subscribe.response = true;
  subscribe.payload_length = 2;
  subscribe.payload[0] = 1;
  subscribe.payload[1] = 0;
  journal.record(subscribe);

  std::vector<BtDebugEvent> events;
  check(journal.dump(0, collect, &events) == 2, "dump returns actual typed event count");
  check(events.size() == 2 && events[0].kind == BtDebugEventKind::GAP_CONNECT
            && events[1].kind == BtDebugEventKind::GATT,
        "dump preserves event types and order");
  check(events[0].address_type == 3 && events[0].identity_type == 0,
        "address and identity types survive the event boundary");
  check(events[1].payload_length == 2 && events[1].payload[0] == 1 && events[1].response,
        "CCCD value and response mode survive the event boundary");

  events.clear();
  check(journal.drain(1, collect, &events) == 1 && events.size() == 1
            && events[0].kind == BtDebugEventKind::GAP_CONNECT,
        "live drain is explicitly bounded");
  events.clear();
  check(journal.drain(1, collect, &events) == 1 && events[0].kind == BtDebugEventKind::GATT,
        "live drain advances without flooding");

  journal.clear();
  for (size_t index = 0; index < BtDebugJournal::MAX_EVENTS + 5; ++index) {
    BtDebugEvent event;
    event.timestamp_ms = index;
    event.kind = BtDebugEventKind::SCAN;
    event.generation = index;
    journal.record(event);
  }
  events.clear();
  check(journal.dump(0, collect, &events) == BtDebugJournal::MAX_EVENTS,
        "journal has a hard bounded capacity");
  check(events.front().timestamp_ms == 5 && events.back().timestamp_ms == BtDebugJournal::MAX_EVENTS + 4,
        "ring keeps only the newest bounded events");
  check(Furble::btGapReasonName(0x13) == std::string("remote-user-terminated"),
        "GAP reason has a human meaning");
  journal.setEnabled(false);
  return 0;
}
