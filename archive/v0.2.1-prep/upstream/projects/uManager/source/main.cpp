#include <ul/man/ui/ui_MainApplication.hpp>
#include <ul/sf/sf_PublicService.hpp>
#include <ul/man/man_AppScanner.hpp>

ul::man::ui::MainApplication::Ref g_MainApplication;

namespace {

    bool g_IsAvailable;
    ul::Version g_GotVersion;

    void Initialize() {
        UL_RC_ASSERT(nsInitialize());

        // Q OS v0.2.3 — scan installed titles and write records.bin + icons to
        // sdmc:/switch/qos-apps/ so hbloader-hosted mocks can read them.
        // Runs before the UI loop; failures are non-fatal (summary logged below).
        const auto scan = ul::man::ScanAndWriteAppList();
        UL_LOG_INFO("QOS_APP_SCAN: ok=%d count=%u bin=%zu icons=%zu summary=%s",
                    scan.ok ? 1 : 0,
                    scan.count,
                    scan.bin_size,
                    scan.icons_total_bytes,
                    scan.summary.c_str());

        g_IsAvailable = ul::sf::IsAvailable();
        if(g_IsAvailable) {
            UL_RC_ASSERT(ul::sf::Initialize());
            UL_RC_ASSERT(ul::sf::GetVersion(&g_GotVersion));
        }
    }

    void Finalize() {
        ul::sf::Finalize();
        nsExit();
    }

}

int main() {
    ul::InitializeLogging("qos-manager");
    Initialize();

    // v0.6.2: use software renderer to avoid Mesa nouveau_bo_new NULL-deref when
    // uManager runs as a library applet inside the 14 MB uMenu applet pool.
    // Hardware-accelerated SDL renderer + SDL2_image (ImgAllFlags) exhaust the
    // pool; software rendering eliminates that path entirely.  No image textures
    // are loaded by uManager so dropping UseImage() has no visible effect.
    auto renderer_opts = pu::ui::render::RendererInitOptions(SDL_INIT_EVERYTHING, pu::ui::render::RendererSoftwareFlags);

    renderer_opts.SetPlServiceType(PlServiceType_User);
    renderer_opts.AddDefaultAllSharedFonts();
    renderer_opts.AddExtraDefaultFontSize(35);

    renderer_opts.UseRomfs();

    renderer_opts.SetInputPlayerCount(1);
    renderer_opts.AddInputNpadStyleTag(HidNpadStyleSet_NpadStandard);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_Handheld);
    renderer_opts.AddInputNpadIdType(HidNpadIdType_No1);

    auto renderer = pu::ui::render::Renderer::New(renderer_opts);

    g_MainApplication = ul::man::ui::MainApplication::New(renderer);
    g_MainApplication->Set(g_IsAvailable, g_GotVersion.Equals(ul::CurrentVersion), g_GotVersion);
    UL_RC_ASSERT(g_MainApplication->Load());
    g_MainApplication->ShowWithFadeIn();

    Finalize();
    return 0;
}