/*
 * main.cpp — Windows keyboard remapper
 *
 * Перехватывает ввод со всех клавиатур через WH_KEYBOARD_LL.
 * Raw Input используется только для определения vendor:product ID
 * устройства, с которого пришло последнее нажатие (WH_KEYBOARD_LL
 * этого не даёт), чтобы выбрать набор функций по конфигу.
 *
 * Права администратора не требуются.
 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <map>
#include <set>
#include <string>
#include <cstdint>

#include "config.h"

// ─── Настройки ────────────────────────────────────────────────────────────
static const DWORD DELETE_REPEAT_MS = 150;

// ─── Навигационная таблица (Caps + key → стрелки/Home/End) ────────────────
static const std::map<DWORD, DWORD> NAV_MAP = {
    {'I', VK_UP},
    {'J', VK_LEFT},
    {'K', VK_DOWN},
    {'L', VK_RIGHT},
    {'U', VK_HOME},
    {'O', VK_END},
};

// ─── Глобальное состояние ───────────────────────────────────────────────────
static HHOOK g_hook = nullptr;
static Config g_cfg;

static bool g_caps_held      = false;
static bool g_alt_held       = false;
static bool g_ctrl_held      = false;
static bool g_shift_held     = false;
static bool g_backspace_held = false;

static DWORD g_last_delete_ms = 0;
static std::set<DWORD> g_consumed_while_caps;

// Устройство, с которого пришло последнее физическое нажатие
// (заполняется через Raw Input, читается в LL-хуке).
static uint32_t g_last_device_id  = 0;
static bool     g_last_full_layout = false;

// Кэш: HANDLE устройства Raw Input → vendor<<16|product
static std::map<HANDLE, uint32_t> g_device_id_cache;

// ─── Определение vendor:product устройства через Raw Input ────────────────
static uint32_t query_device_id(HANDLE hDevice) {
    auto cached = g_device_id_cache.find(hDevice);
    if (cached != g_device_id_cache.end())
        return cached->second;

    RID_DEVICE_INFO info{};
    info.cbSize = sizeof(info);
    UINT size = sizeof(info);
    uint32_t id = 0;

    if (GetRawInputDeviceInfoA(hDevice, RIDI_DEVICEINFO, &info, &size) > 0 &&
        info.dwType == RIM_TYPEKEYBOARD) {
        id = (static_cast<uint32_t>(info.hid.dwVendorId) << 16) |
             (static_cast<uint32_t>(info.hid.dwProductId) & 0xFFFF);
    }

    g_device_id_cache[hDevice] = id;
    return id;
}

// Скрытое окно только для получения WM_INPUT сообщений.
static HWND g_msg_window = nullptr;

static LRESULT CALLBACK RawInputWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_INPUT) {
        UINT size = 0;
        GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, nullptr, &size, sizeof(RAWINPUTHEADER));
        if (size > 0 && size < 1024) {
            BYTE buf[1024];
            if (GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) == size) {
                RAWINPUT* raw = reinterpret_cast<RAWINPUT*>(buf);
                if (raw->header.dwType == RIM_TYPEKEYBOARD) {
                    uint32_t id = query_device_id(raw->header.hDevice);
                    g_last_device_id   = id;
                    g_last_full_layout = g_cfg.full_layout_devices.count(id) > 0;
                }
            }
        }
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

static bool init_raw_input(HINSTANCE hInst) {
    WNDCLASSA wc{};
    wc.lpfnWndProc   = RawInputWndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = "KeyboardRemapRawInputWnd";
    RegisterClassA(&wc);

    g_msg_window = CreateWindowA("KeyboardRemapRawInputWnd", "", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g_msg_window) return false;

    RAWINPUTDEVICE rid{};
    rid.usUsagePage = 0x01; // Generic Desktop
    rid.usUsage     = 0x06; // Keyboard
    rid.dwFlags     = RIDEV_INPUTSINK;
    rid.hwndTarget  = g_msg_window;
    return RegisterRawInputDevices(&rid, 1, sizeof(rid)) == TRUE;
}

// ─── SendInput helpers ──────────────────────────────────────────────────────
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

// ─── LL Keyboard Hook ───────────────────────────────────────────────────────
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

    bool full_layout = g_last_full_layout;

    // CapsLock — только отслеживаем, не пропускаем в систему
    if (vk == VK_CAPITAL) {
        g_caps_held = down;
        if (!down) {
            for (DWORD k : g_consumed_while_caps) {
                auto it = NAV_MAP.find(k);
                if (it != NAV_MAP.end())
                    send_vk(it->second, false);
            }
            g_consumed_while_caps.clear();
        }
        return 1;
    }

    if (vk == VK_LCONTROL) g_ctrl_held  = down;
    if (vk == VK_LSHIFT)   g_shift_held = down;

    // ─── Функции, доступные только для устройств из
    // full_layout_devices (обычно — сплит-клавиатуры) ───
    if (full_layout) {

        if (vk == VK_LMENU) {
            g_alt_held = down;
            if (down) {
                send_vk(VK_LMENU,    true);
                send_vk(VK_RCONTROL, true);
            } else {
                send_vk(VK_RCONTROL, false);
                send_vk(VK_LMENU,    false);
            }
            return 1;
        }

        // Alt+Space → Win+Space
        if (g_alt_held && vk == VK_SPACE) {
            if (down) {
                send_vk(VK_RCONTROL, false);
                send_vk(VK_LMENU,    false);
                send_win_space();
                send_vk(VK_LMENU,    true);
                send_vk(VK_RCONTROL, true);
            }
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
    }
    // ─── конец full_layout-функций ───
    else {
        // На "базовых" устройствах Alt и остальные модификаторы должны
        // работать как обычно — просто отслеживаем Alt для консистентности.
        if (vk == VK_LMENU) g_alt_held = down;
    }

    // Shift+Backspace → Delete (базовая функция, всегда доступна)
    if (g_shift_held && vk == VK_BACK) {
        send_vk(VK_LSHIFT, false);
        send_vk(VK_DELETE, down);
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
        auto it = NAV_MAP.find(vk);
        if (it != NAV_MAP.end())
            send_vk(it->second, false);
        return 1;
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

// ─── main ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    g_cfg = load_config();

    if (!init_raw_input(hInst))
        return 1;

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