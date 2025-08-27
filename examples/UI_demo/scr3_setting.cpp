#include "screen.h"
#include "moon_encoder_ble.h"
#include <NimBLEAdvertisedDevice.h>
#include <lvgl.h>

// Restore these for compatibility (even though we don’t use them anymore)
#define INPUT_TOUCH_ENCODER 0
#define INPUT_ONLY_TOUCH    1
#define INPUT_ONLY_ENCODER  2

// Retain the old globals for compatibility but note they are unused now
static lv_obj_t *setting_obj;
static lv_obj_t *setting_item1;
static lv_obj_t *setting_item2;
static lv_obj_t *setting_item3;
static lv_obj_t *setting_item4;
static lv_obj_t *setting_item5;
static lv_obj_t *setting_item6;

uint16_t brightness_level = 10;
uint16_t input_mode      = INPUT_TOUCH_ENCODER;    // previously INPUT_TOUCH_ENCODER
bool     touch_line_state = false;

void create3(lv_obj_t *parent)
{
    // Show the back button so users can navigate back
    back_btn_hidden(false);

    // Status label: wider font size replaced with an available one
    lv_obj_t *status_label = lv_label_create(parent);
    lv_obj_set_width(status_label, 280);
    lv_label_set_text(status_label, "Searching devices...");
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_color(status_label, lv_color_hex(COLOR_ACCENT1), LV_PART_MAIN);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 40);

    // Register the status label with the BLE logger
    setBleLogLabel(status_label);

    // Perform a blocking scan for MOON devices (5 s). During the scan,
    // updateBleLog() will write progress to this label and to Serial.
    scanForMoonDevices(5);

    // After scanning, show results
    if (foundDevices.empty()) {
        lv_label_set_text(status_label, "No devices found");
    } else {
        lv_label_set_text(status_label, "Devices found:");

        // Create a table for device name and RSSI
        lv_obj_t *table = lv_table_create(parent);
        lv_table_set_col_cnt(table, 2);
        lv_table_set_row_cnt(table, foundDevices.size() + 1);
        lv_table_set_col_width(table, 0, 180);
        lv_table_set_col_width(table, 1, 80);
        lv_table_set_cell_value(table, 0, 0, "Name");
        lv_table_set_cell_value(table, 0, 1, "RSSI");

        for (uint16_t i = 0; i < foundDevices.size(); ++i) {
            NimBLEAdvertisedDevice* dev = foundDevices[i].device;
            const char* name;
            if (dev->haveName()) {
                name = dev->getName().c_str();
            } else {
                name = dev->toString().c_str();  // fallback to MAC
            }
            lv_table_set_cell_value(table, i + 1, 0, name);

            char rssiStr[16];
            snprintf(rssiStr, sizeof(rssiStr), "%d", foundDevices[i].rssi);
            lv_table_set_cell_value(table, i + 1, 1, rssiStr);
        }

        // Use an available font for the table
        lv_obj_set_style_text_font(table, &lv_font_montserrat_14, LV_PART_ITEMS);
        lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);
        lv_obj_set_style_pad_all(table, 4, LV_PART_ITEMS);
        lv_obj_align(table, LV_ALIGN_TOP_MID, 0, 80);
    }
}

void entry3(void)  {}
void exit3(void)   {}
void destroy3(void){ back_btn_hidden(true); }

scr_lifecycle_t scr_setting = {
    .create  = create3,
    .entry   = entry3,
    .exit    = exit3,
    .destroy = destroy3,
};
