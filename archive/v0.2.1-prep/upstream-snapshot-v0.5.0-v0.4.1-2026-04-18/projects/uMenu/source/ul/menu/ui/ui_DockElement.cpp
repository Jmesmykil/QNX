// Copyright (c) XorTroll. GPLv2.
// QOS-PATCH-005: DockElement implementation — hover-scale magnification using
// pu::ui::SigmoidIncrementer<s32> for smooth per-slot transitions.
// EVENT UX_DOCK_MAGNIFY is emitted (via UL_LOG_INFO) when the focused index changes.
// See ui_DockElement.hpp for scale constants.

#include <ul/menu/ui/ui_DockElement.hpp>
#include <ul/ul_Result.hpp>
#include <cmath>

namespace ul::menu::ui {

    DockElement::DockElement(const s32 x, const s32 y, const u32 w, const u32 h)
        : elem_x(x), elem_y(y), elem_w(w), elem_h(h), focused_slot_idx(-1) {}

    void DockElement::AddSlot(pu::sdl2::TextureHandle::Ref icon) {
        // QOS-PATCH-005: initialise slot at rest scale (1.0 → DockBaseSlotSize pixels)
        DockSlot slot = {};
        slot.icon = std::move(icon);
        slot.target_scale = DockScaleFar;
        slot.cur_scale = DockScaleFar;
        slot.cur_scale_pixels = static_cast<s32>(DockBaseSlotSize);
        slot.scale_pixel_incr = {};
        this->slots.push_back(std::move(slot));
    }

    void DockElement::UpdateHoverZone(const s32 new_focused_idx) {
        if(new_focused_idx == this->focused_slot_idx) {
            return;
        }
        // QOS-PATCH-005: EVENT UX_DOCK_MAGNIFY
        UL_LOG_INFO("EVENT UX_DOCK_MAGNIFY focused_slot=%d", new_focused_idx);
        this->focused_slot_idx = new_focused_idx;

        for(s32 i = 0; i < static_cast<s32>(this->slots.size()); i++) {
            auto &slot = this->slots[static_cast<std::size_t>(i)];
            const float new_target = this->ComputeTargetScale(i);
            if(new_target == slot.target_scale) {
                continue;
            }
            slot.target_scale = new_target;

            // Drive scale interpolation in pixel space so SigmoidIncrementer<s32> works
            const auto target_pixels = static_cast<s32>(DockBaseSlotSize * new_target);
            if(slot.cur_scale_pixels != target_pixels) {
                slot.scale_pixel_incr.Start(
                    DockScaleIncrementSteps,
                    slot.cur_scale_pixels,
                    target_pixels - slot.cur_scale_pixels);
            }
        }
    }

    void DockElement::OnRender(pu::ui::render::Renderer::Ref &drawer, const s32 base_x, const s32 base_y) {
        if(this->slots.empty()) {
            return;
        }

        // QOS-PATCH-005: First pass — advance sigmoid incrementers and compute total width
        u32 total_render_w = 0;
        for(auto &slot : this->slots) {
            // Advance the incrementer; when done it returns true
            if(!slot.scale_pixel_incr.IsDone()) {
                slot.scale_pixel_incr.Increment(slot.cur_scale_pixels);
            }
            slot.cur_scale = static_cast<float>(slot.cur_scale_pixels) / static_cast<float>(DockBaseSlotSize);
            total_render_w += static_cast<u32>(slot.cur_scale_pixels);
        }
        // Add inter-slot margins
        if(this->slots.size() > 1) {
            total_render_w += DockSlotHMargin * static_cast<u32>(this->slots.size() - 1);
        }

        // QOS-PATCH-005: Centre the dock strip horizontally within elem_w
        const auto strip_start_x = base_x + (static_cast<s32>(this->elem_w) - static_cast<s32>(total_render_w)) / 2;

        // Draw translucent background bar
        const pu::ui::Color bg_color = { 0x10, 0x10, 0x10, 0xCC };
        drawer->RenderRectangleFill(
            bg_color,
            strip_start_x - static_cast<s32>(DockBgPad),
            base_y + static_cast<s32>(this->elem_h) - static_cast<s32>(DockBaseSlotSize) - 2 * static_cast<s32>(DockBgPad),
            total_render_w + 2 * DockBgPad,
            DockBaseSlotSize + 2 * DockBgPad);

        // QOS-PATCH-005: Second pass — render each icon scaled + vertically bottom-aligned
        s32 cur_x = strip_start_x;
        for(const auto &slot : this->slots) {
            const auto sz = static_cast<u32>(slot.cur_scale_pixels);
            // Bottom-align within the dock strip
            const auto icon_y = base_y + static_cast<s32>(this->elem_h)
                                - static_cast<s32>(DockBgPad)
                                - static_cast<s32>(sz);

            if(slot.icon != nullptr) {
                drawer->RenderTexture(
                    slot.icon->Get(),
                    cur_x,
                    icon_y,
                    pu::ui::render::TextureRenderOptions({}, sz, sz, {}, {}, {}));
            }
            cur_x += static_cast<s32>(sz) + static_cast<s32>(DockSlotHMargin);
        }
    }

    void DockElement::OnInput(const u64 /*keys_down*/, const u64 /*keys_up*/, const u64 /*keys_held*/, const pu::ui::TouchPoint /*touch_pos*/) {
        // QOS-PATCH-005: Input routing is handled by MainMenuLayout::RouteDpadInput.
        // DockElement itself is passive — MainMenuLayout calls UpdateHoverZone() each frame.
    }

}
