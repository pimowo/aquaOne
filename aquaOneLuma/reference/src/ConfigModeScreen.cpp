#include "ConfigModeScreen.h"

#include "DisplayDriver.h"
#include "YoRadioFont.h"
#include "HardwareConfig.h"

namespace {
constexpr uint16_t BLACK = 0x0000, WHITE = 0xFFFF, YELLOW = 0xFFE0, GREEN = 0x07E0, RED = 0xF800;
constexpr uint8_t GLYPH_ADVANCE = 6;
}

void ConfigModeScreen::show() {
  display_.fillScreen(BLACK);
  drawText(43, 4, "yo", 1, WHITE);
  drawText(55, 4, "PILOT", 1, GREEN);
  drawCentered(20, "KONFIGURACJA", 1, YELLOW);
  drawText(8, 48, "SSID: yoPILOT", 1, WHITE);
  drawText(8, 66, "PASS: 12345987", 1, WHITE);
  drawText(8, 84, "IP: 192.168.4.1", 1, WHITE);
  clientStatusKnown_ = false;
}

void ConfigModeScreen::updateClientStatus(bool connected) {
  if (clientStatusKnown_ && connected == clientConnected_) return;
  clientStatusKnown_ = true;
  clientConnected_ = connected;
  display_.fillRect(0, 112, DISPLAY_WIDTH, 12, BLACK);
  drawCentered(114, connected ? String(u8"Połączony") : String(u8"Brak połączenia"), 1,
               connected ? GREEN : RED);
}

void ConfigModeScreen::drawText(int16_t x, int16_t y, const String& text, uint8_t scale,
                                uint16_t color) {
  for (size_t i = 0; i < text.length();) {
    uint8_t glyphCode = '?';
    const uint8_t first = static_cast<uint8_t>(text[i++]);
    if (first < 0x80) {
      glyphCode = first;
    } else if (i < text.length()) {
      const uint8_t second = static_cast<uint8_t>(text[i++]);
      if (first == 0xC5 && second == 0x82) glyphCode = 0xCF;       // ł
      else if (first == 0xC4 && second == 0x85) glyphCode = 0xB8;  // ą
      else if (first == 0xC4 && second == 0x99) glyphCode = 0xD6;  // ę
    }
    const YoRadioGlyph& glyph = yoRadioGlyph(glyphCode);
    for (uint8_t col = 0; col < 5; ++col) {
      for (uint8_t row = 0; row < 8; ++row) {
        if (glyph.columns[col] & (1U << row)) {
          display_.fillRect(x + col * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += GLYPH_ADVANCE * scale;
  }
}

void ConfigModeScreen::drawCentered(int16_t y, const String& text, uint8_t scale,
                                    uint16_t color) {
  uint8_t glyphCount = 0;
  for (size_t i = 0; i < text.length(); ++i, ++glyphCount) {
    if (static_cast<uint8_t>(text[i]) >= 0x80 && i + 1 < text.length()) ++i;
  }
  drawText((DISPLAY_WIDTH - glyphCount * GLYPH_ADVANCE * scale) / 2, y, text, scale, color);
}
