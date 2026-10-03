#include <Arduino.h>
#include <WiFi.h>

#include <cstdint>

#include "esp_http_server.h"

namespace {

constexpr char ACCESS_POINT_NAME[] = "aquaone-f8-spike";
constexpr char SNAPSHOT[] = "{\"revision\":1,\"value\":42}";
constexpr char EVENT[] = "{\"type\":\"spike.changed\",\"revision\":1}";
constexpr size_t MAX_BODY_LENGTH = 256U;

httpd_handle_t server = nullptr;

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
    httpd_resp_set_type(request, "application/json");
    return httpd_resp_send(request, SNAPSHOT, HTTPD_RESP_USE_STRLEN);
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

void sendConnectEvent(void* argument) {
    const int socket = static_cast<int>(reinterpret_cast<intptr_t>(argument));
    httpd_ws_frame_t frame {};
    frame.type = HTTPD_WS_TYPE_TEXT;
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(EVENT));
    frame.len = sizeof(EVENT) - 1U;
    httpd_ws_send_frame_async(server, socket, &frame);
    printHeap("ws_connect");
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
    frame.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(EVENT));
    frame.len = sizeof(EVENT) - 1U;

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
        httpd_ws_frame_t response {};
        response.type = HTTPD_WS_TYPE_TEXT;
        response.payload = reinterpret_cast<uint8_t*>(const_cast<char*>(EVENT));
        response.len = sizeof(EVENT) - 1U;
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

    return
        httpd_register_uri_handler(server, &snapshot) == ESP_OK &&
        httpd_register_uri_handler(server, &body) == ESP_OK &&
        httpd_register_uri_handler(server, &websocket) == ESP_OK;
}

} // namespace

void setup() {
    Serial.begin(115200);
    delay(100);
    printHeap("before_network");

    WiFi.mode(WIFI_AP);
    WiFi.softAP(ACCESS_POINT_NAME);
    printHeap("before_server");

    Serial.printf("server_started=%s\n", startServer() ? "true" : "false");
    printHeap("after_server");
    Serial.printf("AP %s IP %s\n", ACCESS_POINT_NAME, WiFi.softAPIP().toString().c_str());
}

void loop() {
    delay(1000);
}
