#pragma once

namespace gassense {

// Rozszerzenie wspólnego panelu AquaCore.
// Zakładki: Status / Butla / Kalibracja / Alarmy / Ustawienia / Diagnostyka
class GasSenseWeb {
public:
    void registerPages();
    void update();
};

} // namespace gassense
