/**
 * Plutonium library
 * @file ui_Application.hpp
 * @brief Contains pu::ui::Application class
 * @author XorTroll
 * @copyright XorTroll
 */

#pragma once
#include <pu/ui/ui_Dialog.hpp>
#include <pu/ui/ui_Layout.hpp>
#include <pu/ui/ui_Overlay.hpp>
#include <chrono>

namespace pu::ui {

    /**
     * @brief Type that represents the basic object in this library, the general context for UI and rendering.
     */
    class Application {
        public:
            /**
             * @brief Function type used to further customize a Dialog (setting fields) when using Application::CreateShowDialog.
             */
            using DialogPrepareCallback = std::function<void(Dialog::Ref&)>;

            /**
             * @brief Function type used to handle input events.
             * @param keys_down The keys that were pressed in this frame.
             * @param keys_up The keys that were released in this frame.
             * @param keys_held The keys that are being held in this frame.
             * @param touch_pos The position of the touch on the screen.
             */
            using OnInputCallback = std::function<void(const u64 keys_down, const u64 keys_up, const u64 keys_held, const TouchPoint touch_pos)>;

            /**
             * @brief Function type to be called every rendering loop/frame.
             */
            using RenderCallback = std::function<void()>;

            /**
             * @brief Function type to be called when rendering over the main rendering loop/frame.
             * @param renderer The Renderer instance to render over.
             * @return Whether the rendering loop/frame should continue.
             */
            using RenderOverFunction = std::function<bool(render::Renderer::Ref&)>;
            
            // Self-explanatory constant(s)

            static constexpr u8 DefaultFadeAlphaIncrementSteps = 20;

        private:
            void OnRender();

        protected:
            bool loaded;
            bool in_render_over;
            RenderOverFunction render_over_fn;
            bool is_shown;
            u8 fade_alpha_increment_steps;
            SigmoidIncrementer<s32> fade_alpha_incr;
            s32 fade_alpha;
            sdl2::TextureHandle::Ref fade_bg_tex;
            Color fade_bg_clr;
            Layout::Ref lyt;
            Overlay::Ref ovl;
            u64 ovl_timeout_ms;
            std::chrono::steady_clock::time_point ovl_start_time;
            std::vector<RenderCallback> render_cbs;
            OnInputCallback on_ipt_cb;
            render::Renderer::Ref renderer;
            RMutex render_lock;
            // Task A/B — USB mouse + keyboard state (zeroed in ctor, polled in accessors)
            HidMouseState usb_mouse_state_;
            HidKeyboardState usb_kbd_state_;
            // Accumulated fractional cursor position for sub-pixel delta accumulation
            s32 usb_mouse_cursor_x_;
            s32 usb_mouse_cursor_y_;
        
        public:
            /**
             * @brief Creates a new Application with the specified Renderer.
             * @param renderer The Renderer to use for rendering.
             */
            Application(render::Renderer::Ref renderer);
            PU_SMART_CTOR(Application)
            virtual ~Application();

            /**
             * @brief Loads the Layout to be used by the Application.
             * @param lyt The Layout to load.
             */
            inline void LoadLayout(Layout::Ref lyt) {
                this->lyt = lyt;
            }

            /**
             * @brief Gets the current Layout (of the specified type).
             * @tparam L The type of Layout to get.
             * @return The Layout of the specified type.
             */
            template<typename L>
            inline std::shared_ptr<L> GetLayout() {
                static_assert(std::is_base_of_v<ui::Layout, L>);
                return std::static_pointer_cast<L>(this->lyt);
            }

            /**
             * @brief Loads the Application.
             * @note This function must be called before calling Application::Show or similar.
             * @return Whether the Application was loaded successfully.
             */
            Result Load();

            /**
             * @brief Function to be implemented by child classes, called when the Application is loaded.
             * @note This function must be implemented by the user.
             */
            virtual void OnLoad() = 0;

            /**
             * @brief Adds a RenderCallback to the Application.
             * @param render_cb The RenderCallback to add.
             */
            inline void AddRenderCallback(RenderCallback render_cb) {
                this->render_cbs.push_back(render_cb);
            }

            /**
             * @brief Sets the OnInputCallback for the Application.
             * @param on_ipt_cb The OnInputCallback to set.
             */
            inline void SetOnInput(OnInputCallback on_ipt_cb) {
                this->on_ipt_cb = on_ipt_cb;
            }

            /**
             * @brief Shows a Dialog and waits for the user to interact with it.
             * @param dialog The Dialog to show.
             * @return The index of the option selected by the user.
             */
            inline s32 ShowDialog(Dialog::Ref &dialog) {
                return dialog->Show(this);
            }

            /**
             * @brief Wrapper function that creates a Dialog with the specified parameters and shows it.
             * @param title The title of the Dialog.
             * @param content The content of the Dialog.
             * @param opts The options of the Dialog.
             * @param use_last_opt_as_cancel Whether the last option should be used as a cancel option.
             * @param icon The icon of the Dialog.
             * @param prepare_cb The DialogPrepareCallback to further customize the Dialog.
             * @return The index of the option selected by the user.
             */
            s32 CreateShowDialog(const std::string &title, const std::string &content, const std::vector<std::string> &opts, const bool use_last_opt_as_cancel, sdl2::TextureHandle::Ref icon = {}, DialogPrepareCallback prepare_cb = nullptr);
            
            /**
             * @brief Starts an Overlay.
             * @param ovl The Overlay to start.
             */
            inline void StartOverlay(Overlay::Ref ovl) {
                if(this->ovl == nullptr) {
                    this->ovl = ovl;
                }
            }

            /**
             * @brief Starts an Overlay with a timeout.
             * @param ovl The Overlay to start.
             * @param ms The timeout in milliseconds.
             */
            void StartOverlayWithTimeout(Overlay::Ref ovl, const u64 ms);

            /**
             * @brief Ends the current Overlay.
             */
            void EndOverlay();

            /**
             * @brief Shows the Application.
             * @note Application::Load must be called before calling this function, and the Layout must be previously set using Application::LoadLayout.
             * @note If no Layout was previously set or if the Application was not loaded yet, this function will inmediately return.
             */
            void Show();
            
            /**
             * @brief Shows the Application with a fade-in effect.
             * @note Application::Load must be called before calling this function, and the Layout must be previously set using Application::LoadLayout.
             * @note If no Layout was previously set or if the Application was not loaded yet, this function will inmediately return.
             */
            inline void ShowWithFadeIn() {
                this->FadeIn();
                this->Show();
            }
            
            /**
             * @brief Gets whether the Application is currently shown.
             * @return Whether the Application is currently shown.
             */
            inline bool IsShown() {
                return this->is_shown;
            }

            /**
             * @brief Gets whether the Application can be shown.
             * @note This function checks if the Application is loaded and if any Layout was set.
             * @return Whether the Application can be shown.
             */
            inline bool CanBeShown() {
                return this->loaded && (this->lyt != nullptr);
            }
            
            /**
             * @brief Calls for rendering the Application.
             * @note This is essentially the rendering code called every frame. This function may be used to create custom rendering loops, but be careful when using it.
             * @return Whether the rendering loop/frame should continue.
             */
            bool CallForRender();

            /**
             * @brief Calls for rendering the Application with a custom RenderOverFunction.
             * @param render_over_fn The RenderOverFunction to use.
             * @note This function is useful when you want to render over the main rendering loop/frame. This logic is internally used with Dialog rendering.
             * @return Whether the rendering loop/frame should continue.
             */
            bool CallForRenderWithRenderOver(RenderOverFunction render_over_fn);

            /**
             * @brief Locks the rendering mutex.
             */
            inline void LockRender() {
                rmutexLock(&this->render_lock);
            }

            /**
             * @brief Unlocks the rendering mutex.
             */
            inline void UnlockRender() {
                rmutexUnlock(&this->render_lock);
            }

            /**
             * @brief Starts and waits for a fade-in effect.
             * @note This function will block until the fade-in effect is completed.
             */
            void FadeIn();

            /**
             * @brief Starts and waits for a fade-out effect.
             * @note This function will block until the fade-out effect is completed.
             */
            void FadeOut();
            
            /**
             * @brief Gets whether the Application is currently fading in or already faded in.
             * @return Whether the Application is currently fading in or already faded in.
             */
            inline bool IsFadingOrFadedIn() {
                return this->fade_alpha > 0;
            }

            /**
             * @brief Sets the number of steps for the fade-in and fade-out effects.
             * @param fade_alpha_increment_steps The number of steps for the fade-in and fade-out effects.
             */
            inline void SetFadeAlphaIncrementStepCount(const u8 fade_alpha_increment_steps) {
                this->fade_alpha_increment_steps = fade_alpha_increment_steps;
            }

            /**
             * @brief Sets the target background image for the fade effect.
             * @param bg_tex The background image to use for the fade effect.
             * @note Fade effects will start/end with the specified background image.
             */
            void SetFadeBackgroundImage(sdl2::TextureHandle::Ref bg_tex);

            /**
             * @brief Resets the background image used for the fade effect.
             * @note This function will reset the background image used for the fade effect, and the fade effect will use the set solid color instead.
             */
            void ResetFadeBackgroundImage();

            /**
             * @brief Gets the background image used for the fade effect.
             * @return The background image used for the fade effect.
             */
            inline sdl2::TextureHandle::Ref &GetFadeBackgroundImageTexture() {
                return this->fade_bg_tex;
            }

            /**
             * @brief Gets whether the fade effect is using a background image.
             * @return Whether the fade effect is using a background image.
             */
            inline bool HasFadeBackgroundImage() {
                return this->fade_bg_tex != nullptr;
            }

            /**
             * @brief Sets the background color for the fade effect.
             * @param clr The background color to use for the fade effect.
             * @note Fade effects will start/end with the specified background color.
             * @note If a background image is set, this function will reset it.
             */
            void SetFadeBackgroundColor(const Color clr);
            
            /**
             * @brief Closes the Application.
             * @param do_exit Whether the program should fully exit after closing.
             * @note This function will finish the Application and, if specified, exit the program using `exit(0)`.
             */
            void Close(const bool do_exit = false);
            
            /**
             * @brief Closes the Application with a fade-out effect.
             * @param do_exit Whether the program should fully exit after closing.
             * @note This function will finish the Application with a fade-out effect and, if specified, exit the program using `exit(0)`.
             */
            inline void CloseWithFadeOut(const bool do_exit = false) {
                this->FadeOut();
                this->Close(do_exit);
            }

            /**
             * @brief Gets the currently pressed button keys.
             * @return The currently pressed button keys.
             */
            inline u64 GetButtonsDown() {
                return this->renderer->GetButtonsDown();
            }

            /**
             * @brief Gets the currently released button keys.
             * @return The currently released button keys.
             */
            inline u64 GetButtonsUp() {
                return this->renderer->GetButtonsUp();
            }

            /**
             * @brief Gets the currently held button keys.
             * @return The currently held button keys.
             */
            inline u64 GetButtonsHeld() {
                return this->renderer->GetButtonsHeld();
            }

            /**
             * @brief Gets the currently pressed touch position.
             * @return The currently pressed touch position.
             */
            inline HidTouchScreenState GetTouchState() {
                HidTouchScreenState state = {};
                hidGetTouchScreenStates(&state, 1);
                return state;
            }

            /**
             * @brief Task A — Poll one USB mouse sample and cache it in usb_mouse_state_.
             * @note Callers should check IsUsbMouseConnected() before trusting delta/button fields.
             * @return The polled HidMouseState (also written to usb_mouse_state_).
             */
            inline HidMouseState PollUsbMouse() {
                hidGetMouseStates(&this->usb_mouse_state_, 1);
                return this->usb_mouse_state_;
            }

            /**
             * @brief Task A — Returns true when a USB mouse reports HidMouseAttribute_IsConnected.
             * @note Reads the cached usb_mouse_state_ — call PollUsbMouse() first each frame.
             */
            inline bool IsUsbMouseConnected() const {
                return (this->usb_mouse_state_.attributes & HidMouseAttribute_IsConnected) != 0;
            }

            /**
             * @brief Task A — Update the logical cursor position from USB mouse delta.
             *
             * Clamps to [0, screen_w) x [0, screen_h).  Returns the updated TouchPoint so
             * callers can feed it into the same pipeline as joystick-synthesized cursor.
             *
             * @param screen_w  Logical screen width in pixels (e.g. render::ScreenWidth).
             * @param screen_h  Logical screen height in pixels (e.g. render::ScreenHeight).
             * @return The new cursor position clamped to screen bounds.
             */
            inline TouchPoint ApplyUsbMouseDelta(const s32 screen_w, const s32 screen_h) {
                this->usb_mouse_cursor_x_ += this->usb_mouse_state_.delta_x;
                this->usb_mouse_cursor_y_ += this->usb_mouse_state_.delta_y;
                // Clamp to logical screen bounds
                if(this->usb_mouse_cursor_x_ < 0) { this->usb_mouse_cursor_x_ = 0; }
                if(this->usb_mouse_cursor_y_ < 0) { this->usb_mouse_cursor_y_ = 0; }
                if(this->usb_mouse_cursor_x_ >= screen_w) { this->usb_mouse_cursor_x_ = screen_w - 1; }
                if(this->usb_mouse_cursor_y_ >= screen_h) { this->usb_mouse_cursor_y_ = screen_h - 1; }
                return { static_cast<u32>(this->usb_mouse_cursor_x_), static_cast<u32>(this->usb_mouse_cursor_y_) };
            }

            /**
             * @brief Task A — Returns the raw USB mouse button bitfield from the last PollUsbMouse() call.
             * @note Bits: Left=BIT(0), Right=BIT(1), Middle=BIT(2), Forward=BIT(3), Back=BIT(4).
             */
            inline u32 GetUsbMouseButtons() const {
                return static_cast<u32>(this->usb_mouse_state_.buttons);
            }

            /**
             * @brief Task B — Poll one USB keyboard sample and cache it in usb_kbd_state_.
             * @return The polled HidKeyboardState (also written to usb_kbd_state_).
             */
            inline HidKeyboardState PollUsbKeyboard() {
                hidGetKeyboardStates(&this->usb_kbd_state_, 1);
                return this->usb_kbd_state_;
            }

            /**
             * @brief Task B — Translate held USB keyboard navigation/action keys into
             *        HidNpadButton-compatible bit flags so callers can OR them into keys_down.
             *
             * Mapping:
             *  Return (40)    → HidNpadButton_A       (BIT(0))
             *  Escape (41)    → HidNpadButton_B       (BIT(1))
             *  LeftArrow (80) → HidNpadButton_StickLLeft  (BIT(12))
             *  UpArrow (82)   → HidNpadButton_StickLUp    (BIT(13))
             *  RightArrow(79) → HidNpadButton_StickLRight (BIT(14))
             *  DownArrow (81) → HidNpadButton_StickLDown  (BIT(15))
             *  Home (74)      → HidNpadButton_L        (BIT(6))
             *  End (77)       → HidNpadButton_R        (BIT(7))
             *  PageUp (75)    → HidNpadButton_ZL       (BIT(8))
             *  PageDown (78)  → HidNpadButton_ZR       (BIT(9))
             *
             * @note Call PollUsbKeyboard() first each frame before calling this.
             * @return u64 bitmask compatible with keys_down / keys_held HidNpadButton values.
             */
            inline u64 GetKeyboardKeysDown() const {
                u64 bits = 0;
                const HidKeyboardState *ks = &this->usb_kbd_state_;
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(40)))  { bits |= HidNpadButton_A; }           // Return → A
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(41)))  { bits |= HidNpadButton_B; }           // Escape → B
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(80)))  { bits |= HidNpadButton_StickLLeft; }  // LeftArrow
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(82)))  { bits |= HidNpadButton_StickLUp; }    // UpArrow
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(79)))  { bits |= HidNpadButton_StickLRight; } // RightArrow
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(81)))  { bits |= HidNpadButton_StickLDown; }  // DownArrow
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(74)))  { bits |= HidNpadButton_L; }           // Home
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(77)))  { bits |= HidNpadButton_R; }           // End
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(75)))  { bits |= HidNpadButton_ZL; }          // PageUp
                if(hidKeyboardStateGetKey(ks, static_cast<HidKeyboardKey>(78)))  { bits |= HidNpadButton_ZR; }          // PageDown
                return bits;
            }

            /**
             * @brief Task A — Returns the current cached USB mouse cursor X position.
             */
            inline s32 GetUsbMouseCursorX() const { return this->usb_mouse_cursor_x_; }

            /**
             * @brief Task A — Returns the current cached USB mouse cursor Y position.
             */
            inline s32 GetUsbMouseCursorY() const { return this->usb_mouse_cursor_y_; }
    };

}
