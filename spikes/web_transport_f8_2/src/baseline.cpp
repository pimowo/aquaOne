#include <Arduino.h>
#include <WiFi.h>

namespace {

constexpr char ACCESS_POINT_NAME[] = "aquaone-f8-spike";

void printHeap(const char* stage) {
    Serial.printf(
        "heap stage=%s free=%u min=%u largest=%u\n",
        stage,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    );
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    printHeap("before_network");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ACCESS_POINT_NAME);
    printHeap("after_network");
    Serial.printf("AP %s IP %s\n", ACCESS_POINT_NAME, WiFi.softAPIP().toString().c_str());
}

void loop() {
    delay(1000);
}
