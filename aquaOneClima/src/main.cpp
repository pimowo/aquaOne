#include <Arduino.h>

#ifndef PROJECT_NAME
#define PROJECT_NAME "aquaOneClima"
#endif

#ifndef PROJECT_DISPLAY_NAME
#define PROJECT_DISPLAY_NAME "aquaOne Clima"
#endif

void setup()
{
    Serial.begin(115200);
    delay(500);

    Serial.println();
    Serial.println("================================");
    Serial.println(PROJECT_DISPLAY_NAME);
    Serial.print("Project: ");
    Serial.println(PROJECT_NAME);
    Serial.println("Status: startup OK");
    Serial.println("================================");
}

void loop()
{
    delay(1000);
}
