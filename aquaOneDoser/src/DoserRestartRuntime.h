#pragma once

#include "DoserNativeWeb.h"

class PumpDriver;

// Only the restart authority crosses into the native Web Application.
class DoserRestartRuntime final : public DoserRestartAuthority {
public:
    explicit DoserRestartRuntime(PumpDriver& pumps) : pumps_(pumps) {}
    uint32_t nowMs() const override;
    void stopPumps() override;
    void restartDevice() override;

private:
    PumpDriver& pumps_;
};
