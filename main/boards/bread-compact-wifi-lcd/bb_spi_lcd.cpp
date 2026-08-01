#include "bb_spi_lcd.h"
#include <string.h>
#include <esp_log.h>

#define TAG "BB_SPI_LCD"

BB_SPI_LCD::BB_SPI_LCD() {
    _mosi = _miso = _sclk = _cs = _dc = _rst = _bl = -1;
}

int BB_SPI_LCD::begin(int iType, int iMOSI, int iMISO, int iSCLK, int iCS, int iDC, int iRST, int iBL) {
    _type = iType;
    _mosi = iMOSI;
    _miso = iMISO;
    _sclk = iSCLK;
    _cs = iCS;
    _dc = iDC;
    _rst = iRST;
    _bl = iBL;

    // Initialize display control GPIOs
    if (_cs >= 0) {
        gpio_reset_pin((gpio_num_t)_cs);
        gpio_set_direction((gpio_num_t)_cs, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)_cs, 1);
    }
    if (_dc >= 0) {
        gpio_reset_pin((gpio_num_t)_dc);
        gpio_set_direction((gpio_num_t)_dc, GPIO_MODE_OUTPUT);
    }
    if (_rst >= 0) {
        gpio_reset_pin((gpio_num_t)_rst);
        gpio_set_direction((gpio_num_t)_rst, GPIO_MODE_OUTPUT);
        gpio_set_level((gpio_num_t)_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(10));
        gpio_set_level((gpio_num_t)_rst, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
        gpio_set_level((gpio_num_t)_rst, 1);
        vTaskDelay(pdMS_TO_TICKS(120));
    }

    // Set up SPI Bus parameters
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = _mosi;
    buscfg.miso_io_num = _miso;
    buscfg.sclk_io_num = _sclk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 320 * 480 * 3; // Max frame length for 18-bit pixel bursts
    spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO);

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 40 * 1000 * 1000; // Fast 40MHz clock speed
    devcfg.mode = 0;
    devcfg.spics_io_num = -1; // Managed manually via standard GPIO toggling
    devcfg.queue_size = 7;
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    spi_bus_add_device(SPI3_HOST, &devcfg, &_spi);

    // Physical Initialization Commands targeting the ILI9488 register bank
    writeCommand(0xE0); // Positive Gamma Control
    uint8_t g1[] = {0x00, 0x03, 0x09, 0x08, 0x16, 0x0A, 0x3F, 0x78, 0x4C, 0x09, 0x0A, 0x08, 0x16, 0x1A, 0x0F};
    writeData(g1, 15);

    writeCommand(0XE1); // Negative Gamma Control
    uint8_t g2[] = {0x00, 0x16, 0x19, 0x03, 0x0F, 0x05, 0x32, 0x45, 0x46, 0x04, 0x0E, 0x0D, 0x35, 0x37, 0x0F};
    writeData(g2, 15);

    writeCommand(0xC0); // Power Control 1
    uint8_t p1[] = {0x17, 0x15};
    writeData(p1, 2);

    writeCommand(0xC1); // Power Control 2
    uint8_t p2[] = {0x41};
    writeData(p2, 1);

    writeCommand(0xC5); // VCOM Control
    uint8_t vcom[] = {0x00, 0x12, 0x80};
    writeData(vcom, 3);

    writeCommand(0x36); // Memory Access Control
    uint8_t madctl[] = {0x48}; // MX, BGR byte color ordering
    writeData(madctl, 1);

    writeCommand(0x3A); // Interface Pixel Format
    uint8_t pixel[] = {0x66}; // Crucial configuration step: Forces 18-bit color mode over standard SPI
    writeData(pixel, 1);

    writeCommand(0xB0); // Interface Mode Control
    uint8_t imc[] = {0x00};
    writeData(imc, 1);

    writeCommand(0xB1); // Frame Rate Control
    uint8_t frc[] = {0xA0};
    writeData(frc, 1);

    writeCommand(0xB4); // Display Inversion Control
    uint8_t dic[] = {0x02};
    writeData(dic, 1);

    writeCommand(0xB6); // Display Function Control
    uint8_t dfc[] = {0x02, 0x02, 0x3B};
    writeData(dfc, 3);

    writeCommand(0xE9); // Set Image Function
    uint8_t img[] = {0x00};
    writeData(img, 1);

    writeCommand(0xF7); // Adjust Control
    uint8_t adj[] = {0xA9, 0x51, 0x2C, 0x82};
    writeData(adj, 4);

    writeCommand(0x11); // Exit Sleep mode
    vTaskDelay(pdMS_TO_TICKS(120));

    writeCommand(0x29); // Display ON
    return 1;
}

void BB_SPI_LCD::writeCommand(uint8_t c) {
    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 0);
    gpio_set_level((gpio_num_t)_dc, 0); // Enter Command Mode
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &c;
    spi_device_polling_transmit(_spi, &t);
    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 1);
}

void BB_SPI_LCD::writeData(uint8_t *d, int len) {
    if (len == 0) return;
    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 0);
    gpio_set_level((gpio_num_t)_dc, 1); // Enter Data Mode
    spi_transaction_t t = {};
    t.length = len * 8;
    t.tx_buffer = d;
    spi_device_polling_transmit(_spi, &t);
    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 1);
}

void BB_SPI_LCD::setAddrWindow(int x, int y, int x2, int y2) {
    uint8_t x_buf[] = { (uint8_t)(x >> 8), (uint8_t)(x & 0xFF), (uint8_t)(x2 >> 8), (uint8_t)(x2 & 0xFF) };
    uint8_t y_buf[] = { (uint8_t)(y >> 8), (uint8_t)(y & 0xFF), (uint8_t)(y2 >> 8), (uint8_t)(y2 & 0xFF) };

    writeCommand(0x2A); // Column Target Window allocation
    writeData(x_buf, 4);
    writeCommand(0x2B); // Row Target Window allocation
    writeData(y_buf, 4);
    writeCommand(0x2C); // Write to layout memory sequence
}

void BB_SPI_LCD::pushPixels(uint16_t *pPixels, int iCount, int bSwap) {
    if (iCount == 0) return;
    
    // Allocate 3 bytes per pixel to safely split 16-bit RGB565 to 18-bit RGB666
    int outLen = iCount * 3;
    uint8_t *outBuf = (uint8_t *)malloc(outLen);
    if (!outBuf) return;

    for (int i = 0; i < iCount; i++) {
        uint16_t color = pPixels[i];
        if (bSwap) {
            color = (color >> 8) | (color << 8);
        }
        // Extract 5-6-5 bits and map them to 6-6-6 bits stretched across 3 full bytes
        outBuf[i * 3]     = ((color >> 11) & 0x1F) << 3; // Red
        outBuf[i * 3 + 1] = ((color >> 5) & 0x3F) << 2;  // Green
        outBuf[i * 3 + 2] = (color & 0x1F) << 3;         // Blue
    }

    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 0);
    gpio_set_level((gpio_num_t)_dc, 1);

    spi_transaction_t t = {};
    t.length = outLen * 8;
    t.tx_buffer = outBuf;
    spi_device_polling_transmit(_spi, &t);

    if (_cs >= 0) gpio_set_level((gpio_num_t)_cs, 1);
    free(outBuf);
}

void BB_SPI_LCD::fillScreen(uint16_t color) {
    setAddrWindow(0, 0, 319, 479);
    int chunk = 320;
    uint16_t *buf = (uint16_t *)malloc(chunk * sizeof(uint16_t));
    if (!buf) return;
    for (int i = 0; i < chunk; i++) buf[i] = color;
    
    for (int y = 0; y < 480; y++) {
        pushPixels(buf, chunk, 0);
    }
    free(buf);
}

void BB_SPI_LCD::setRotation(int r) {
    writeCommand(0x36);
    uint8_t madctl = 0x48; // Default Portrait orientation rules
    if (r == 1) madctl = 0x28; // Landscape
    if (r == 2) madctl = 0x88; // Inverse Portrait
    if (r == 3) madctl = 0xE8; // Inverse Landscape
    writeData(&madctl, 1);
}
