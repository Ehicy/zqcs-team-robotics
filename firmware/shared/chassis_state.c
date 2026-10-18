#include "chassis_state.h"
chassis_state_t chassis_state_update(uint8_t link,uint8_t pid,uint8_t calibrated,
                                    uint8_t fault,uint8_t armed,uint8_t moving)
{
    if(!link) return CHASSIS_WAIT_LINK;
    if(fault) return CHASSIS_FAULT;
    if(!pid) return CHASSIS_WAIT_PID;
    if(!calibrated) return CHASSIS_WAIT_CALIBRATION;
    if(armed) return moving?CHASSIS_RUNNING:CHASSIS_ARMED;
    return CHASSIS_LOCKED;
}
