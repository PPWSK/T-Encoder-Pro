#include "screen.h"
#include "moon_encoder_ble.h"
#include <lvgl.h>

// Keep these for the demo codebase
#define INPUT_TOUCH_ENCODER 0
#define INPUT_ONLY_TOUCH    1
#define INPUT_ONLY_ENCODER  2
uint16_t brightness_level = 10;
uint16_t input_mode      = INPUT_TOUCH_ENCODER;
bool     touch_line_state = false;

static lv_obj_t *g_status = nullptr;
static lv_obj_t *g_table  = nullptr;
static lv_timer_t *g_timer = nullptr;
static std::vector<MoonDeviceInfo> g_snap;

// Ensure table reacts to a light tap
static void enable_easy_tap(lv_obj_t* table){
    lv_obj_add_flag(table, LV_OBJ_FLAG_CLICKABLE);
    // In v8: enable single-cell selection so a tap selects the tapped cell
    #ifdef LV_TABLE_CELL_SELECT_MODE_SINGLE
    lv_table_set_cell_select_mode(table, LV_TABLE_CELL_SELECT_MODE_SINGLE);
    #endif
}

static void rebuild_table(bool includeRescanRow = true) {
    if (!g_table) return;

    // +1 header; +1 optional "Rescan" tail row
    uint16_t dataRows = g_snap.size();
    uint16_t rows = 1 + dataRows + (includeRescanRow ? 1 : 0);

    lv_table_set_col_cnt(g_table, 2);
    lv_table_set_row_cnt(g_table, rows);

    lv_table_set_cell_value(g_table, 0, 0, "Name / MAC");
    lv_table_set_cell_value(g_table, 0, 1, "✔");

    for (uint16_t i=0;i<dataRows;++i){
        const auto &d = g_snap[i];
        lv_table_set_cell_value(g_table, i+1, 0, d.name.c_str());
        lv_table_set_cell_value(g_table, i+1, 1, d.connected ? "v" : "");
    }

    // Last row: "Rescan (5s)" — always visible, easy to click
    if (includeRescanRow){
        uint16_t rr = rows - 1;
        lv_table_set_cell_value(g_table, rr, 0, isScanning() ? "Stop scanning" : "Rescan (5s)");
        lv_table_set_cell_value(g_table, rr, 1, "");
    }

    // Status line
    if (isConnected()) {
        String s = "Connected to "; s += connectedMac().c_str();
        lv_label_set_text(g_status, s.c_str());
    } else if (isScanning()) {
        lv_label_set_text(g_status, "Scanning… (tap a MOON to connect)");
    } else if (g_snap.empty()) {
        lv_label_set_text(g_status, "No MOONs found");
    } else {
        lv_label_set_text(g_status, "Tap a MOON to connect");
    }
}

static void on_table_click(lv_event_t *e){
    lv_obj_t *tbl = lv_event_get_target(e);

    // Get the selected cell (tap should select it)
    uint16_t row = LV_TABLE_CELL_NONE, col = LV_TABLE_CELL_NONE;
    lv_table_get_selected_cell(tbl, &row, &col);
    if (row == LV_TABLE_CELL_NONE) return;

    uint16_t dataRows = g_snap.size();
    uint16_t lastRow  = 1 + dataRows; // header + data -> rescan row index

    // If user tapped the last row => Rescan/Stop
    if (row == lastRow) {
        if (isScanning()) stopScan();
        else startScanAsync(5);           // requested: 5s rescan
        rebuild_table();                  // update tail caption immediately
        return;
    }

    // Ignore header row
    if (row == 0) return;

    // Otherwise: connect to that device (soft tap)
    uint16_t idx = row - 1;
    if (idx >= g_snap.size()) return;

    // For reliability: stop any ongoing scan
    if (isScanning()) stopScan();

    connectToAddress(g_snap[idx].mac);

    // Update UI immediately
    std::vector<MoonDeviceInfo> now;
    if (bleCopySnapshot(now)) g_snap.swap(now);
    rebuild_table();
}

static void tick_cb(lv_timer_t *){
    // Pump one pending UI line if any
    String line; if (blePopUiLog(line)) lv_label_set_text(g_status, line.c_str());

    // Refresh rows when snapshot changed
    std::vector<MoonDeviceInfo> now;
    if (bleCopySnapshot(now)) {
        g_snap.swap(now);
        rebuild_table();
    }

    // Keep footer caption current while scanning
    rebuild_table();
}

void create3(lv_obj_t *parent)
{
    back_btn_hidden(false);

    // Status label (top)
    g_status = lv_label_create(parent);
    lv_obj_set_width(g_status, 280);
    lv_label_set_text(g_status, "Settings");
    lv_obj_set_style_text_align(g_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(g_status, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(g_status, LV_ALIGN_TOP_MID, 0, 16);

    // Device table (tap to select/connect, last row = rescan)
    g_table = lv_table_create(parent);
    lv_table_set_col_cnt(g_table, 2);
    lv_table_set_row_cnt(g_table, 1);
    lv_table_set_col_width(g_table, 0, 180);
    lv_table_set_col_width(g_table, 1, 40);
    lv_obj_set_style_text_font(g_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_align(g_table, LV_ALIGN_TOP_MID, 0, 52);
    enable_easy_tap(g_table);
    lv_obj_add_event_cb(g_table, on_table_click, LV_EVENT_CLICKED, NULL);

    // Start a short scan automatically when opening if not connected
    if (!isConnected()) startScanAsync(2);   // <= 2s per your ask

    // Refresh timer (fast but light)
    if (g_timer) lv_timer_del(g_timer);
    g_timer = lv_timer_create(tick_cb, 300, NULL);

    g_snap.clear();
    rebuild_table(); // draw headers and tail row instantly
}

void entry3(void)  {}
void exit3(void)   {}
void destroy3(void){
    back_btn_hidden(true);
    if (g_timer){ lv_timer_del(g_timer); g_timer=nullptr; }
}
scr_lifecycle_t scr_setting = {
    .create  = create3,
    .entry   = entry3,
    .exit    = exit3,
    .destroy = destroy3,
};
