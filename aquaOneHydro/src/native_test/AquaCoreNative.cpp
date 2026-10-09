#if !defined(ARDUINO_ARCH_ESP32)

// The product host suite links only the portable Core units used by the
// native Web composition. Production ESP32 builds compile this file empty.
#include "../../../aquaOneCore/src/System/DeviceIdentity.cpp"
#include "../../../aquaOneCore/src/System/RestartReason.cpp"
#include "../../../aquaOneCore/src/System/SystemService.cpp"
#include "../../../aquaOneCore/src/Network/NetworkService.cpp"
#include "../../../aquaOneCore/src/Web/WebTypes.cpp"
#include "../../../aquaOneCore/src/Web/HtmlShell.cpp"
#include "../../../aquaOneCore/src/Web/NativeWebService.cpp"

#endif
