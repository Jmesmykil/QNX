// qd_VaultHostLayout.hpp — IMenuLayout host wrapper for QdVaultLayout (Vault Element).
//
// Why this class exists
// ---------------------
// QdVaultLayout is a pu::ui::elm::Element (despite the misleading "Layout" suffix in
// its class name).  Plutonium's pu::ui::Layout::Add() is the canonical way to attach
// an Element to a screen, so the original implementation wrapped QdVaultLayout in a
// bare `pu::ui::Layout::New()` and called that the "host layout."
//
// That architecture had a latent crash: ul::menu::ui::OnMessage() does
//
//     auto ptr = g_MenuApplication->GetLayout<IMenuLayout>();
//     ptr->NotifyMessageContext(msg_ctx);
//
// and Plutonium's GetLayout<L>() is a `std::static_pointer_cast<L>(this->lyt)` —
// an UNCHECKED downcast.  When the active layout is a bare pu::ui::Layout (as the
// Vault host was), the resulting "IMenuLayout*" is a misaligned reinterpretation
// of the base Layout vtable.  NotifyMessageContext() then accesses members
// (`msg_queue`, `msg_queue_lock`) at offsets that simply don't exist on the bare
// Layout, producing a Data Abort at address 0x0 (Atmosphère 2168-0002).
//
// The crash reproduces as: open Vault → press HOME → uMenu dies (PC at uMenu+0x7b07a8,
// confirmed against atmosphere/crash_reports/01777163509_010000000000100d.log).
//
// Fix
// ---
// QdVaultHostLayout is a real IMenuLayout that owns the QdVaultLayout element and
// supplies the IMenuLayout obligations the dispatcher relies on:
//   * OnMenuInput     — input is forwarded to the wrapped Vault element
//   * OnHomeButtonPress — return the user to the main desktop and consume the
//                         message (returning true tells IMenuLayout::OnLayoutInput
//                         to pop the queued HomeRequest)
//   * LoadSfx / DisposeSfx — Vault has no per-layout sfx, so both are no-ops
//
// MenuApplication holds a QdVaultHostLayout::Ref instead of a bare Layout::Ref;
// LoadLayout(this->vault_host_lyt_) still works because IMenuLayout is a
// pu::ui::Layout.

#pragma once

#include <ul/menu/ui/ui_IMenuLayout.hpp>
#include <ul/menu/qdesktop/qd_VaultLayout.hpp>
#include <ul/menu/qdesktop/qd_HotCornerOverlay.hpp>

namespace ul::menu::qdesktop {

    class QdVaultHostLayout : public ul::menu::ui::IMenuLayout {
        public:
            // Construct with an already-built QdVaultLayout element.  The host
            // takes ownership via shared_ptr and adds the element as a child.
            explicit QdVaultHostLayout(QdVaultLayout::Ref vault_element);

            // PU_SMART_CTOR also defines `using Ref = std::shared_ptr<...>;` — do
            // NOT declare Ref above this macro or the compiler errors with a
            // redeclaration of the alias.
            PU_SMART_CTOR(QdVaultHostLayout)

            // ── IMenuLayout pure-virtual obligations ─────────────────────────

            void OnMenuInput(const u64 keys_down,
                             const u64 keys_up,
                             const u64 keys_held,
                             const pu::ui::TouchPoint touch_pos) override;

            // Return to the main desktop on HOME and consume the message.
            // Returning true tells IMenuLayout::OnLayoutInput to pop the
            // queued HomeRequest from the message queue.
            bool OnHomeButtonPress() override;

            // Vault has no dedicated sfx surface today — these are intentional
            // no-ops (not stubs); kept here so adding sfx later is a single-file
            // change and the contract with IMenuLayout is satisfied.
            void LoadSfx() override;
            void DisposeSfx() override;

        private:
            QdVaultLayout::Ref vault_element_;
            QdHotCornerOverlay::Ref overlay_;
    };

}
