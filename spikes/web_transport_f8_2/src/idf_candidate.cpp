#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include <cstdint>

#include "esp_http_server.h"
#include "f8_3a_credentials.h"

namespace {

constexpr size_t MAX_BODY_LENGTH = 256U;
constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 30000U;
constexpr uint32_t WIFI_RECONNECT_DELAY_MS = 1500U;

httpd_handle_t server = nullptr;
char snapshotPayload[64] = "{\"revision\":1,\"value\":42}";
char eventPayload[80] = "{\"type\":\"spike.changed\",\"revision\":1}";
uint32_t revision = 1U;
uint32_t snapshotRequests = 0U;
uint32_t wsConnectRequests = 0U;
uint32_t wsEvents = 0U;
uint32_t lastWsClientCount = UINT32_MAX;
volatile bool wsSampleQueued = false;
volatile bool networkLossRequested = false;
uint8_t networkRecoveryState = 0U;
uint32_t networkRecoveryStartedMs = 0U;

void updatePayloads() {
    snprintf(snapshotPayload, sizeof(snapshotPayload),
             "{\"revision\":%lu,\"value\":42}",
             static_cast<unsigned long>(revision));
    snprintf(eventPayload, sizeof(eventPayload),
             "{\"type\":\"spike.changed\",\"revision\":%lu}",
             static_cast<unsigned long>(revision));
}

void printHeap(const char* stage) {
    Serial.printf(
        "heap stage=%s free=%u min=%u largest=%u\n",
        stage,
        ESP.getFreeHeap(),
        ESP.getMinFreeHeap(),
        heap_caps_get_largest_free_block(MALLOC_CAP_8BIT)
    );
}

esp_err_t snapshotHandler(httpd_req_t* request) {
    ++snapshotRequests;
    if (snapshotRequests == 10U) {
        printHeap("H4_after_10_http_gets");
    }
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, snapshotPayload, HTTPD_RESP_USE_STRLEN);
}

esp_err_t bodyHandler(httpd_req_t* request) {
    if (request->content_len == 0U) {
        httpd_resp_set_status(request, "400 Bad Request");
        return httpd_resp_send(request, "Body required", HTTPD_RESP_USE_STRLEN);
    }
    if (request->content_len > MAX_BODY_LENGTH) {
        httpd_resp_set_status(request, "413 Payload Too Large");
        return httpd_resp_send(request, "Payload Too Large", HTTPD_RESP_USE_STRLEN);
    }

    Serial.printf("post accepted length=%u\n", request->content_len);

    uint8_t body[MAX_BODY_LENGTH];
    size_t received = 0U;
    while (received < request->content_len) {
        const int result = httpd_req_recv(
            request,
            reinterpret_cast<char*>(body + received),
            request->content_len - received
        );
        if (result == HTTPD_SOCK_ERR_TIMEOUT) {
            continue;
        }
        if (result <= 0) {
            httpd_resp_set_status(request, "400 Bad Request");
            return httpd_resp_send(request, "Body read failed", HTTPD_RESP_USE_STRLEN);
        }
        received += static_cast<size_t>(result);
    }

    return httpd_resp_send(request, "Accepted", HTTPD_RESP_USE_STRLEN);
}

esp_err_t networkLossHandler(httpd_req_t* request) {
    networkLossRequested = true;
    httpd_resp_set_status(request, "202 Accepted");
    return httpd_resp_send(request, "ESP reconnect queued", HTTPD_RESP_USE_STRLEN);
}

void sendConnectEvent(void* argument) {
    const int socket = static_cast<int>(reinterpret_cast<intptr_t>(argument));
    httpd_ws_frame_t frame {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(eventPayload);
    frame.len = strlen(eventPayload);
    httpd_ws_send_frame_async(server, socket, &frame);
    printHeap("ws_connect");
}

void sampleWsClients(void*) {
    wsSampleQueued = false;
    if (server == nullptr) {
        return;
    }
    size_t socketCount = 7U;
    int sockets[7] {};
    if (httpd_get_client_list(server, &socketCount, sockets) != ESP_OK) {
        return;
    }
    uint32_t wsCount = 0U;
    for (size_t index = 0U; index < socketCount; ++index) {
        if (httpd_ws_get_fd_info(server, sockets[index]) == HTTPD_WS_CLIENT_WEBSOCKET) {
            ++wsCount;
        }
    }
    if (wsCount == lastWsClientCount) {
        return;
    }
    Serial.printf("ws_clients=%lu\n", static_cast<unsigned long>(wsCount));
    if (lastWsClientCount >= 2U && wsCount == 1U) {
        printHeap("H5_one_ws_closed");
    } else if (wsCount == 1U) {
        printHeap("H2_one_ws");
    } else if (wsCount == 2U) {
        printHeap("H3_two_ws");
    } else if (lastWsClientCount != UINT32_MAX && lastWsClientCount > 0U && wsCount == 0U) {
        printHeap("H6_all_ws_closed");
    }
    lastWsClientCount = wsCount;
}

void broadcastEvent(void* argument) {
    const int originSocket = static_cast<int>(
        reinterpret_cast<intptr_t>(argument)
    );
    size_t socketCount = 7U;
    int sockets[7] {};
    if (httpd_get_client_list(server, &socketCount, sockets) != ESP_OK) {
        return;
    }

    httpd_ws_frame_t frame {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(eventPayload);
    frame.len = strlen(eventPayload);

    for (size_t index = 0U; index < socketCount; ++index) {
        if (
            sockets[index] != originSocket &&
            httpd_ws_get_fd_info(server, sockets[index]) ==
            HTTPD_WS_CLIENT_WEBSOCKET
        ) {
            httpd_ws_send_frame_async(server, sockets[index], &frame);
        }
    }
}

esp_err_t websocketHandler(httpd_req_t* request) {
    if (request->method == HTTP_GET) {
        ++wsConnectRequests;
        const int socket = httpd_req_to_sockfd(request);
        return httpd_queue_work(
            server,
            sendConnectEvent,
            reinterpret_cast<void*>(static_cast<intptr_t>(socket))
        );
    }

    httpd_ws_frame_t frame {};
    esp_err_t result = httpd_ws_recv_frame(request, &frame, 0U);
    if (result != ESP_OK) {
        return result;
    }
    if (frame.len > MAX_BODY_LENGTH) {
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t payload[MAX_BODY_LENGTH + 1U] {};
    frame.payload = payload;
    result = httpd_ws_recv_frame(request, &frame, MAX_BODY_LENGTH);
    if (result != ESP_OK) {
        return result;
    }

    if (frame.type == HTTPD_WS_TYPE_TEXT) {
        ++revision;
        ++wsEvents;
        updatePayloads();
        httpd_ws_frame_t response {};
        response.type = HTTPD_WS_TYPE_TEXT;
        response.payload = reinterpret_cast<uint8_t*>(eventPayload);
        response.len = strlen(eventPayload);
        result = httpd_ws_send_frame(request, &response);
        if (result != ESP_OK) {
            return result;
        }

        const int socket = httpd_req_to_sockfd(request);
        return httpd_queue_work(
            server,
            broadcastEvent,
            reinterpret_cast<void*>(static_cast<intptr_t>(socket))
        );
    }
    return ESP_OK;
}

bool startServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80U;

    if (httpd_start(&server, &config) != ESP_OK) {
        return false;
    }

    httpd_uri_t snapshot {};
    snapshot.uri = "/api/spike/snapshot";
    snapshot.method = HTTP_GET;
    snapshot.handler = snapshotHandler;

    httpd_uri_t body {};
    body.uri = "/api/spike/body";
    body.method = HTTP_POST;
    body.handler = bodyHandler;

    httpd_uri_t websocket {};
    websocket.uri = "/ws/spike";
    websocket.method = HTTP_GET;
    websocket.handler = websocketHandler;
    websocket.is_websocket = true;
    websocket.handle_ws_control_frames = false;

    httpd_uri_t networkLoss {};
    networkLoss.uri = "/api/spike/hil/network-loss";
    networkLoss.method = HTTP_POST;
    networkLoss.handler = networkLossHandler;

    return
        httpd_register_uri_handler(server, &snapshot) == ESP_OK &&
        httpd_register_uri_handler(server, &body) == ESP_OK &&
        httpd_register_uri_handler(server, &networkLoss) == ESP_OK &&
        httpd_register_uri_handler(server, &websocket) == ESP_OK;
}

void printStability(void*) {
    const uint32_t now = millis();
    printHeap("stability");
    Serial.printf("uptime_ms=%lu snapshot_gets=%lu ws_connect_requests=%lu ws_events=%lu\n",
                  static_cast<unsigned long>(now),
                  static_cast<unsigned long>(snapshotRequests),
                  static_cast<unsigned long>(wsConnectRequests),
                  static_cast<unsigned long>(wsEvents));
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    printHeap("H0_before_network");

    WiFi.mode(WIFI_STA);
    WiFi.begin(F8_3A_WIFI_SSID, F8_3A_WIFI_PASSWORD);
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
    printHeap("before_server");

    const bool started = startServer();
    Serial.printf("server_started=%s\n", started ? "true" : "false");
    if (started) {
        printHeap("H1_httpd_started");
    }
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
        WiFi.begin(F8_3A_WIFI_SSID, F8_3A_WIFI_PASSWORD);
    }
    if (networkRecoveryState == 3U && WiFi.status() == WL_CONNECTED) {
        networkRecoveryState = 0U;
        Serial.println("wifi_connected=true");
        Serial.printf("ip=%s\n", WiFi.localIP().toString().c_str());
        Serial.println("hil_network_recovered=true");
    }
    if (server != nullptr && !wsSampleQueued) {
        wsSampleQueued = true;
        if (httpd_queue_work(server, sampleWsClients, nullptr) != ESP_OK) {
            wsSampleQueued = false;
        }
    }
    static uint32_t lastTelemetryMs = 0U;
    const uint32_t now = millis();
    if (server != nullptr && now - lastTelemetryMs >= 30000U) {
        lastTelemetryMs = now;
        httpd_queue_work(server, printStability, nullptr);
    }
    delay(1000);
}
