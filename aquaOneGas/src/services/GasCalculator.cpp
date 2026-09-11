#include "services/GasCalculator.h"
#include <algorithm>

namespace gassense {

GasState GasCalculator::calculate(const GasSenseConfig& cfg, const Measurements& m) {
    GasState s;

    s.dryMassKg = cfg.bottleTareKg + cfg.equipmentMassKg;

    float remaining = m.filteredMassKg - s.dryMassKg;
    remaining = std::max(0.0f, std::min(remaining, cfg.nominalCo2Kg));

    s.co2RemainingKg = remaining;
    s.co2RemainingPercent =
        cfg.nominalCo2Kg > 0.0f ? (remaining / cfg.nominalCo2Kg) * 100.0f : 0.0f;
    s.co2UsedKg = std::max(0.0f, cfg.nominalCo2Kg - remaining);

    // TODO: pełna maszyna stanów, hysteresis i timery potwierdzające.
    s.co2Low = s.co2RemainingPercent <= cfg.lowPercent;
    s.co2Critical = s.co2RemainingPercent <= cfg.criticalPercent;

    if (s.co2RemainingKg <= cfg.emptyKg) {
        s.bottle = BottleState::Empty;
    } else if (s.co2Critical) {
        s.bottle = BottleState::Critical;
    } else if (s.co2Low) {
        s.bottle = BottleState::Low;
    } else {
        s.bottle = BottleState::Ok;
    }

    return s;
}

}
