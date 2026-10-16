#include "link.h"
#include "mdee_robot.h"

extern "C" void app_main(void) {
    MdeeRobot::GetInstance().Init();   // motors, servos, sensors, lights, autonomy, safety
    LinkStart();                       // commands from / status to the N16R8 brain
}
