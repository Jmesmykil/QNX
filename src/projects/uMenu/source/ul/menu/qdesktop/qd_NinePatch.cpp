// qd_NinePatch.cpp — DrawNinePatch implementation.
#ifdef QDESKTOP_MODE
#include <ul/menu/qdesktop/qd_NinePatch.hpp>
#include <algorithm>

namespace ul::menu::qdesktop {

void DrawNinePatch(SDL_Renderer* r, SDL_Texture* src,
                   int src_w, int src_h,
                   const NinePatchInsets& ins,
                   const SDL_Rect& dst, u8 alpha)
{
    if (r == nullptr || src == nullptr) return;

    SDL_SetTextureAlphaMod(src, alpha);

    const int dx = dst.x, dy = dst.y, dw = dst.w, dh = dst.h;
    const int mws = std::max(0, src_w - ins.left - ins.right);   // source mid-width
    const int mhs = std::max(0, src_h - ins.top  - ins.bottom);  // source mid-height
    const int mwd = std::max(0, dw    - ins.left - ins.right);   // dest   mid-width
    const int mhd = std::max(0, dh    - ins.top  - ins.bottom);  // dest   mid-height

    // Source regions (row-major: TL TC TR / ML MC MR / BL BC BR)
    const SDL_Rect s_tl = {0,              0,               ins.left,  ins.top    };
    const SDL_Rect s_tc = {ins.left,       0,               mws,       ins.top    };
    const SDL_Rect s_tr = {src_w-ins.right,0,               ins.right, ins.top    };
    const SDL_Rect s_ml = {0,              ins.top,         ins.left,  mhs        };
    const SDL_Rect s_mc = {ins.left,       ins.top,         mws,       mhs        };
    const SDL_Rect s_mr = {src_w-ins.right,ins.top,         ins.right, mhs        };
    const SDL_Rect s_bl = {0,              src_h-ins.bottom,ins.left,  ins.bottom };
    const SDL_Rect s_bc = {ins.left,       src_h-ins.bottom,mws,       ins.bottom };
    const SDL_Rect s_br = {src_w-ins.right,src_h-ins.bottom,ins.right, ins.bottom };

    // Dest regions
    const SDL_Rect d_tl = {dx,            dy,            ins.left,  ins.top    };
    const SDL_Rect d_tc = {dx+ins.left,   dy,            mwd,       ins.top    };
    const SDL_Rect d_tr = {dx+dw-ins.right,dy,           ins.right, ins.top    };
    const SDL_Rect d_ml = {dx,            dy+ins.top,    ins.left,  mhd        };
    const SDL_Rect d_mc = {dx+ins.left,   dy+ins.top,    mwd,       mhd        };
    const SDL_Rect d_mr = {dx+dw-ins.right,dy+ins.top,   ins.right, mhd        };
    const SDL_Rect d_bl = {dx,            dy+dh-ins.bottom,ins.left, ins.bottom };
    const SDL_Rect d_bc = {dx+ins.left,   dy+dh-ins.bottom,mwd,     ins.bottom };
    const SDL_Rect d_br = {dx+dw-ins.right,dy+dh-ins.bottom,ins.right,ins.bottom};

    auto blit = [&](const SDL_Rect& s, const SDL_Rect& d) {
        if (s.w > 0 && s.h > 0 && d.w > 0 && d.h > 0)
            SDL_RenderCopy(r, src, &s, &d);
    };

    blit(s_tl, d_tl); blit(s_tc, d_tc); blit(s_tr, d_tr);
    blit(s_ml, d_ml); blit(s_mc, d_mc); blit(s_mr, d_mr);
    blit(s_bl, d_bl); blit(s_bc, d_bc); blit(s_br, d_br);

    SDL_SetTextureAlphaMod(src, 255);
}

} // namespace ul::menu::qdesktop
#endif // QDESKTOP_MODE
