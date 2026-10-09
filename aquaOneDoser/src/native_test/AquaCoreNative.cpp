#if !defined(ARDUINO_ARCH_ESP32)
#include "../../../aquaOneCore/src/System/DeviceIdentity.cpp"
#include "../../../aquaOneCore/src/System/RestartReason.cpp"
#include "../../../aquaOneCore/src/System/SystemService.cpp"
#include "../../../aquaOneCore/src/Web/WebTypes.cpp"
#include "../../../aquaOneCore/src/Web/HtmlShell.cpp"
#include "../../../aquaOneCore/src/Web/CoreWebProjectionSources.cpp"
#include "../../../aquaOneCore/src/Web/CoreWebProjectionPublisher.cpp"
#include "../../../aquaOneCore/src/Web/NativeWebService.cpp"
#include "../../../aquaOneCore/src/Web/HttpBasicAuth.cpp"

// The candidate exercises the SystemService source only. The other Core
// adapter shares its translation unit and needs this link-only host stub.
namespace AquaCore { namespace Diagnostics {
DiagnosticsSnapshot DiagnosticsService::snapshot() const { return {}; }
} }
#endif
