#include "DisplayDriver.h"

#include "HardwareConfig.h"

#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "DebugLog.h"
namespace {
constexpr uint16_t COLOR_BLACK = 0x0000;
constexpr size_t TRANSFER_BUFFER_BYTES = 4096;
constexpr uint32_t PIXELS_PER_TRANSFER = TRANSFER_BUFFER_BYTES / 2;
}

bool DisplayDriver::begin() {
  if (initialized_) {
    return true;
  }

  DEBUG_LOGF("[DISPLAY][%lu] DISPLAY_INIT_START\n", millis());

  gpio_config_t outputConfig{};
  outputConfig.mode = GPIO_MODE_OUTPUT;
  outputConfig.pin_bit_mask = (1ULL << DISPLAY_CS) | (1ULL << DISPLAY_DC) |
                              (1ULL << DISPLAY_RST);
  if (gpio_config(&outputConfig) != ESP_OK) {
    return false;
  }

  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_CS), 1);
  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_DC), 1);
  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_RST), 1);

  spi_bus_config_t busConfig{};
  busConfig.mosi_io_num = DISPLAY_MOSI;
  busConfig.miso_io_num = -1;
  busConfig.sclk_io_num = DISPLAY_SCLK;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.max_transfer_sz = TRANSFER_BUFFER_BYTES * 2;

  const esp_err_t busResult = spi_bus_initialize(SPI2_HOST, &busConfig, SPI_DMA_CH_AUTO);
  if (busResult != ESP_OK && busResult != ESP_ERR_INVALID_STATE) {
    return false;
  }

  spi_device_interface_config_t deviceConfig{};
  deviceConfig.clock_speed_hz = DISPLAY_SPI_HZ;
  deviceConfig.mode = DISPLAY_SPI_MODE;
  deviceConfig.spics_io_num = DISPLAY_CS;
  deviceConfig.queue_size = 1;

  spi_device_handle_t device = nullptr;
  if (spi_bus_add_device(SPI2_HOST, &deviceConfig, &device) != ESP_OK) {
    return false;
  }
  spi_ = device;

  // Panel pozostaje ciemny, zanim zresetujemy jego GRAM i rejestry.
  sendCommand(0x28);
  vTaskDelay(pdMS_TO_TICKS(10));

  reset();
  initializeController();
  setRotation(DISPLAY_ROTATION);

  initialized_ = true;
  fillScreen(COLOR_BLACK);
  DEBUG_LOGF("[DISPLAY][%lu] DISPLAY_READY\n", millis());
  return true;
}

void DisplayDriver::fillScreen(uint16_t color) {
  fillRect(0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT, color);
}

void DisplayDriver::fillRect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t color) {
  if (!initialized_ || w == 0 || h == 0) {
    return;
  }

  const int32_t x0 = x < 0 ? 0 : x;
  const int32_t y0 = y < 0 ? 0 : y;
  const int32_t x1 = (static_cast<int32_t>(x) + w) > DISPLAY_WIDTH
                         ? DISPLAY_WIDTH
                         : static_cast<int32_t>(x) + w;
  const int32_t y1 = (static_cast<int32_t>(y) + h) > DISPLAY_HEIGHT
                         ? DISPLAY_HEIGHT
                         : static_cast<int32_t>(y) + h;
  if (x0 >= x1 || y0 >= y1) {
    return;
  }

  setAddrWindow(static_cast<int16_t>(x0), static_cast<int16_t>(y0),
                static_cast<uint16_t>(x1 - x0), static_cast<uint16_t>(y1 - y0));

  const uint8_t high = static_cast<uint8_t>(color >> 8);
  const uint8_t low = static_cast<uint8_t>(color);
  const uint32_t pixels = static_cast<uint32_t>(x1 - x0) * static_cast<uint32_t>(y1 - y0);
  uint32_t remaining = pixels;

  while (remaining > 0) {
    const uint32_t chunkPixels = remaining > PIXELS_PER_TRANSFER ? PIXELS_PER_TRANSFER : remaining;
    for (uint32_t index = 0; index < chunkPixels; ++index) {
      transferBuffer_[index * 2] = high;
      transferBuffer_[index * 2 + 1] = low;
    }
    sendData(transferBuffer_, chunkPixels * 2);
    remaining -= chunkPixels;
  }
}

void DisplayDriver::drawPixel(int16_t x, int16_t y, uint16_t color) {
  if (!initialized_ || x < 0 || y < 0 || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) {
    return;
  }

  setAddrWindow(x, y, 1, 1);
  pushPixels(&color, 1);
}

void DisplayDriver::pushPixels(const uint16_t* pixels, uint32_t count) {
  if (!initialized_ || pixels == nullptr) {
    return;
  }

  while (count > 0) {
    const uint32_t chunkPixels = count > PIXELS_PER_TRANSFER ? PIXELS_PER_TRANSFER : count;
    for (uint32_t index = 0; index < chunkPixels; ++index) {
      transferBuffer_[index * 2] = static_cast<uint8_t>(pixels[index] >> 8);
      transferBuffer_[index * 2 + 1] = static_cast<uint8_t>(pixels[index]);
    }
    sendData(transferBuffer_, chunkPixels * 2);
    pixels += chunkPixels;
    count -= chunkPixels;
  }
}

void DisplayDriver::pushRect(int16_t x, int16_t y, uint16_t w, uint16_t h,
                             const uint16_t* pixels) {
  if (!initialized_ || pixels == nullptr || x < 0 || y < 0 ||
      x + w > DISPLAY_WIDTH || y + h > DISPLAY_HEIGHT || w == 0 || h == 0) {
    return;
  }
  setAddrWindow(x, y, w, h);
  pushPixels(pixels, static_cast<uint32_t>(w) * h);
}

void DisplayDriver::sendCommand(uint8_t command) {
  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_DC), 0);
  spi_transaction_t transaction{};
  transaction.length = 8;
  transaction.tx_buffer = &command;
  spi_device_polling_transmit(static_cast<spi_device_handle_t>(spi_), &transaction);
}

void DisplayDriver::sendData(const uint8_t* data, size_t length) {
  if (length == 0) {
    return;
  }

  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_DC), 1);
  spi_transaction_t transaction{};
  transaction.length = length * 8;
  transaction.tx_buffer = data;
  spi_device_polling_transmit(static_cast<spi_device_handle_t>(spi_), &transaction);
}

void DisplayDriver::sendData8(uint8_t value) {
  sendData(&value, 1);
}

void DisplayDriver::setAddrWindow(int16_t x, int16_t y, uint16_t w, uint16_t h) {
  if (w == 0 || h == 0) {
    return;
  }

  const uint16_t x0 = static_cast<uint16_t>(x + DISPLAY_X_OFFSET);
  const uint16_t x1 = static_cast<uint16_t>(x + DISPLAY_X_OFFSET + w - 1);
  const uint16_t y0 = static_cast<uint16_t>(y + DISPLAY_Y_OFFSET);
  const uint16_t y1 = static_cast<uint16_t>(y + DISPLAY_Y_OFFSET + h - 1);

  const uint8_t column[] = {
      static_cast<uint8_t>(x0 >> 8), static_cast<uint8_t>(x0),
      static_cast<uint8_t>(x1 >> 8), static_cast<uint8_t>(x1),
  };
  const uint8_t row[] = {
      static_cast<uint8_t>(y0 >> 8), static_cast<uint8_t>(y0),
      static_cast<uint8_t>(y1 >> 8), static_cast<uint8_t>(y1),
  };

  sendCommand(0x2A);
  sendData(column, sizeof(column));
  sendCommand(0x2B);
  sendData(row, sizeof(row));
  sendCommand(0x2C);
}

void DisplayDriver::setRotation(uint8_t rotation) {
  sendCommand(0x36);

  uint8_t madctl = 0xC8;
  switch (rotation & 0x03) {
    case 1:
      madctl = 0xA8;
      break;
    case 2:
      madctl = 0x08;
      break;
    case 3:
      madctl = 0x68;
      break;
    default:
      break;
  }
  sendData8(madctl);
}

void DisplayDriver::reset() {
  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_RST), 0);
  vTaskDelay(pdMS_TO_TICKS(10));
  gpio_set_level(static_cast<gpio_num_t>(DISPLAY_RST), 1);
  vTaskDelay(pdMS_TO_TICKS(120));
}

void DisplayDriver::initializeController() {
  const uint8_t frameRate[] = {1, 0x2C, 0x2D};
  const uint8_t frameRatePartial[] = {1, 0x2C, 0x2D, 1, 0x2C, 0x2D};
  const uint8_t powerControl1[] = {0xA2, 2, 0x84};
  const uint8_t powerControl3[] = {0x0A, 0};
  const uint8_t powerControl4[] = {0x8A, 0x2A};
  const uint8_t powerControl5[] = {0x8A, 0xEE};
  const uint8_t positiveGamma[] = {2, 0x1C, 7, 0x12, 0x37, 0x32, 0x29, 0x2D,
                                   0x29, 0x25, 0x2B, 0x39, 0, 1, 3, 0x10};
  const uint8_t negativeGamma[] = {3, 0x1D, 7, 6, 0x2E, 0x2C, 0x29, 0x2D,
                                   0x2E, 0x2E, 0x37, 0x3F, 0, 0, 2, 0x10};

  sendCommand(0x01);
  vTaskDelay(pdMS_TO_TICKS(150));
  sendCommand(0x11);
  vTaskDelay(pdMS_TO_TICKS(150));

  sendCommand(0xB1); sendData(frameRate, sizeof(frameRate));
  sendCommand(0xB2); sendData(frameRate, sizeof(frameRate));
  sendCommand(0xB3); sendData(frameRatePartial, sizeof(frameRatePartial));
  sendCommand(0xB4); sendData8(0x07);
  sendCommand(0xC0); sendData(powerControl1, sizeof(powerControl1));
  sendCommand(0xC1); sendData8(0xC5);
  sendCommand(0xC2); sendData(powerControl3, sizeof(powerControl3));
  sendCommand(0xC3); sendData(powerControl4, sizeof(powerControl4));
  sendCommand(0xC4); sendData(powerControl5, sizeof(powerControl5));
  sendCommand(0xC5); sendData8(0x0E);
  sendCommand(0x20);
  sendCommand(0x3A); sendData8(0x05);
  sendCommand(0xE0); sendData(positiveGamma, sizeof(positiveGamma));
  sendCommand(0xE1); sendData(negativeGamma, sizeof(negativeGamma));
  sendCommand(0x13);
  vTaskDelay(pdMS_TO_TICKS(10));
  sendCommand(0x29);
  vTaskDelay(pdMS_TO_TICKS(100));
}
