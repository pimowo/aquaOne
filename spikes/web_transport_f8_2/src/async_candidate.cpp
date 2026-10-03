#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

namespace {

constexpr char ACCESS_POINT_NAME[] = "aquaone-f8-spike";
constexpr char SNAPSHOT[] = "{\"revision\":1,\"value\":42}";
constexpr char EVENT[] = "{\"type\":\"spike.changed\",\"revision\":1}";

AsyncWebServer server(80);
AsyncWebSocket websocket("/ws/spike");

void printHeap(const char* stage) {
    Serial.printf(
        "heap stage=%s free=%u min=%u largest=%u ws_clients=%u\n",
        stage,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
        websocket.count()
    );
}

void onWebSocketEvent(
    AsyncWebSocket*,
    AsyncWebSocketClient* client,
    AwsEventType type,
    void* argument,
    uint8_t*,
    size_t
) {
    switch (type) {
        case WS_EVT_CONNECT:
            client->text(EVENT);
            printHeap("ws_connect");
            break;
        case WS_EVT_DISCONNECT:
            printHeap("ws_disconnect");
            break;
        case WS_EVT_DATA: {
            const AwsFrameInfo* frame = static_cast<const AwsFrameInfo*>(argument);
            if (
                frame != nullptr &&
                frame->opcode == WS_TEXT &&
                frame->final &&
                frame->index == 0U &&
                frame->len <= 256U
            ) {
                websocket.textAll(EVENT);
            }
            break;
        }
        case WS_EVT_PONG:
        case WS_EVT_ERROR:
            break;
    }
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    printHeap("before_network");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ACCESS_POINT_NAME);
    printHeap("before_server");

    websocket.onEvent(onWebSocketEvent);
    server.addHandler(&websocket);
    server.on(
        "/api/spike/snapshot",
        HTTP_GET,
        [](AsyncWebServerRequest* request) {
            request->send(200, "application/json", SNAPSHOT);
        }
    );
    server.onNotFound([](AsyncWebServerRequest* request) {
        request->send(404, "text/plain", "Not Found");
    });
    server.begin();

    printHeap("after_server");
    Serial.printf("AP %s IP %s\n", ACCESS_POINT_NAME, WiFi.softAPIP().toString().c_str());
}

void loop() {
    websocket.cleanupClients();
    delay(10);
}
