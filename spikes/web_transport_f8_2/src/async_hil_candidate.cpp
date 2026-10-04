#include <Arduino.h>
#include <WiFi.h>

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>

#include "async_hil_credentials.h"

namespace {

constexpr size_t MAX_BODY_LENGTH = 256U;
constexpr size_t MAX_WS_MESSAGE_LENGTH = 256U;
constexpr size_t MAX_TRACKED_WS_CLIENTS = 8U;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000U;
constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 1500U;

AsyncWebServer server(80U);
AsyncWebSocket websocket("/ws/spike");

char snapshotPayload[64] = "{\"revision\":1,\"value\":42}";
char eventPayload[80] = "{\"type\":\"spike.changed\",\"revision\":1}";
char ackPayload[80] = "{\"type\":\"spike.ack\",\"revision\":1}";
uint32_t revision = 1U;
uint32_t snapshotRequests = 0U;
uint32_t wsEvents = 0U;
uint32_t lastWsClientCount = UINT32_MAX;
uint32_t serverBeginCount = 0U;
volatile bool networkLossRequested = false;
uint8_t networkRecoveryState = 0U;
uint32_t networkRecoveryStartedMs = 0U;
uint32_t lastTelemetryMs = 0U;

uint8_t acceptedBody[MAX_BODY_LENGTH] {};
size_t acceptedBodyReceived = 0U;
size_t acceptedBodyTotal = 0U;
bool bodyRejected = false;
bool bodySequenceError = false;

struct WsClientState {
    uint32_t clientId = 0U;
    AsyncWebSocketClient* client = nullptr;
    bool messageActive = false;
    uint8_t messageOpcode = WS_TEXT;
    uint32_t expectedFrameNumber = 0U;
    size_t messageLength = 0U;
    size_t currentFrameReceived = 0U;
    uint8_t message[MAX_WS_MESSAGE_LENGTH] {};
    size_t maxQueueLength = 0U;
    uint32_t queueFullSamples = 0U;
};

WsClientState clients[MAX_TRACKED_WS_CLIENTS] {};

void printTaskContext(const char* callbackName) {
    const char* taskName = pcTaskGetName(nullptr);
    Serial.printf("callback_context=%s task=%s core=%d\n",
                  callbackName,
                  taskName == nullptr ? "unknown" : taskName,
                  static_cast<int>(xPortGetCoreID()));
}

void printHeap(const char* stage) {
    Serial.printf("heap stage=%s free=%u min=%u largest=%u ws_clients=%u\n",
                  stage,
                  ESP.getFreeHeap(),
                  ESP.getMinFreeHeap(),
                  heap_caps_get_largest_free_block(MALLOC_CAP_8BIT),
                  static_cast<unsigned>(websocket.count()));
}

void updatePayloads() {
    snprintf(snapshotPayload, sizeof(snapshotPayload),
             "{\"revision\":%lu,\"value\":42}",
             static_cast<unsigned long>(revision));
    snprintf(eventPayload, sizeof(eventPayload),
             "{\"type\":\"spike.changed\",\"revision\":%lu}",
             static_cast<unsigned long>(revision));
    snprintf(ackPayload, sizeof(ackPayload),
             "{\"type\":\"spike.ack\",\"revision\":%lu}",
             static_cast<unsigned long>(revision));
}

WsClientState* findClient(uint32_t clientId) {
    for (WsClientState& state : clients) {
        if (state.client != nullptr && state.clientId == clientId) {
            return &state;
        }
    }
    return nullptr;
}

WsClientState* addClient(AsyncWebSocketClient* client) {
    for (WsClientState& state : clients) {
        if (state.client == nullptr) {
            state = WsClientState {};
            state.clientId = client->id();
            state.client = client;
            return &state;
        }
    }
    return nullptr;
}

void sampleQueues(uint32_t eventNumber) {
    for (WsClientState& state : clients) {
        if (state.client == nullptr) {
            continue;
        }
        const size_t queueLength = state.client->queueLen();
        if (queueLength > state.maxQueueLength) {
            state.maxQueueLength = queueLength;
        }
        const bool full = state.client->queueIsFull();
        if (full) {
            ++state.queueFullSamples;
        }
        if (eventNumber % 8U == 0U) {
            Serial.printf("ws_queue event=%lu client=%lu len=%u full=%s max=%u full_samples=%lu\n",
                          static_cast<unsigned long>(eventNumber),
                          static_cast<unsigned long>(state.clientId),
                          static_cast<unsigned>(queueLength),
                          full ? "true" : "false",
                          static_cast<unsigned>(state.maxQueueLength),
                          static_cast<unsigned long>(state.queueFullSamples));
        }
    }
}

const char* sendStatusName(AsyncWebSocket::SendStatus status) {
    switch (status) {
        case AsyncWebSocket::DISCARDED: return "DISCARDED";
        case AsyncWebSocket::ENQUEUED: return "ENQUEUED";
        case AsyncWebSocket::PARTIALLY_ENQUEUED: return "PARTIALLY_ENQUEUED";
    }
    return "UNKNOWN";
}

void processCompleteMessage(AsyncWebSocketClient* client, WsClientState* state) {
    if (state->messageOpcode == WS_BINARY) {
        Serial.printf("ws_binary_accepted client=%lu bytes=%u\n",
                      static_cast<unsigned long>(client->id()),
                      static_cast<unsigned>(state->messageLength));
        state->messageLength = 0U;
        state->expectedFrameNumber = 0U;
        state->messageActive = false;
        return;
    }

    if (state->messageOpcode != WS_TEXT) {
        client->close(1003U, "unsupported fixture opcode");
        state->messageActive = false;
        return;
    }

    ++revision;
    ++wsEvents;
    updatePayloads();
    const bool directed = client->text(ackPayload);
    const AsyncWebSocket::SendStatus broadcast = websocket.textAll(eventPayload);
    Serial.printf("ws_send event=%lu client=%lu directed=%s broadcast=%s queue_all_available=%s\n",
                  static_cast<unsigned long>(wsEvents),
                  static_cast<unsigned long>(client->id()),
                  directed ? "true" : "false",
                  sendStatusName(broadcast),
                  websocket.availableForWriteAll() ? "true" : "false");
    sampleQueues(wsEvents);
    state->messageLength = 0U;
    state->expectedFrameNumber = 0U;
    state->messageActive = false;
}

void onWebSocketEvent(AsyncWebSocket*, AsyncWebSocketClient* client,
                      AwsEventType type, void* argument, uint8_t* data,
                      size_t length) {
    switch (type) {
        case WS_EVT_CONNECT: {
            WsClientState* state = addClient(client);
            Serial.printf("ws_connect client=%lu tracked=%s count=%u\n",
                          static_cast<unsigned long>(client->id()),
                          state == nullptr ? "false" : "true",
                          static_cast<unsigned>(websocket.count()));
            printTaskContext("ws_connect");
            const bool greetingSent = client->text(eventPayload);
            Serial.printf("ws_greeting_send=%s\n", greetingSent ? "true" : "false");
            printHeap("ws_connect");
            if (state == nullptr) {
                client->close(1011U, "fixture client slots exhausted");
            }
            break;
        }
        case WS_EVT_DISCONNECT: {
            Serial.printf("ws_disconnect client=%lu\n",
                          static_cast<unsigned long>(client->id()));
            WsClientState* state = findClient(client->id());
            if (state != nullptr) {
                *state = WsClientState {};
            }
            printHeap("ws_disconnect");
            break;
        }
        case WS_EVT_DATA: {
            AwsFrameInfo* frame = static_cast<AwsFrameInfo*>(argument);
            WsClientState* state = findClient(client->id());
            Serial.printf("ws_data client=%lu num=%lu index=%llu len=%llu final=%u opcode=%u message_opcode=%u chunk=%u tracked=%s\n",
                          static_cast<unsigned long>(client->id()),
                          static_cast<unsigned long>(frame == nullptr ? 0U : frame->num),
                          static_cast<unsigned long long>(frame == nullptr ? 0U : frame->index),
                          static_cast<unsigned long long>(frame == nullptr ? 0U : frame->len),
                          frame == nullptr ? 0U : static_cast<unsigned>(frame->final),
                          frame == nullptr ? 0U : static_cast<unsigned>(frame->opcode),
                          frame == nullptr ? 0U : static_cast<unsigned>(frame->message_opcode),
                          static_cast<unsigned>(length), state == nullptr ? "false" : "true");
            if (frame == nullptr || state == nullptr) {
                Serial.println("ws_reject=invalid_metadata");
                client->close(1002U, "invalid fixture frame metadata");
                break;
            }

            if (frame->num == 0U && frame->index == 0U) {
                state->messageActive = true;
                // ESPAsyncWebServer 3.6.0 can leave message_opcode at zero when
                // a complete frame arrives in one TCP chunk; opcode remains set.
                state->messageOpcode = frame->message_opcode != 0U
                    ? frame->message_opcode : frame->opcode;
                state->expectedFrameNumber = 0U;
                state->messageLength = 0U;
                state->currentFrameReceived = 0U;
            }
            if ((state->messageOpcode != WS_TEXT && state->messageOpcode != WS_BINARY) ||
                (frame->message_opcode != 0U && frame->message_opcode != state->messageOpcode)) {
                Serial.println("ws_reject=invalid_message_opcode");
                client->close(1002U, "invalid fixture message opcode");
                state->messageActive = false;
                break;
            }
            if (!state->messageActive || frame->num != state->expectedFrameNumber ||
                frame->index != state->currentFrameReceived) {
                Serial.printf("ws_reject=fragment_order active=%s expected_num=%lu expected_index=%u\n",
                              state->messageActive ? "true" : "false",
                              static_cast<unsigned long>(state->expectedFrameNumber),
                              static_cast<unsigned>(state->currentFrameReceived));
                client->close(1002U, "invalid fixture fragment order");
                state->messageActive = false;
                break;
            }
            if (frame->len > MAX_WS_MESSAGE_LENGTH - state->messageLength) {
                Serial.printf("ws_oversize client=%lu bytes_at_least=%llu\n",
                              static_cast<unsigned long>(client->id()),
                              static_cast<unsigned long long>(state->messageLength + frame->len));
                client->close(1009U, "fixture message exceeds 256 bytes");
                state->messageActive = false;
                break;
            }
            if (frame->index + length > frame->len ||
                state->messageLength + frame->index + length > MAX_WS_MESSAGE_LENGTH) {
                Serial.println("ws_reject=fragment_length");
                client->close(1002U, "invalid fixture fragment length");
                state->messageActive = false;
                break;
            }
            memcpy(state->message + state->messageLength + frame->index, data, length);
            state->currentFrameReceived += length;
            if (state->currentFrameReceived == frame->len) {
                state->messageLength += state->currentFrameReceived;
                state->currentFrameReceived = 0U;
                state->expectedFrameNumber = frame->num + 1U;
                if (frame->final != 0U) {
                    processCompleteMessage(client, state);
                }
            }
            break;
        }
        case WS_EVT_PING:
            Serial.printf("ws_ping client=%lu bytes=%u\n",
                          static_cast<unsigned long>(client->id()),
                          static_cast<unsigned>(length));
            break;
        case WS_EVT_PONG:
            Serial.printf("ws_pong client=%lu bytes=%u\n",
                          static_cast<unsigned long>(client->id()),
                          static_cast<unsigned>(length));
            break;
        case WS_EVT_ERROR:
            Serial.printf("ws_error client=%lu\n",
                          static_cast<unsigned long>(client->id()));
            break;
    }
}

void snapshotHandler(AsyncWebServerRequest* request) {
    ++snapshotRequests;
    if (snapshotRequests == 1U) {
        printTaskContext("http_snapshot");
    }
    if (snapshotRequests == 10U) {
        printHeap("H4_after_10_concurrent_gets");
    }
    request->send(200, "application/json", snapshotPayload);
}

void bodyChunkHandler(AsyncWebServerRequest*, uint8_t* data, size_t length,
                      size_t index, size_t total) {
    if (index == 0U) {
        acceptedBodyTotal = total;
        acceptedBodyReceived = 0U;
        bodyRejected = total > MAX_BODY_LENGTH;
        bodySequenceError = false;
        Serial.printf("post_begin total=%u early_oversize=%s\n",
                      static_cast<unsigned>(total),
                      bodyRejected ? "true" : "false");
    }
    if (bodyRejected) {
        return;
    }
    if (index != acceptedBodyReceived || index + length > total ||
        index + length > MAX_BODY_LENGTH) {
        bodySequenceError = true;
        bodyRejected = true;
        return;
    }
    memcpy(acceptedBody + index, data, length);
    acceptedBodyReceived += length;
}

void bodyRequestHandler(AsyncWebServerRequest* request) {
    const size_t contentLength = request->contentLength();
    if (contentLength == 0U) {
        request->send(400, "text/plain", "Body required");
        return;
    }
    if (bodyRejected || contentLength > MAX_BODY_LENGTH) {
        Serial.printf("post_reject total=%u received_into_fixture=%u sequence_error=%s\n",
                      static_cast<unsigned>(contentLength),
                      static_cast<unsigned>(acceptedBodyReceived),
                      bodySequenceError ? "true" : "false");
        request->send(413, "text/plain", "Payload Too Large");
        return;
    }
    if (bodySequenceError || acceptedBodyReceived != contentLength) {
        request->send(400, "text/plain", "Body incomplete");
        return;
    }
    Serial.printf("post_accept bytes=%u\n", static_cast<unsigned>(acceptedBodyReceived));
    request->send(200, "text/plain", "Accepted");
}

void networkLossHandler(AsyncWebServerRequest* request) {
    networkLossRequested = true;
    request->send(202, "text/plain", "ESP reconnect queued");
}

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
    if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        Serial.printf("wifi_disconnect_reason=%u\n",
                      static_cast<unsigned>(info.wifi_sta_disconnected.reason));
    }
}

void sampleClientCount() {
    const uint32_t count = static_cast<uint32_t>(websocket.count());
    if (count == lastWsClientCount) {
        return;
    }
    Serial.printf("ws_clients=%lu\n", static_cast<unsigned long>(count));
    if (lastWsClientCount >= 2U && count == 1U) {
        printHeap("H5_one_ws_closed");
    } else if (count == 1U) {
        printHeap("H2_one_ws");
    } else if (count == 2U) {
        printHeap("H3_two_ws");
    } else if (lastWsClientCount != UINT32_MAX && lastWsClientCount > 0U && count == 0U) {
        printHeap("H6_all_ws_closed");
    }
    lastWsClientCount = count;
}

void printStability() {
    printHeap("stability");
    Serial.printf("uptime_ms=%lu snapshot_gets=%lu ws_events=%lu ws_server_begin_count=%lu\n",
                  static_cast<unsigned long>(millis()),
                  static_cast<unsigned long>(snapshotRequests),
                  static_cast<unsigned long>(wsEvents),
                  static_cast<unsigned long>(serverBeginCount));
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    Serial.printf("hardware flash_bytes=%u psram_found=%s psram_bytes=%u\n",
                  ESP.getFlashChipSize(), psramFound() ? "true" : "false",
                  ESP.getPsramSize());
    printHeap("H0_before_network");
    WiFi.onEvent(onWifiEvent);
    WiFi.mode(WIFI_STA);
    WiFi.begin(F8_3B_WIFI_SSID, F8_3B_WIFI_PASSWORD);
    const uint32_t connectStartedMs = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - connectStartedMs < WIFI_CONNECT_TIMEOUT_MS) {
        delay(100);
    }
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("wifi_connected=false");
        return;
    }
    Serial.println("wifi_connected=true");
    Serial.printf("ip=%s\n", WiFi.localIP().toString().c_str());

    websocket.onEvent(onWebSocketEvent);
    server.addHandler(&websocket);
    server.on("/api/spike/snapshot", HTTP_GET, snapshotHandler);
    server.on("/api/spike/body", HTTP_POST, bodyRequestHandler, nullptr, bodyChunkHandler);
    server.on("/api/spike/hil/network-loss", HTTP_POST, networkLossHandler);
    server.begin();
    ++serverBeginCount;
    Serial.printf("server_started=true server_begin_count=%lu\n",
                  static_cast<unsigned long>(serverBeginCount));
    printHeap("H1_async_server_started");
}

void loop() {
    if (networkLossRequested && networkRecoveryState == 0U) {
        networkLossRequested = false;
        networkRecoveryState = 1U;
        networkRecoveryStartedMs = millis();
        Serial.println("hil_network_loss_scheduled=true");
    }
    if (networkRecoveryState == 1U &&
        millis() - networkRecoveryStartedMs >= WIFI_RECONNECT_DELAY_MS) {
        networkRecoveryState = 2U;
        networkRecoveryStartedMs = millis();
        WiFi.disconnect(false, false);
        Serial.println("hil_network_loss_started=true");
    }
    if (networkRecoveryState == 2U &&
        millis() - networkRecoveryStartedMs >= WIFI_RECONNECT_DELAY_MS) {
        networkRecoveryState = 3U;
        WiFi.begin(F8_3B_WIFI_SSID, F8_3B_WIFI_PASSWORD);
    }
    if (networkRecoveryState == 3U && WiFi.status() == WL_CONNECTED) {
        networkRecoveryState = 0U;
        Serial.println("wifi_connected=true");
        Serial.printf("ip=%s\n", WiFi.localIP().toString().c_str());
        Serial.printf("hil_network_recovered=true server_begin_count=%lu\n",
                      static_cast<unsigned long>(serverBeginCount));
    }

    websocket.cleanupClients();
    sampleClientCount();
    const uint32_t now = millis();
    if (now - lastTelemetryMs >= 30000U) {
        lastTelemetryMs = now;
        printStability();
    }
    delay(10);
}
