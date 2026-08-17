#ifndef FURBLE_SIM_DRIVER_H
#define FURBLE_SIM_DRIVER_H

namespace Furble {
class UI;
}

namespace Furble::Sim {

void configure(int argc, char **argv);
void setBackTarget(Furble::UI *ui);
void driverTick(void);

}  // namespace Furble::Sim

#endif
