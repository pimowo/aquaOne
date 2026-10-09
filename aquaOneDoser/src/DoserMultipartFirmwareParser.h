#pragma once

#include <stddef.h>
#include <stdint.h>

// Fixed, binary-safe parser for the one-file FormData contract of /update.
class DoserMultipartFirmwareParser {
public:
    enum class Event : uint8_t { Start, Data, End };
    using Sink = bool (*)(void*, Event, const uint8_t*, size_t);
    enum class State : uint8_t { Opening, Headers, Data, Closing, Done, Error };

    static bool parseContentType(const char* value, char boundary[71]);
    static bool parseFirmwareSize(const char* value, uint32_t& size);
    static bool validContentLength(size_t body, uint32_t firmwareSize);

    bool begin(const char* boundary);
    bool feed(const uint8_t* bytes, size_t length, Sink sink, void* context);
    bool finish(Sink sink, void* context);
    State state() const { return state_; }
    const char* filename() const { return filename_; }

private:
    bool parseHeaders();
    bool feedData(const uint8_t* bytes, size_t length, Sink sink, void* context);
    bool emitRange(const uint8_t* bytes, size_t from, size_t to,
                   size_t carryLength, Sink sink, void* context);
    static bool equalInsensitive(const char* text, size_t length, const char* literal);

    char boundary_[71] {};
    char filename_[129] {};
    char headers_[513] {};
    uint8_t carry_[78] {};
    uint8_t opening_[74] {};
    uint8_t closing_[78] {};
    uint8_t nextPart_[76] {};
    size_t boundaryLength_ = 0U;
    size_t openingLength_ = 0U;
    size_t closingLength_ = 0U;
    size_t nextPartLength_ = 0U;
    size_t position_ = 0U;
    size_t headerLength_ = 0U;
    size_t carryLength_ = 0U;
    uint8_t closeTail_ = 0U;
    bool endEmitted_ = false;
    State state_ = State::Error;
};
