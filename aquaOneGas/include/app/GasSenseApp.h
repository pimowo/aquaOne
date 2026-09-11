#pragma once
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"
#include "../drivers/WeightDriver.h"
#include "../drivers/PressureDriver.h"
#include "../drivers/TemperatureDriver.h"
#include "../services/GasCalculator.h"
#include "../services/AlarmService.h"
#include "../services/BottleService.h"
#include "../services/SensorHealthService.h"
#include "../interfaces/GasSenseMqtt.h"
#include "../interfaces/GasSenseWeb.h"

namespace gassense {

class GasSenseApp {
public:
    bool begin();
    void loop();

private:
    GasSenseConfig config_{};
    Measurements measurements_{};
    GasState state_{};

    WeightDriver weight_;
    PressureDriver pressure_;
    TemperatureDriver temperature_;

    AlarmService alarms_;
    BottleService bottle_;
    SensorHealthService sensorHealth_;

    GasSenseMqtt mqtt_;
    GasSenseWeb web_;

    bool serviceMode_ = false;
};

} // namespace gassense
