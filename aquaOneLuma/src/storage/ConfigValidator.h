#pragma once

#include "ConfigTypes.h"

namespace LumaSense {

class ConfigValidator {
 public:
  static bool validate(const DeviceConfig& config);
  static bool validateChannel(const ChannelConfig& channel);
  static bool validateProfile(const Profile& profile);
  static bool validateTank(const TankConfig& tank);
};

}  // namespace LumaSense
