#include <Arduino.h>
#include "app/GasSenseApp.h"

#include <AquaCore/Logging/Logger.h>
#include <AquaCore/Logging/SerialLogSink.h>

gassense::GasSenseApp app;

AquaCore::SerialLogSink serialSink(Serial);
AquaCore::Logger logger(serialSink);

void setup() {
    Serial.begin(115200);
    delay(50);

    logger.info("GasSense", "AquaCore logger ready");

    app.begin();
}

void loop() {
    app.loop();
}
