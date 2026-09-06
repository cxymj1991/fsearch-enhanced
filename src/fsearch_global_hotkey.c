/*
   FSearch - A fast file search utility
   Copyright © 2026 Christian Boxdörfer

   自定义全局唤起快捷键：通过 X11 XGrabKey 注册系统级热键，
   即使 FSearch 不在前台（例如仅驻留托盘）也能一键唤起/聚焦窗口。

   实现说明：
   - 仅在 X11 后端生效（UOS 20 / deepin v20 默认 X11）。Wayland 下整体为 no-op。
   - XGrabKey 把组合键抓到本进程；GDK 根窗口事件过滤器捕获该按键并触发回调。
   - 对“无锁键 / NumLock / CapsLock / ScrollLock”及其组合共 8 种锁键状态各抓一次，
     否则开着 NumLock/CapsLock/ScrollLock 时按组合键不会触发。
   - 仅剥离“锁键”修饰符（NumLock/CapsLock/ScrollLock），保留真正的加速键修饰符
     （Shift/Control/Alt/Super/Hyper/Meta）。
   - 关键修正：GDK 的修饰键位（如 GDK_SUPER_MASK 是高位位 0x4000000）与 X11 的
     8-bit 低掩码（Mod4 = 64）并不相等，必须把 GDK 修饰位转换成对应的 X11 掩码后才能
     交给 XGrabKey / 与 X 事件状态比较，否则 Super 等组合键永远抓不到、也永远匹配不上。
*/

#define G_LOG_DOMAIN "fsearch-global-hotkey"

#include "fsearch_global_hotkey.h"

#include <gdk/gdk.h>
#include <gtk/gtk.h>

#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#endif

static void (*activate_callback)(void) = NULL;

void
fsearch_global_hotkey_set_activate_callback(void (*cb)(void)) {
    activate_callback = cb;
}

#ifdef GDK_WINDOWING_X11

typedef struct {
    Display *display;
    Window root;
    guint keycode;
    guint xmodifiers; // 已转换成 X11 8-bit 掩码的修饰键（用于 XGrabKey 与事件匹配）
    gboolean grabbed;
    GdkWindow *filter_window;
} FsearchGlobalHotkey;

static FsearchGlobalHotkey hk = {0};

// 仅剥离“锁键”修饰符：NumLock(Mod2)、CapsLock(Lock)、ScrollLock(Mod5)。
// 注意：这些 GDK 位与 X11 的对应掩码数值相同（Lock=2/Mod2=16/Mod5=128），可直接用于 X 事件比较。
static GdkModifierType
ignored_lock_mask(void) {
    return (GdkModifierType)(GDK_LOCK_MASK | GDK_MOD2_MASK | GDK_MOD5_MASK);
}

// 把 GDK 修饰键位转换成 X11 的 8-bit 修饰键掩码（ShiftMask/ControlMask/Mod1..Mod5）。
// 物理修饰键（Shift/Lock/Control/Mod1-5）与 X11 掩码数值一致，可直接转。
// 但 Super/Hyper/Meta 在 GDK 里是“虚拟修饰键”（高位位，如 GDK_SUPER_MASK=0x4000000），
// 与 X11 的 8-bit 低掩码（Super→Mod4=64）并不相等，必须经 keymap 映射到真实掩码后才能交给 XGrabKey。
// 注意：GdkModifierIntent 枚举并不含 SUPER/HYPER/META，故不能用 gdk_keymap_get_modifier_mask(...,意图)，
// 而要用 gdk_keymap_map_virtual_modifiers() 把虚拟位映射成真实 Mod 位。
static guint
gdk_mods_to_x11(GdkModifierType gdk_mods) {
    guint x = 0;
    // 物理/真实修饰键（与 X11 掩码数值一致）
    if (gdk_mods & GDK_SHIFT_MASK)   x |= ShiftMask;
    if (gdk_mods & GDK_LOCK_MASK)    x |= LockMask;
    if (gdk_mods & GDK_CONTROL_MASK) x |= ControlMask;
    if (gdk_mods & GDK_MOD1_MASK)    x |= Mod1Mask;
    if (gdk_mods & GDK_MOD2_MASK)    x |= Mod2Mask;
    if (gdk_mods & GDK_MOD3_MASK)    x |= Mod3Mask;
    if (gdk_mods & GDK_MOD4_MASK)    x |= Mod4Mask;
    if (gdk_mods & GDK_MOD5_MASK)    x |= Mod5Mask;

    GdkDisplay *disp = gdk_display_get_default();
    GdkKeymap *keymap = disp ? gdk_keymap_get_for_display(disp) : NULL;
    if (keymap) {
        // 把虚拟修饰键（Super/Hyper/Meta）映射成真实 X11 掩码（Super→Mod4 等）
        GdkModifierType mapped = gdk_mods;
        if (gdk_keymap_map_virtual_modifiers(keymap, &mapped)) {
            if (mapped & GDK_MOD4_MASK) x |= Mod4Mask;
            if (mapped & GDK_MOD3_MASK) x |= Mod3Mask;
            if (mapped & GDK_MOD1_MASK) x |= Mod1Mask;
            if (mapped & GDK_MOD2_MASK) x |= Mod2Mask;
            if (mapped & GDK_MOD5_MASK) x |= Mod5Mask;
        }
    } else {
        // 无 keymap 时的兜底：按常规绑定直接映射
        if (gdk_mods & GDK_SUPER_MASK) x |= Mod4Mask;
        if (gdk_mods & GDK_HYPER_MASK) x |= Mod3Mask;
        if (gdk_mods & GDK_META_MASK)  x |= Mod1Mask;
    }
    return x;
}

// 对“无锁键 / 仅 NumLock / 仅 CapsLock / 两者 / 仅 ScrollLock / Num+Scroll / Caps+Scroll / 三者”共 8 种
// 锁键状态各抓一次，否则用户开着 NumLock/CapsLock/ScrollLock 时按组合键不会触发。
static const guint lock_combos[8] = {
    0,
    GDK_MOD2_MASK,
    GDK_LOCK_MASK,
    GDK_MOD2_MASK | GDK_LOCK_MASK,
    GDK_MOD5_MASK,
    GDK_MOD2_MASK | GDK_MOD5_MASK,
    GDK_LOCK_MASK | GDK_MOD5_MASK,
    GDK_MOD2_MASK | GDK_LOCK_MASK | GDK_MOD5_MASK,
};

static void
hotkey_grab(void) {
    if (!hk.display || hk.keycode == 0 || hk.xmodifiers == 0) {
        return;
    }
    const guint base = hk.xmodifiers & ~(guint)ignored_lock_mask();
    for (int i = 0; i < 8; i++) {
        XGrabKey(hk.display,
                 (int)hk.keycode,
                 (int)(base | lock_combos[i]),
                 hk.root,
                 False,
                 GrabModeAsync,
                 GrabModeAsync);
    }
    hk.grabbed = TRUE;
}

static void
hotkey_ungrab(void) {
    if (!hk.display || !hk.grabbed) {
        return;
    }
    const guint base = hk.xmodifiers & ~(guint)ignored_lock_mask();
    for (int i = 0; i < 8; i++) {
        XUngrabKey(hk.display, (int)hk.keycode, (int)(base | lock_combos[i]), hk.root);
    }
    hk.grabbed = FALSE;
}

static GdkFilterReturn
hotkey_root_filter(GdkXEvent *xevent, GdkEvent *event, gpointer data) {
    (void)data;
    (void)event;

    XEvent *xev = (XEvent *)xevent;
    if (xev->type != KeyPress) {
        return GDK_FILTER_CONTINUE;
    }

    const guint mask = (guint)ignored_lock_mask();
    const guint base = hk.xmodifiers & ~mask;
    const guint state = (guint)xev->xkey.state & ~mask;

    if ((guint)xev->xkey.keycode == hk.keycode && state == base) {
        if (activate_callback) {
            activate_callback();
        }
        // 拦截该按键，避免其它程序（包括本进程的菜单）再次收到
        return GDK_FILTER_REMOVE;
    }
    return GDK_FILTER_CONTINUE;
}

#endif // GDK_WINDOWING_X11

void
fsearch_global_hotkey_init(void) {
#ifdef GDK_WINDOWING_X11
    GdkDisplay *disp = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(disp)) {
        g_warning("[global-hotkey] 当前会话非 X11（可能是 Wayland），全局快捷键不可用");
        return;
    }
    hk.display = gdk_x11_display_get_xdisplay(disp);
    hk.root = gdk_x11_window_get_xid(gdk_get_default_root_window());
    hk.filter_window = gdk_get_default_root_window();
    gdk_window_add_filter(hk.filter_window, hotkey_root_filter, NULL);
#else
    g_warning("[global-hotkey] 编译时未启用 X11 支持，全局快捷键不可用");
#endif
}

void
fsearch_global_hotkey_set(const char *accel) {
#ifdef GDK_WINDOWING_X11
    if (!hk.display) {
        return;
    }

    // 先解除旧绑定
    hotkey_ungrab();
    hk.keycode = 0;
    hk.xmodifiers = 0;

    if (!accel || !*accel) {
        return; // 留空 = 关闭全局快捷键
    }

    guint keyval = 0;
    GdkModifierType mods = 0;
    gtk_accelerator_parse(accel, &keyval, &mods);
    if (keyval == 0) {
        g_warning("[global-hotkey] 无法解析快捷键字符串：%s", accel);
        return;
    }

    // GDK keyval 在 X11 后端下即等于 X11 keysym（二者数值一致），无需转换，
    // 直接强转后查 X 按键码交给 XGrabKey。
    KeySym keysym = (KeySym)keyval;
    int keycode = XKeysymToKeycode(hk.display, keysym);
    if (keycode == 0) {
        g_warning("[global-hotkey] 找不到对应的 X 按键码：%s", accel);
        return;
    }

    hk.keycode = (guint)keycode;
    // 关键：把 GDK 修饰位转换成 X11 掩码后再保存，抓取与匹配都基于此。
    hk.xmodifiers = gdk_mods_to_x11(mods);
    hotkey_grab();
#else
    (void)accel;
#endif
}

void
fsearch_global_hotkey_shutdown(void) {
#ifdef GDK_WINDOWING_X11
    if (!hk.display) {
        return;
    }
    hotkey_ungrab();
    if (hk.filter_window) {
        gdk_window_remove_filter(hk.filter_window, hotkey_root_filter, NULL);
        hk.filter_window = NULL;
    }
#endif
}
