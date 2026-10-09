#pragma once

#include <stddef.h>
#include <stdint.h>

#include <AquaCore/Web/HttpStreamingServerTransport.h>

#include "DoserMultipartFirmwareParser.h"
#include "DoserOtaApplication.h"

// Registered on the same physical transport as NativeWebService in G4.
// This G3 candidate remains uncomposed in production.
class DoserNativeOtaRoute {
public:
    DoserNativeOtaRoute(StreamingUploadBridge& bridge,
                        DoserOtaCapacitySnapshot& capacity, DoserOtaClock& clock,
                        const char* user, const char* password)
        : bridge_(bridge), capacity_(capacity), clock_(clock),
          user_(user), password_(password) {}
    bool addTo(AquaCore::Web::HttpStreamingServerTransport& transport,
               uint32_t startupCapacity);
    static AquaCore::Web::HttpStreamHandlerResult handle(
        void* context, const AquaCore::Web::HttpStreamEvent& event,
        AquaCore::Web::WebResponseWriter& response);

private:
    bool authenticate(const AquaCore::Web::HttpStreamRequest& request,
                      AquaCore::Web::WebResponseWriter& response);
    bool start(const AquaCore::Web::HttpStreamRequest& request,
               AquaCore::Web::WebResponseWriter& response);
    bool data(const uint8_t* bytes, size_t length,
              AquaCore::Web::WebResponseWriter& response);
    bool end(AquaCore::Web::WebResponseWriter& response);
    bool send(AquaCore::Web::WebResponseWriter& response, uint16_t status,
              const char* text);
    bool submit(UploadOperation kind, const uint8_t* bytes, size_t length,
                AquaCore::Web::WebResponseWriter& response);
    static bool parserSink(void* context, DoserMultipartFirmwareParser::Event event,
                           const uint8_t* bytes, size_t length);
    bool onParser(DoserMultipartFirmwareParser::Event event,
                  const uint8_t* bytes, size_t length);
    bool expired() const;

    StreamingUploadBridge& bridge_;
    DoserOtaCapacitySnapshot& capacity_;
    DoserOtaClock& clock_;
    const char* user_;
    const char* password_;
    DoserMultipartFirmwareParser parser_;
    AquaCore::Web::WebResponseWriter* response_ = nullptr;
    char contentType_[128] {};
    uint64_t generation_ = 0U;
    uint32_t firmwareSize_ = 0U;
    uint32_t fileBytes_ = 0U;
    uint32_t startedAt_ = 0U;
    bool responded_ = false;
    bool committed_ = false;
};
