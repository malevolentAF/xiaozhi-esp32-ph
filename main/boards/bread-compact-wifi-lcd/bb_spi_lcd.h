#ifndef BB_SPI_LCD_H
#define BB_SPI_LCD_H

#include <stdint.h>
#include <driver/gpio.h>
#include <driver/spi_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define DISPLAY_ILI9488 4

class BB_SPI_LCD {
private:
    int _type;
    int _mosi, _miso, _sclk, _cs, _dc, _rst, _bl;
    spi_device_handle_t _spi;

    void writeCommand(uint8_t c);
    void writeData(uint8_t *d, int len);

public:
    BB_SPI_LCD();
    int begin(int iType, int iMOSI, int iMISO, int iSCLK, int iCS, int iDC, int iRST, int iBL);
    void setAddrWindow(int x, int y, int x2, int y2);
    void pushPixels(uint16_t *pPixels, int iCount, int bSwap);
    void fillScreen(uint16_t color);
    void setRotation(int r);
};

#endif // BB_SPI_LCD_H
