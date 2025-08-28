#pragma once
#include <lvgl.h>

// Create the overlay once (after LVGL/display/touch init).
void overlay_init();

// Show/hide with a message. Use anywhere.
void overlay_show(const char* msg);   // e.g., overlay_show("Scanning…")
void overlay_hide();

// Optional: you can call overlay_update() every frame if you prefer
// to drive it from BLE state (isScanning()/isConnecting()).
// If you don't use this, just call show/hide directly where needed.
void overlay_update();
