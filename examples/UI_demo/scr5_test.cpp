/*
 * Custom Test screen for the T‑Encoder‑Pro (screen 5 in the V2.0.14 firmware).
 *
 * This screen replaces the default pass/fail button matrix with a BLE
 * device scanner. When entered from the home menu it will perform a
 * synchronous BLE scan using the functions in moon_encoder_ble.cpp and
 * display the results in a two column table. If no MOON devices are
 * discovered the screen will show a message indicating this fact.
 *
 * To hook this screen into your existing project replace the contents of
 * scr5_test.cpp in the examples/Lvgl_CIT folder (or your 2.0.14
 * equivalent) with this file. You must also ensure that
 * moon_encoder_ble.h/cpp are compiled as part of your Arduino project.
 */

#include "lvgl.h"
#include <stdio.h>
#include "ui.h"
#include "moon_encoder_ble.h"

void scr5_test(lv_ui *ui)
{
    // Create a fresh screen and set its size to the full display. If your
    // project uses CIT_UI.LCD_Width/Height these globals can be used to
    // size the screen; otherwise adjust as needed.
    ui->Test = lv_obj_create(NULL);
    lv_obj_set_size(ui->Test, CIT_UI.LCD_Width, CIT_UI.LCD_Height);
    lv_obj_set_scrollbar_mode(ui->Test, LV_SCROLLBAR_MODE_OFF);
    // Set a white background
    lv_obj_set_style_bg_opa(ui->Test, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(ui->Test, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);

    // Create a label at the top of the screen for status messages. This
    // will show scan progress and is also wired into the BLE module via
    // setBleLogLabel() so that log messages from the BLE code are
    // forwarded here automatically.
    lv_obj_t *log_label = lv_label_create(ui->Test);
    lv_obj_set_width(log_label, CIT_UI.LCD_Width);
    lv_obj_set_height(log_label, 20);
    lv_label_set_long_mode(log_label, LV_LABEL_LONG_WRAP);
    lv_obj_align(log_label, LV_ALIGN_TOP_MID, 0, 4);
    lv_label_set_text(log_label, "Scanning...");

    // Register the label with the BLE module so messages are displayed
    setBleLogLabel(log_label);

    // Perform a synchronous scan for MOON devices. This call blocks for
    // the specified duration. All discovered devices are stored in
    // foundDevices.
    scanForMoonDevices(5);

    // After scanning, check whether any devices were found. If not,
    // display an informative message. Otherwise build a table of device
    // names and RSSI values.
    if (foundDevices.empty()) {
        lv_label_set_text(log_label, "No MOON devices found");
    } else {
        // Create a table to list devices. Two columns: name and RSSI.
        lv_obj_t *table = lv_table_create(ui->Test);
        // Position the table below the log label
        lv_obj_set_pos(table, 0, 30);
        lv_obj_set_size(table, CIT_UI.LCD_Width, CIT_UI.LCD_Height - 30);
        lv_table_set_col_cnt(table, 2);
        lv_table_set_row_cnt(table, foundDevices.size());
        // Adjust column widths: name column wider than RSSI
        lv_table_set_col_width(table, 0, (CIT_UI.LCD_Width * 2) / 3);
        lv_table_set_col_width(table, 1, CIT_UI.LCD_Width / 3);
        // Populate table rows
        for (size_t i = 0; i < foundDevices.size(); ++i) {
            MoonDeviceInfo &info = foundDevices[i];
            const char *name;
            if (info.device->haveName()) {
                name = info.device->getName().c_str();
            } else {
                // Use the address string if no name is advertised
                name = info.device->toString().c_str();
            }
            // Set device name in first column
            lv_table_set_cell_value(table, i, 0, name);
            // Format RSSI as a string
            char rssi_buf[16];
            snprintf(rssi_buf, sizeof(rssi_buf), "%d", info.rssi);
            // Set RSSI in second column
            lv_table_set_cell_value(table, i, 1, rssi_buf);
        }
    }

    // Update layout to ensure all children are properly positioned
    lv_obj_update_layout(ui->Test);
    // Mark this screen as created so events_init can reuse/destroy it
    ui->Test_del = false;
}