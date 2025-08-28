#include "ui_overlay.h"
#include "moon_encoder_ble.h"   // for optional overlay_update

static lv_obj_t* s_overlay = nullptr;
static lv_obj_t* s_label   = nullptr;

void overlay_init(){
    if (s_overlay) return;
    s_overlay = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_40, 0);
    lv_obj_align(s_overlay, LV_ALIGN_CENTER, 0, 0);

    s_label = lv_label_create(s_overlay);
    lv_label_set_text(s_label, "");
    lv_obj_align(s_label, LV_ALIGN_CENTER, 0, 0);

    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);         // make sure it's on top
}

void overlay_show(const char* msg){
    if (!s_overlay) overlay_init();
    if (s_label && msg) lv_label_set_text(s_label, msg);
    lv_obj_clear_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_overlay);
}

void overlay_hide(){
    if (s_overlay) lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_HIDDEN);
}

// Optional policy-driven overlay (auto shows on BLE busy)
void overlay_update(){
    if (isScanning())      overlay_show("Scanning…");
    else if (isConnecting()) overlay_show("Connecting…");
    else overlay_hide();
}
