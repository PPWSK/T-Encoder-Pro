// scr5_test.cpp — live MOON scan demo using the async snapshot API
#include <lvgl.h>
#include <vector>
#include <string>
#include "moon_encoder_ble.h"

// If your project defines lv_ui, we keep the signature; we don't use 'ui'.
struct lv_ui;

static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_table  = nullptr;
static lv_timer_t* s_timer = nullptr;
static std::vector<MoonDeviceInfo> s_snapshot;

static void fill_table() {
    if (!s_table) return;

    lv_table_set_col_cnt(s_table, 2);
    lv_table_set_row_cnt(s_table, s_snapshot.size() + 1);
    lv_table_set_cell_value(s_table, 0, 0, "Name / MAC");
    lv_table_set_cell_value(s_table, 0, 1, "RSSI");

    for (uint16_t i = 0; i < s_snapshot.size(); ++i) {
        const auto& d = s_snapshot[i];
        lv_table_set_cell_value(s_table, i + 1, 0, d.name.c_str());
        char r[16]; snprintf(r, sizeof(r), "%d", d.rssi);
        lv_table_set_cell_value(s_table, i + 1, 1, r);
    }

    if (s_status) {
        if (s_snapshot.empty())
            lv_label_set_text(s_status, isScanning() ? "Scanning…" : "No MOONs found");
        else
            lv_label_set_text(s_status, isScanning() ? "Scanning… (tap to connect)" : "Tap a MOON to connect");
    }
}

static void on_table_click(lv_event_t *e) {
    lv_obj_t *tbl = lv_event_get_target(e);
    uint16_t row = LV_TABLE_CELL_NONE, col = LV_TABLE_CELL_NONE;
    lv_table_get_selected_cell(tbl, &row, &col);
    if (row == LV_TABLE_CELL_NONE || row == 0) return;

    uint16_t idx = row - 1;
    if (idx >= s_snapshot.size()) return;

    const auto &sel = s_snapshot[idx];
    connectToAddress(sel.mac);   // keep UI here; connect logs via Serial
}

static void timer_cb(lv_timer_t *) {
    // Pump one pending status line (optional)
    String line;
    if (blePopUiLog(line) && s_status) lv_label_set_text(s_status, line.c_str());

    // Refresh table when the device DB changed
    std::vector<MoonDeviceInfo> now;
    if (bleCopySnapshot(now)) {
        s_snapshot.swap(now);
        fill_table();
    }
}

void scr5_test(lv_ui *ui) {
    LV_UNUSED(ui);

    lv_obj_t* parent = lv_scr_act();

    // Status label
    s_status = lv_label_create(parent);
    lv_obj_set_width(s_status, 280);
    lv_label_set_text(s_status, "Preparing scan…");
    lv_obj_set_style_text_align(s_status, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(s_status, &lv_font_montserrat_18, LV_PART_MAIN);
    lv_obj_align(s_status, LV_ALIGN_TOP_MID, 0, 24);

    // Table
    s_table = lv_table_create(parent);
    lv_table_set_col_cnt(s_table, 2);
    lv_table_set_row_cnt(s_table, 1);
    lv_table_set_col_width(s_table, 0, 160);
    lv_table_set_col_width(s_table, 1, 60);
    lv_obj_set_style_text_font(s_table, &lv_font_montserrat_14, LV_PART_ITEMS);
    lv_obj_align(s_table, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_add_flag(s_table, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_table, on_table_click, LV_EVENT_CLICKED, NULL);

    // Start non-blocking scan and UI refresh
    startScanAsync(10);
    if (s_timer) lv_timer_del(s_timer);
    s_timer = lv_timer_create(timer_cb, 500, NULL);

    s_snapshot.clear();
    fill_table();
}
