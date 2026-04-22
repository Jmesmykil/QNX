#pragma once
#include <imgui.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace qos {

struct TileEntry {
    ImTextureID icon = 0;     // 0 = draw placeholder
    std::string label;        // displayed under tile; auto-truncated to fit
    uint64_t    app_id = 0;   // opaque tag (TID or NRO path hash)
};

using TileActivateCb = std::function<void(const TileEntry &entry)>;

struct TileGridOpts {
    int   cols         = 7;
    int   rows_visible = 2;
    float tile_size_px = 180.0f;   // 180x180 per tile (icon area)
    float tile_gap_px  = 16.0f;
    float label_h_px   = 24.0f;
    ImU32 tile_bg      = IM_COL32(24, 28, 40, 220);
    ImU32 tile_border  = IM_COL32(72, 92, 140, 255);
    ImU32 tile_focus   = IM_COL32(255, 255, 255, 255);
    ImU32 label_color  = IM_COL32(220, 230, 255, 255);
};

class TileGrid {
public:
    void SetTiles(std::vector<TileEntry> tiles);
    void SetOptions(const TileGridOpts &opts);
    void SetActivateCallback(TileActivateCb cb);

    // Call once per frame inside an ImGui::Begin window. Returns true if a
    // tile was activated this frame (callback also invoked).
    bool Render();

    // Called by main loop to push navigation input — returns true if the
    // input was consumed (pressed A/activate this frame).
    bool OnInput(bool up, bool down, bool left, bool right, bool activate_a, bool back_b);

    int  GetCursorIndex() const;
    void SetCursorIndex(int idx);

private:
    std::vector<TileEntry> tiles_;
    TileGridOpts           opts_;
    TileActivateCb         activate_cb_;
    int                    cursor_ = 0;
    int                    scroll_row_ = 0;
};

} // namespace qos
