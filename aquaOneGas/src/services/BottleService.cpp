#include "services/BottleService.h"

namespace gassense {

bool BottleService::startNewBottle(const GasSenseConfig&,
                                   float measuredMassKg,
                                   uint64_t nowEpoch) {
    runtime_.active = true;
    runtime_.startedEpoch = nowEpoch;
    runtime_.diagnosticStartMassKg = measuredMassKg;
    return true;
}

}
