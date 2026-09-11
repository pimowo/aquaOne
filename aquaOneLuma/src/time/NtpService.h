#pragma once

#include "AquaCore/Time/NtpService.h"
#include "../../include/Constants.h"
#include "RtcService.h"
#include "TimeTypes.h"

namespace LumaSense {

using NtpBackendResult = AquaCore::Time::NtpBackendResult;
using NtpBackend = AquaCore::Time::NtpBackend;
using EspNtpBackend = AquaCore::Time::EspNtpBackend;
using NtpConfig = AquaCore::Time::NtpConfig;
using NtpService = AquaCore::Time::NtpService;

} // namespace LumaSense