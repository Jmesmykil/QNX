// Copyright (c) XorTroll. GPLv2.
// QOS-PATCH-005: DockElement — Finder-style magnifying dock with sigmoid hover-scale.
// Hover zones: focused=1.4x, adjacent=1.2x, neighbor+1=1.05x, far=1.0x.
// Emits EVENT UX_DOCK_MAGNIFY via UL_LOG_INFO on each scale change.
// Added for Q OS v0.3.0 UX port.

#pragma once
#include <pu/Plutonium>
#include <vector>
#include <functional>

namespace ul::menu::ui {

    // QOS-PATCH-005: Dock magnification constants
    static constexpr float DockScaleFocused   = 1.4f;
    static constexpr float DockScaleAdjacent  = 1.2f;
    static constexpr float DockScaleNeighbor2 = 1.05f;
    static constexpr float DockScaleFar       = 1.0f;

    // QOS-PATCH-005: Frame steps for sigmoid scale interpolation
    static constexpr u32 DockScaleIncrementSteps = 8;

    // QOS-PATCH-005: Base slot size in pixels
    static constexpr u32 DockBaseSlotSize = 72;

    // QOS-PATCH-005: Vertical padding from the bottom of the screen
    static constexpr u32 DockBottomMargin = 12;

    // QOS-PATCH-005: Horizontal slot margin
    static constexpr u32 DockSlotHMargin = 8;

    // QOS-PATCH-005: Dock background padding (each side)
    static constexpr u32 DockBgPad = 10;

    // QOS-PATCH-005: Each slot in the dock
    struct DockSlot {
        pu::sdl2::TextureHandle::Ref icon;   // Icon texture (may be null)
        float target_scale;                  // Scale target this frame drives toward
        float cur_scale;                     // Current rendered scale (interpolated)
        pu::ui::SigmoidIncrementer<s32> scale_pixel_incr; // Pixel-space incr driving cur_scale
        s32 cur_scale_pixels;               // cur_scale expressed as pixel size (for SigmoidIncrementer)
    };

    // QOS-PATCH-005: DockElement — horizontal strip of app icons with magnification.
    // Lives at the bottom-centre of MainMenuLayout.
    // Only renders what's given via AddSlot(); hover zone determined by focused index
    // set each frame via UpdateHoverZone().
    class DockElement : public pu::ui::elm::Element {
        private:
            s32 elem_x;
            s32 elem_y;
            u32 elem_w;
            u32 elem_h;
            std::vector<DockSlot> slots;
            s32 focused_slot_idx;   // -1 = no slot focused

            // QOS-PATCH-005: Compute target scale for slot i given focused index
            inline float ComputeTargetScale(const s32 slot_i) const {
                if(this->focused_slot_idx < 0) {
                    return DockScaleFar;
                }
                const auto dist = std::abs(slot_i - this->focused_slot_idx);
                switch(dist) {
                    case 0:  return DockScaleFocused;
                    case 1:  return DockScaleAdjacent;
                    case 2:  return DockScaleNeighbor2;
                    default: return DockScaleFar;
                }
            }

        public:
            // QOS-PATCH-005
            DockElement(const s32 x, const s32 y, const u32 w, const u32 h);
            PU_SMART_CTOR(DockElement)

            inline s32 GetX() override { return this->elem_x; }
            inline s32 GetY() override { return this->elem_y; }
            inline s32 GetWidth() override { return static_cast<s32>(this->elem_w); }
            inline s32 GetHeight() override { return static_cast<s32>(this->elem_h); }

            inline void SetX(const s32 x) { this->elem_x = x; }
            inline void SetY(const s32 y) { this->elem_y = y; }

            // QOS-PATCH-005: Add an icon slot.  Must be called before first render.
            void AddSlot(pu::sdl2::TextureHandle::Ref icon);

            // QOS-PATCH-005: Call each frame from OnMenuInput with the current D-pad
            // focused index (0-based into the dock slot array).  Pass -1 when the dock
            // is not focused at all.
            void UpdateHoverZone(const s32 new_focused_idx);

            // QOS-PATCH-005: Returns number of registered slots
            inline u32 GetSlotCount() const {
                return static_cast<u32>(this->slots.size());
            }

            void OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 x, const s32 y) override;
            void OnInput(const u64 keys_down, const u64 keys_up, const u64 keys_held, const pu::ui::TouchPoint touch_pos) override;
    };

}
