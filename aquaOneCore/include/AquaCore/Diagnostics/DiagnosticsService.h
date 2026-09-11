#pragma once

#include "AquaCore/Config/StorageService.h"
#include "AquaCore/Diagnostics/DiagnosticsTypes.h"
#include "AquaCore/Network/NetworkService.h"
#include "AquaCore/System/SystemService.h"
#include "AquaCore/Time/NtpService.h"
#include "AquaCore/Time/RtcService.h"

namespace AquaCore {
namespace Diagnostics {

class DiagnosticsService {
public:
    DiagnosticsService(
        const SystemService& system,
        const Time::RtcService& rtc,
        const Config::StorageService& storage,
        const char* timeProviderName,
        const Time::NtpService* ntp = nullptr,
        const Network::NetworkService* network = nullptr
    );

    DiagnosticsSnapshot snapshot() const;

private:
    const SystemService& system_;
    const Time::RtcService& rtc_;
    const Config::StorageService& storage_;
    const Time::NtpService* ntp_;
    const Network::NetworkService* network_;
    char timeProviderName_[TIME_PROVIDER_NAME_CAPACITY] {};

    static HealthState systemHealth(
        const SystemDiagnostics& diagnostics
    );
    static HealthState timeHealth(
        const TimeDiagnostics& diagnostics
    );
    static HealthState storageHealth(
        const StorageDiagnostics& diagnostics
    );
    static HealthState networkHealth(
        const NetworkDiagnostics& diagnostics
    );
    static HealthState worse(
        HealthState first,
        HealthState second
    );
};

} // namespace Diagnostics
} // namespace AquaCore
