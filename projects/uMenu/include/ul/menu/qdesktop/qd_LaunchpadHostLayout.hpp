// qd_LaunchpadHostLayout.hpp — IMenuLayout host wrapper for QdLaunchpadElement.
//
// Why this class exists
// ---------------------
// QdLaunchpadElement is a pu::ui::elm::Element despite the "Layout"-style
// naming convention.  Plutonium's pu::ui::Layout::Add() is the canonical way
// to attach an Element to a screen, so QdLaunchpadElement must be wrapped in
// a Layout subclass.
//
// That wrapper MUST be an IMenuLayout — NOT a bare pu::ui::Layout.  Plutonium's
// GetLayout<L>() is a `std::static_pointer_cast<L>(this->lyt)` — an UNCHECKED
// downcast.  ul::menu::ui::OnMessage() does
//
//     auto ptr = g_MenuApplication->GetLayout<IMenuLayout>();
//     ptr->NotifyMessageContext(msg_ctx);
//
// If the active layout is a bare pu::ui::Layout, the cast reinterprets the
// vtable and NotifyMessageContext() accesses members (msg_queue, msg_queue_lock)
// at offsets that don't exist — Data Abort at 0x0 (Atmosphère 2168-0002).
// See qd_VaultHostLayout.hpp and qd_MonitorHostLayout.hpp for the full crash
// chain description.
//
// QdLaunchpadHostLayout is a real IMenuLayout that owns the QdLaunchpadElement
// and satisfies the IMenuLayout obligations the dispatcher relies on.

#pragma once

#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_Launchpad.hpp>

namespace ul::menu::qdesktop {

    class QdLaunchpadHostLayout : public ul::menu::ui::IMenuLayout {
        public:
            // Construct with an already-built QdLaunchpadElement.  The host
            // takes ownership via shared_ptr and adds the element as a child.
            explicit QdLaunchpadHostLayout(QdLaunchpadElement::Ref launchpad_element);

            // PU_SMART_CTOR also defines `using Ref = std::shared_ptr<...>;` — do
            // NOT declare Ref above this macro or the compiler errors with a
            // redeclaration of the alias.
            PU_SMART_CTOR(QdLaunchpadHostLayout)

            // ── IMenuLayout pure-virtual obligations ─────────────────────────────

            void OnMenuInput(const u64 keys_down,
                             const u64 keys_up,
                             const u64 keys_held,
                             const pu::ui::TouchPoint touch_pos) override;

            // Return to the main desktop on HOME and consume the message.
            // Returning true tells IMenuLayout::OnLayoutInput to pop the
            // queued HomeRequest from the message queue.
            bool OnHomeButtonPress() override;

            // Launchpad has no dedicated sfx surface — these are intentional
            // no-ops (not stubs); kept here so adding sfx later is a single-file
            // change and the contract with IMenuLayout is satisfied.
            void LoadSfx() override;
            void DisposeSfx() override;

        private:
            QdLaunchpadElement::Ref launchpad_element_;
    };

}
