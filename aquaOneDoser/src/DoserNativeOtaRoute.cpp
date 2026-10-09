#include "DoserNativeOtaRoute.h"

#include <limits.h>
#include <string.h>

using namespace AquaCore::Web;

bool DoserNativeOtaRoute::addTo(HttpStreamingServerTransport& transport,
                                uint32_t startupCapacity) {
    if (startupCapacity == 0U ||
        static_cast<uint64_t>(startupCapacity) + 664U > SIZE_MAX) return false;
    return transport.addStreamingRoute("/update", HttpMethod::Post,
                                       handle, this,
                                       static_cast<size_t>(startupCapacity) + 664U);
}

bool DoserNativeOtaRoute::send(WebResponseWriter& response, uint16_t status,
                                const char* message) {
    if (responded_) return false;
    responded_ = true;
    return response.beginResponse(status, ContentType::PlainText) &&
           response.writeText(message) && response.endResponse();
}

bool DoserNativeOtaRoute::authenticate(const HttpStreamRequest& request,
                                        WebResponseWriter& response) {
    if (password_ == nullptr || password_[0] == 0 ||
        strcmp(password_, "CHANGE_ME_BEFORE_USE") == 0) {
        (void)send(response, 503U,
                   "Funkcje administracyjne są wyłączone. Ustaw WEB_PASS w secrets.h.");
        return false;
    }
    if (request.context != nullptr && request.context->authenticateBasic(user_, password_))
        return true;
    responded_ = true;
    if (request.context == nullptr ||
        !request.context->requestBasicAuthentication("PMW AquaDoser")) {
        responded_ = false;
        (void)send(response, 500U, "Authentication unavailable");
    }
    return false;
}

bool DoserNativeOtaRoute::start(const HttpStreamRequest& request,
                                 WebResponseWriter& response) {
    generation_ = 0U;
    fileBytes_ = firmwareSize_ = 0U;
    responded_ = committed_ = false;
    response_ = &response;
    startedAt_ = clock_.nowMs();
    if (!authenticate(request, response)) return false;
    const UploadSessionView session = bridge_.sessionView();
    if (session != UploadSessionView::Idle) {
        (void)send(response, session == UploadSessionView::Busy ? 409U : 503U,
                   session == UploadSessionView::Busy ? "OTA in progress" : "OTA unavailable");
        return false;
    }
    if (request.context == nullptr || !request.context->hasHeader("Content-Type")) {
        (void)send(response, 400U, "Invalid Content-Type"); return false;
    }
    const size_t typeLength = request.context->copyHeader(
        "Content-Type", contentType_, sizeof(contentType_));
    char boundary[71] {};
    if (typeLength == 0U || typeLength >= sizeof(contentType_) ||
        !DoserMultipartFirmwareParser::parseContentType(contentType_, boundary)) {
        (void)send(response, 400U, "Invalid Content-Type"); return false;
    }
    char sizeText[11] {};
    const size_t sizeLength = request.context->copyHeader(
        "X-Firmware-Size", sizeText, sizeof(sizeText));
    if (sizeLength == 0U || sizeLength >= sizeof(sizeText) ||
        !DoserMultipartFirmwareParser::parseFirmwareSize(sizeText, firmwareSize_)) {
        (void)send(response, 400U, "Invalid X-Firmware-Size"); return false;
    }
    DoserOtaCapacity capacity {};
    if (!capacity_.read(capacity) || !capacity.available) {
        (void)send(response, 503U, "OTA capacity unavailable"); return false;
    }
    if (firmwareSize_ > capacity.availableBytes ||
        static_cast<uint64_t>(request.contentLength) >
            static_cast<uint64_t>(capacity.availableBytes) + 664U) {
        (void)send(response, 413U, "Firmware exceeds OTA capacity"); return false;
    }
    if (!DoserMultipartFirmwareParser::validContentLength(
            request.contentLength, firmwareSize_)) {
        (void)send(response, request.contentLength >
            static_cast<uint64_t>(firmwareSize_) + 664U ? 413U : 400U,
            "Invalid multipart Content-Length"); return false;
    }
    if (!parser_.begin(boundary)) {
        (void)send(response, 400U, "Invalid multipart boundary"); return false;
    }
    return true;
}

bool DoserNativeOtaRoute::expired() const {
    return static_cast<uint32_t>(clock_.nowMs() - startedAt_) >= 600000U;
}

bool DoserNativeOtaRoute::submit(UploadOperation kind, const uint8_t* bytes,
                                  size_t length, WebResponseWriter& response) {
    if (expired()) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 408U, "Update request timed out"); return false;
    }
    UploadCommand command {};
    command.kind = kind;
    command.generation = generation_;
    command.expectedSize = kind == UploadOperation::Start ? firmwareSize_ : 0U;
    command.length = static_cast<uint16_t>(length);
    if (!bridge_.submit(command, bytes)) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 503U, "Update bridge unavailable"); return false;
    }
    DoserOtaResult result {};
    const UploadWaitResult waited = bridge_.wait(generation_, 30000U, result);
    if (waited != UploadWaitResult::Completed) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 503U, kind == UploadOperation::End
                   ? "Update outcome unknown" : "Update bridge timeout");
        return false;
    }
    if (result.error == DoserOtaError::None) return true;
    if (result.error == DoserOtaError::UpdateFailure) {
        if (!responded_) {
            responded_ = true;
            (void)response.beginResponse(500U, ContentType::PlainText);
            (void)response.writeText("Aktualizacja nieudana: ");
            (void)response.writeText(result.diagnostic[0] != 0
                ? result.diagnostic : "Update failed");
            (void)response.endResponse();
        }
    } else {
        const uint16_t code = result.error == DoserOtaError::Busy ? 409U :
            result.error == DoserOtaError::TooLarge ? 413U :
            result.error == DoserOtaError::Invalid ? 400U : 503U;
        (void)send(response, code, "Update rejected");
    }
    return false;
}

bool DoserNativeOtaRoute::onParser(DoserMultipartFirmwareParser::Event event,
                                    const uint8_t* bytes, size_t length) {
    if (event == DoserMultipartFirmwareParser::Event::Start) {
        const UploadAdmission admitted = bridge_.admitSession(generation_);
        if (admitted != UploadAdmission::Accepted) {
            (void)send(*response_, admitted == UploadAdmission::Busy ? 409U : 503U,
                       admitted == UploadAdmission::Busy ? "OTA in progress" : "OTA unavailable");
            return false;
        }
        return submit(UploadOperation::Start, nullptr, 0U, *response_);
    }
    if (event == DoserMultipartFirmwareParser::Event::Data) {
        if (length == 0U || length > 1024U || length > firmwareSize_ - fileBytes_) {
            (void)send(*response_, 400U, "Firmware size mismatch"); return false;
        }
        if (!submit(UploadOperation::Chunk, bytes, length, *response_)) return false;
        fileBytes_ += static_cast<uint32_t>(length);
    }
    return true;
}

bool DoserNativeOtaRoute::parserSink(void* context,
                                     DoserMultipartFirmwareParser::Event event,
                                     const uint8_t* bytes, size_t length) {
    return static_cast<DoserNativeOtaRoute*>(context)->onParser(event, bytes, length);
}

bool DoserNativeOtaRoute::data(const uint8_t* bytes, size_t length,
                                WebResponseWriter& response) {
    response_ = &response;
    if (expired()) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 408U, "Update request timed out"); return false;
    }
    if (!parser_.feed(bytes, length, parserSink, this)) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 400U, "Malformed multipart firmware"); return false;
    }
    return true;
}

bool DoserNativeOtaRoute::end(WebResponseWriter& response) {
    response_ = &response;
    if (expired()) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 408U, "Update request timed out"); return false;
    }
    if (!parser_.finish(parserSink, this) || generation_ == 0U ||
        fileBytes_ != firmwareSize_) {
        (void)bridge_.requestCancel(generation_);
        (void)send(response, 400U, "Malformed multipart firmware"); return false;
    }
    if (!submit(UploadOperation::End, nullptr, 0U, response)) return false;
    committed_ = true;
    // Response completion precedes the restart acknowledgement.
    if (!send(response, 200U,
              "Aktualizacja zakończona. Urządzenie uruchomi się ponownie."))
        return true; // The Application watchdog owns committed-image recovery.
    UploadCommand arm {};
    arm.kind = UploadOperation::ArmRestart;
    arm.generation = generation_;
    if (bridge_.submit(arm)) {
        DoserOtaResult result {};
        (void)bridge_.wait(generation_, 30000U, result);
    }
    return true;
}

HttpStreamHandlerResult DoserNativeOtaRoute::handle(
    void* context, const HttpStreamEvent& event, WebResponseWriter& response) {
    if (context == nullptr) return HttpStreamHandlerResult::Stop;
    DoserNativeOtaRoute& self = *static_cast<DoserNativeOtaRoute*>(context);
    if (event.type == HttpStreamEventType::BodyStart)
        return event.request != nullptr && self.start(*event.request, response)
            ? HttpStreamHandlerResult::Continue : HttpStreamHandlerResult::Stop;
    if (event.type == HttpStreamEventType::BodyData)
        return self.data(event.data, event.length, response)
            ? HttpStreamHandlerResult::Continue : HttpStreamHandlerResult::Stop;
    if (event.type == HttpStreamEventType::BodyEnd)
        return self.end(response) ? HttpStreamHandlerResult::Continue
                                  : HttpStreamHandlerResult::Stop;
    if (!self.committed_ && self.generation_ != 0U)
        (void)self.bridge_.requestCancel(self.generation_);
    if (!self.responded_ && event.abortReason == HttpStreamAbortReason::ReceiveTimeout)
        (void)self.send(response, 408U, "Update receive timed out");
    return HttpStreamHandlerResult::Stop;
}
