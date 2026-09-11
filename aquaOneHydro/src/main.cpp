#include <Arduino.h>

#include "app/HydroSenseApp.h"

HydroSenseApp app;

void setup()
{
    Serial.begin(115200);
    delay(500);

    app.begin();
}

void loop()
{
    app.update();
}