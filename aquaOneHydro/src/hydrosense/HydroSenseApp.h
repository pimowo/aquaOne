#pragma once

#include "hardware/Pump.h"
#include "hardware/FloatSensor.h"
#include "hardware/UltrasonicSensor.h"

#include "hydrosense/HydroSenseConfig.h"
#include "hydrosense/WaterTank.h"
#include "hydrosense/TopupController.h"
#include "hydrosense/AlarmManager.h"

class HydroSenseApp
{
public:
    HydroSenseApp();

    void begin();
    void update();

private:
    HydroSenseConfig config_;

    Pump pump_;
    FloatSensor floatSensor_;
    UltrasonicSensor ultrasonicSensor_;

    WaterTank waterTank_;
    TopupController topupController_;
    AlarmManager alarmManager_;
};