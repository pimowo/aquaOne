#pragma once
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"

namespace gassense {

class GasCalculator {
public:
    static GasState calculate(const GasSenseConfig& cfg, const Measurements& m);
};

} // namespace gassense
