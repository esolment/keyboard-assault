/*
 * main.cpp — Windows keyboard remapper
 *
 * Перехватывает ввод со всех клавиатур через WH_KEYBOARD_LL.
 * Права администратора не требуются.
 *
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <map>
#include <set>

// ─── Настройки ────────────────────────────────────────────────────────────────
static const DWORD DELETE_REPEAT_MS = 150;

// ─── Навигационная таблица (Caps + key → стрелки/Home/End) ───────────────────
static const std::map<DWORD, DWORD> NAV_MAP = {
    {'I', VK_UP},
    {'J', VK_LEFT},
    {'K', VK_DOWN},
    {'L', VK_RIGHT},
    {'U', VK_HOME},
    {'O', VK_END},
};

// ─── Глобальное состояние ─────────────────────────────────────────────────────
static HHOOK g_hook = nullptr;

static bool g_caps_held      = false;
static bool g_alt_held       = false;
static bool g_ctrl_held      = false;
static bool g_shift_held     = false;
static bool g_backspace_held = false;

static DWORD g_last_delete_ms = 0;
static std::set<DWORD> g_consumed_while_caps;

// ─── SendInput helpers ────────────────────────────────────────────────────────
static void send_vk(DWORD vk, bool down)
{
    bool ext = (vk == VK_UP || vk == VK_DOWN || vk == VK_LEFT || vk == VK_RIGHT ||
                vk == VK_HOME || vk == VK_END || vk == VK_DELETE ||
                vk == VK_INSERT || vk == VK_PRIOR || vk == VK_NEXT ||
                vk == VK_RCONTROL || vk == VK_RMENU ||
                vk == VK_VOLUME_MUTE || vk == VK_VOLUME_DOWN || vk == VK_VOLUME_UP);
    INPUT inp{};
    inp.type       = INPUT_KEYBOARD;
    inp.ki.wVk     = (WORD)vk;
    inp.ki.dwFlags = (down ? 0 : KEYEVENTF_KEYUP)
                   | (ext  ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &inp, sizeof(INPUT));
}

static void send_ctrl_backspace()
{
    send_vk(VK_CONTROL, true);
    send_vk(VK_BACK,    true);
    send_vk(VK_BACK,    false);
    send_vk(VK_CONTROL, false);
}

static void send_win_space()
{
    send_vk(VK_LWIN,  true);
    send_vk(VK_SPACE, true);
    send_vk(VK_SPACE, false);
    send_vk(VK_LWIN,  false);
}

// ─── LL Keyboard Hook ─────────────────────────────────────────────────────────
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode < 0)
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    auto* kb   = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    DWORD vk   = kb->vkCode;
    bool  down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    // Пропускаем события от нашего же SendInput
    if (kb->flags & LLKHF_INJECTED)
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    // CapsLock — только отслеживаем, не пропускаем в систему
    if (vk == VK_CAPITAL) {
        g_caps_held = down;
        return 1;
    }

    // Обновляем состояние модификаторов
    if (vk == VK_LMENU)    g_alt_held   = down;
    if (vk == VK_LCONTROL) g_ctrl_held  = down;
    if (vk == VK_LSHIFT)   g_shift_held = down;

    // Alt+Space → Win+Space
    if (g_alt_held && vk == VK_SPACE) {
        if (down) {
            send_vk(VK_LMENU, false);
            send_win_space();
            send_vk(VK_LMENU, true);
        }
        return 1;
    }

    // Shift+Backspace → Delete
    if (g_shift_held && vk == VK_BACK) {
        send_vk(VK_LSHIFT, false);
        send_vk(VK_DELETE, down);
        return 1;
    }

    // Ctrl+F1 → Mute, Ctrl+F2 → VolDown, Ctrl+F3 → VolUp
    if (g_ctrl_held && (vk == VK_F1 || vk == VK_F2 || vk == VK_F3)) {
        send_vk(VK_LCONTROL, false);
        DWORD t = (vk == VK_F1) ? VK_VOLUME_MUTE :
                  (vk == VK_F2) ? VK_VOLUME_DOWN : VK_VOLUME_UP;
        send_vk(t, down);
        return 1;
    }

    // RightShift → /
    if (vk == VK_RSHIFT) {
        send_vk(VK_OEM_2, down);
        return 1;
    }

    // Up → RightShift (без Caps)
    if (vk == VK_UP && !g_caps_held) {
        send_vk(VK_RSHIFT, down);
        return 1;
    }

    // / → Up (без Caps)
    if (vk == VK_OEM_2 && !g_caps_held) {
        send_vk(VK_UP, down);
        return 1;
    }

    // Caps+Backspace → удалить слово (с повтором)
    if (vk == VK_BACK) g_backspace_held = down;
    if (g_caps_held && g_backspace_held && vk == VK_BACK) {
        if (down) {
            DWORD now = GetTickCount();
            if (now - g_last_delete_ms > DELETE_REPEAT_MS) {
                send_ctrl_backspace();
                g_last_delete_ms = now;
            }
        }
        return 1;
    }

    // Caps + навигация
    if (g_caps_held) {
        auto it = NAV_MAP.find(vk);
        if (it != NAV_MAP.end()) {
            send_vk(it->second, down);
            if (down) g_consumed_while_caps.insert(vk);
            return 1;
        }
    }

    // Key-up клавиш, съеденных в режиме Caps
    if (!down && g_consumed_while_caps.count(vk)) {
        g_consumed_while_caps.erase(vk);
        return 1;
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// ─── main ─────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    g_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    if (!g_hook) return 1;

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    UnhookWindowsHookEx(g_hook);
    return 0;
}