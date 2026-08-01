#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/spi_common.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_ops.h>

// Include the local copy of the bb_spi_lcd library
#include "bb_spi_lcd.h"

#define TAG "CompactWifiBoardLCD"

// Create a static global instance of the bitbank library driver
static BB_SPI_LCD local_bb_lcd;

class CompactWifiBoardLCD : public WifiBoard {
private:
    Button boot_button_;
    LcdDisplay* display_;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    void InitializeSpi() {
        // Initialize the base SPI physical layer parameters 
        // to allocate the internal DMA host channel channels
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * 3;
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        ESP_LOGI(TAG, "Initializing ILI9488 hardware using bb_spi_lcd...");

        // Fire up the library using the constants defined in your board's config.h
        local_bb_lcd.begin(
            DISPLAY_ILI9488, 
            DISPLAY_MOSI_PIN, 
            -1, 
            DISPLAY_CLK_PIN, 
            DISPLAY_CS_PIN, 
            DISPLAY_DC_PIN, 
            DISPLAY_RST_PIN, 
            DISPLAY_BACKLIGHT_PIN
        );

        local_bb_lcd.setRotation(DISPLAY_SWAP_XY ? 3 : 2);
        local_bb_lcd.fillScreen(0x0000); // Clear layout to pure black

        // Instantiate standard virtual configuration contexts to satisfy 
        // Xiaozhi's base display framework dependencies
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = GPIO_NUM_NC; // Managed directly inside bb_spi_lcd
        io_config.dc_gpio_num = GPIO_NUM_NC; // Managed directly inside bb_spi_lcd
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        
        // Form the structural link handles
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io_));

        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = GPIO_NUM_NC;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
        
        // Allocate a dummy standard frame block
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io_, &panel_config, &panel_));

        // Pass the valid structural configuration objects into Xiaozhi's initialization wrapper
        display_ = new SpiLcdDisplay(panel_io_, panel_,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, 
                                    DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);

        // Fetch the active screen context that was registered by the SpiLcdDisplay block
        lv_display_t* lvgl_disp = lv_display_get_default();
        if (lvgl_disp != nullptr) {
            // Override the default flush callback to redirect rendering through the 18-bit custom driver
            lv_display_set_flush_cb(lvgl_disp, [](lv_display_t * disp, const lv_area_t * area, uint8_t * px_map) {
                int x1 = area->x1;
                int y1 = area->y1;
                int x2 = area->x2;
                int y2 = area->y2;
                
                int width = (x2 - x1) + 1;
                int height = (y2 - y1) + 1;
                
                local_bb_lcd.setAddrWindow(x1, y1, x2, y2);
                local_bb_lcd.pushPixels((uint16_t*)px_map, width * height, 0);
                
                lv_display_flush_ready(disp);
            });
            ESP_LOGI(TAG, "Successfully attached 18-bit color translation pipeline.");
        } else {
            ESP_LOGE(TAG, "Critical: Could not capture active display wrapper!");
        }
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

public:
    CompactWifiBoardLCD() :
        boot_button_(BOOT_BUTTON_GPIO) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }
    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(CompactWifiBoardLCD);
