#pragma once
#include <stdint.h>
#include "../config/GasSenseConfig.h"
#include "../domain/Types.h"

namespace gassense {

struct BottleRuntime {
    bool active = false;
    uint64_t startedEpoch = 0;
    float diagnosticStartMassKg = 0.0f;
};

class BottleService {
public:
    bool startNewBottle(const GasSenseConfig& cfg,
                        float measuredMassKg,
                        uint64_t nowEpoch);

    const BottleRuntime& runtime() const { return runtime_; }

private:
    BottleRuntime runtime_{};
};

} // namespace gassense
