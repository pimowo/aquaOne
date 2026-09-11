#include "SerialDiagnostics.h"

bool SerialDiagnostics::enabled_ = false;

void SerialDiagnostics::begin(bool enabled) { enabled_ = enabled; }
void SerialDiagnostics::setEnabled(bool enabled) { enabled_ = enabled; }
bool SerialDiagnostics::enabled() { return enabled_; }