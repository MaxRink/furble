#ifndef FURBLE_FEEDBACK_H
#define FURBLE_FEEDBACK_H

namespace Furble {

class Feedback {
 public:
  static Feedback &getInstance();
  void reload(void);

 private:
  Feedback() = default;
};

}  // namespace Furble

#endif
