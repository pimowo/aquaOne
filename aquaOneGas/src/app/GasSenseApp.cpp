#include "app/GasSenseApp.h"

namespace gassense {

bool GasSenseApp::begin() {
    // TODO:
    // 1. uruchomić AquaCore,
    // 2. wczytać i zwalidować config,
    // 3. zainicjalizować I2C / RTC poprzez AquaCore,
    // 4. uruchomić sterowniki GasSense,
    // 5. zarejestrować WWW i MQTT,
    // 6. wejść w fazę INITIALIZING/STABILIZING.

    weight_.begin(config_);
    pressure_.begin(config_);
    temperature_.begin(config_);

    web_.registerPages();
    mqtt_.registerEntities();

    return true;
}

void GasSenseApp::loop() {
    // TODO: aquaCore.loop();

    weight_.update();
    pressure_.update();
    temperature_.update();

    measurements_.hx711Raw = weight_.raw();
    measurements_.filteredMassKg = weight_.filteredKg();
    measurements_.totalMassKg = weight_.filteredKg();
    measurements_.weightStability = weight_.stability();
    measurements_.hx711 = weight_.health();

    measurements_.ads1115Raw = pressure_.raw();
    measurements_.pressureBar = pressure_.pressureBar();
    measurements_.ads1115 = pressure_.health();

    measurements_.bottleTempC = temperature_.temperatureC();
    measurements_.ds18b20 = temperature_.health();

    sensorHealth_.update(measurements_);
    state_ = GasCalculator::calculate(config_, measurements_);

    alarms_.update(state_, serviceMode_, config_.soundEnabled);
    web_.update();

    // TODO: publikacja MQTT wg harmonogramu / przy zmianach + snapshot po reconnect.
}

}
