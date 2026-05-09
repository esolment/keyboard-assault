#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/time.h>
#include <sys/inotify.h>
#include <dirent.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <algorithm>

#define INPUT_DIR "/dev/input"
#define SECRET_SEQUENCE "1234"
#define DELETE_REPEAT_DELAY_US 150000

const std::map<int, int> NAV_MAP = {
    {KEY_I, KEY_UP},
    {KEY_J, KEY_LEFT},
    {KEY_K, KEY_DOWN},
    {KEY_L, KEY_RIGHT},
    {KEY_U, KEY_HOME},
    {KEY_O, KEY_END},
};

const std::map<int, char> KEY_CHAR = {
    {KEY_0,'0'},{KEY_1,'1'},{KEY_2,'2'},{KEY_3,'3'},{KEY_4,'4'},
    {KEY_5,'5'},{KEY_6,'6'},{KEY_7,'7'},{KEY_8,'8'},{KEY_9,'9'},
    {KEY_A,'a'},{KEY_B,'b'},{KEY_C,'c'},{KEY_D,'d'},{KEY_E,'e'},
    {KEY_F,'f'},{KEY_G,'g'},{KEY_H,'h'},{KEY_I,'i'},{KEY_J,'j'},
    {KEY_K,'k'},{KEY_L,'l'},{KEY_M,'m'},{KEY_N,'n'},{KEY_O,'o'},
    {KEY_P,'p'},{KEY_Q,'q'},{KEY_R,'r'},{KEY_S,'s'},{KEY_T,'t'},
    {KEY_U,'u'},{KEY_V,'v'},{KEY_W,'w'},{KEY_X,'x'},{KEY_Y,'y'},
    {KEY_Z,'z'},
};

int ui_fd = -1;

void emit(int type, int code, int value) {
    struct input_event ev{};
    ev.type  = type;
    ev.code  = code;
    ev.value = value;
    write(ui_fd, &ev, sizeof(ev));
}

void syn() { emit(EV_SYN, SYN_REPORT, 0); }

void send_key(int code, int value) {
    emit(EV_KEY, code, value);
    syn();
}

void send_ctrl_backspace() {
    emit(EV_KEY, KEY_LEFTCTRL,  1);
    emit(EV_KEY, KEY_BACKSPACE, 1);
    emit(EV_KEY, KEY_BACKSPACE, 0);
    emit(EV_KEY, KEY_LEFTCTRL,  0);
    syn();
}

void send_super_space() {
    emit(EV_KEY, KEY_LEFTMETA, 1);
    emit(EV_KEY, KEY_SPACE,    1);
    emit(EV_KEY, KEY_SPACE,    0);
    emit(EV_KEY, KEY_LEFTMETA, 0);
    syn();
}

long now_us() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return tv.tv_sec * 1000000L + tv.tv_usec;
}

bool is_keyboard(int fd) {
    unsigned char evbit[EV_MAX / 8 + 1]   = {};
    unsigned char keybit[KEY_MAX / 8 + 1] = {};
    ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), evbit);
    ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keybit)), keybit);
    bool has_ev_key = evbit[EV_KEY / 8] & (1 << (EV_KEY % 8));
    bool has_key_a  = keybit[KEY_A / 8]  & (1 << (KEY_A  % 8));
    return has_ev_key && has_key_a;
}

std::vector<std::string> get_event_devices() {
    std::vector<std::string> result;
    DIR* dir = opendir(INPUT_DIR);
    if (!dir) return result;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.rfind("event", 0) == 0)
            result.push_back(std::string(INPUT_DIR) + "/" + name);
    }
    closedir(dir);
    std::sort(result.begin(), result.end());
    return result;
}

int setup_uinput() {
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) { perror("open uinput"); return -1; }

    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    for (int i = 0; i < KEY_MAX; i++)
        ioctl(fd, UI_SET_KEYBIT, i);

    struct uinput_setup usetup{};
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x1234;
    usetup.id.product = 0x5678;
    strcpy(usetup.name, "virtual-keyboard");
    ioctl(fd, UI_DEV_SETUP, &usetup);
    ioctl(fd, UI_DEV_CREATE);
    return fd;
}

void release_all_keys(int dev_fd) {
    unsigned char keystate[KEY_MAX / 8 + 1] = {};
    if (ioctl(dev_fd, EVIOCGKEY(sizeof(keystate)), keystate) < 0)
        return;
    for (int i = 0; i < KEY_MAX; i++) {
        if (keystate[i / 8] & (1 << (i % 8))) {
            emit(EV_KEY, i, 0);
            syn();
        }
    }
}

std::string detect_keyboard() {
    struct DevState {
        std::string path;
        int fd;
        std::string buf;
    };
    std::vector<DevState> devs;

    auto rescan = [&]() {
        for (auto& d : devs) close(d.fd);
        devs.clear();
        for (auto& path : get_event_devices()) {
            int fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
            if (fd < 0) continue;
            if (!is_keyboard(fd)) { close(fd); continue; }
            devs.push_back({path, fd, ""});
        }
    };

    rescan();

    int ino    = inotify_init1(IN_NONBLOCK);
    int ino_wd = inotify_add_watch(ino, INPUT_DIR, IN_CREATE | IN_DELETE);

    auto make_epoll = [&]() {
        int ep = epoll_create1(0);
        struct epoll_event ev{};
        ev.events = EPOLLIN;
        for (auto& d : devs) {
            ev.data.fd = d.fd;
            epoll_ctl(ep, EPOLL_CTL_ADD, d.fd, &ev);
        }
        ev.data.fd = ino;
        epoll_ctl(ep, EPOLL_CTL_ADD, ino, &ev);
        return ep;
    };

    int ep = make_epoll();
    std::string found;
    int found_last_keycode = -1;
    const std::string seq = SECRET_SEQUENCE;
    struct epoll_event events[32];
    struct input_event ie;

    while (found.empty()) {
        int n = epoll_wait(ep, events, 32, 2000);

        char ibuf[4096];
        if (read(ino, ibuf, sizeof(ibuf)) > 0) {
            usleep(100000);
            close(ep);
            rescan();
            ep = make_epoll();
            continue;
        }

        if (n <= 0) continue;

        for (int i = 0; i < n && found.empty(); i++) {
            int efd = events[i].data.fd;
            if (efd == ino) continue;

            DevState* ds = nullptr;
            for (auto& d : devs) {
                if (d.fd == efd) { ds = &d; break; }
            }
            if (!ds) continue;

            while (read(efd, &ie, sizeof(ie)) > 0) {
                if (ie.type != EV_KEY || ie.value != 1) continue;

                auto it = KEY_CHAR.find(ie.code);
                if (it == KEY_CHAR.end()) {
                    ds->buf.clear();
                    continue;
                }

                ds->buf += it->second;
                if (ds->buf.size() > seq.size())
                    ds->buf = ds->buf.substr(ds->buf.size() - seq.size());

                if (ds->buf == seq) {
                    found = ds->path;
                    found_last_keycode = ie.code;
                    break;
                }
            }
        }
    }

    // Ждём key-up последней клавиши последовательности
    if (found_last_keycode != -1) {
        bool got_keyup = false;
        while (!got_keyup) {
            int n = epoll_wait(ep, events, 32, 1000);
            if (n <= 0) break;
            for (int i = 0; i < n && !got_keyup; i++) {
                int efd = events[i].data.fd;
                if (efd == ino) continue;
                while (read(efd, &ie, sizeof(ie)) > 0) {
                    if (ie.type == EV_KEY && ie.code == found_last_keycode && ie.value == 0) {
                        got_keyup = true;
                        break;
                    }
                }
            }
        }
    }

    for (auto& d : devs) close(d.fd);
    inotify_rm_watch(ino, ino_wd);
    close(ino);
    close(ep);
    return found;
}

bool run_keyboard(const std::string& path) {
    int dev_fd = open(path.c_str(), O_RDONLY | O_NONBLOCK);
    if (dev_fd < 0) { perror("open device"); return false; }

    if (ioctl(dev_fd, EVIOCGRAB, 1) < 0) {
        perror("grab"); close(dev_fd); return false;
    }

    release_all_keys(dev_fd);

    char name[256] = {};
    ioctl(dev_fd, EVIOCGNAME(sizeof(name)), name);

    int ep = epoll_create1(0);
    struct epoll_event ev{};
    ev.events  = EPOLLIN;
    ev.data.fd = dev_fd;
    epoll_ctl(ep, EPOLL_CTL_ADD, dev_fd, &ev);

    bool caps_held      = false;
    bool alt_held       = false;
    bool ctrl_held      = false;
    bool shift_held     = false;
    bool backspace_held = false;
    long last_delete_us = 0;
    std::set<int> consumed_while_caps;

    struct epoll_event events[16];
    struct input_event ie;
    bool disconnected = false;

    while (!disconnected) {
        int n = epoll_wait(ep, events, 16, -1);
        if (n < 0) break;

        for (int i = 0; i < n && !disconnected; i++) {
            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                disconnected = true;
                break;
            }

            ssize_t r;
            while ((r = read(dev_fd, &ie, sizeof(ie))) > 0) {
                if (ie.type != EV_KEY) continue;

                int keycode  = ie.code;
                int keystate = ie.value;

                // Caps Lock
                if (keycode == KEY_CAPSLOCK) {
                    caps_held = (keystate != 0);
                    continue;
                }

                // Модификаторы — обновляем первыми
                if (keycode == KEY_LEFTALT)   alt_held   = (keystate != 0);
                if (keycode == KEY_LEFTCTRL)  ctrl_held  = (keystate != 0);
                if (keycode == KEY_LEFTSHIFT) shift_held = (keystate != 0);

                // Alt+Space → Super+Space
                if (alt_held && keycode == KEY_SPACE) {
                    if (keystate == 1) {
                        emit(EV_KEY, KEY_LEFTALT, 0); syn();
                        send_super_space();
                        emit(EV_KEY, KEY_LEFTALT, 1); syn();
                    }
                    continue;
                }

                // Shift+Backspace → Delete
                if (shift_held && keycode == KEY_BACKSPACE) {
                    emit(EV_KEY, KEY_LEFTSHIFT, 0); syn();
                    send_key(KEY_DELETE, keystate);
                    continue;
                }

                // Ctrl+F1 → Mute
                if (ctrl_held && keycode == KEY_F1) {
                    emit(EV_KEY, KEY_LEFTCTRL, 0); syn();
                    send_key(KEY_MUTE, keystate);
                    continue;
                }

                // Ctrl+F2 → Volume Down
                if (ctrl_held && keycode == KEY_F2) {
                    emit(EV_KEY, KEY_LEFTCTRL, 0); syn();
                    send_key(KEY_VOLUMEDOWN, keystate);
                    continue;
                }

                // Ctrl+F3 → Volume Up
                if (ctrl_held && keycode == KEY_F3) {
                    emit(EV_KEY, KEY_LEFTCTRL, 0); syn();
                    send_key(KEY_VOLUMEUP, keystate);
                    continue;
                }

                // RightShift → /
                if (keycode == KEY_RIGHTSHIFT) {
                    send_key(KEY_SLASH, keystate);
                    continue;
                }

                // KEY_UP → RightShift (без Caps)
                if (keycode == KEY_UP && !caps_held) {
                    send_key(KEY_RIGHTSHIFT, keystate);
                    continue;
                }

                // / → KEY_UP
                if (keycode == KEY_SLASH) {
                    send_key(KEY_UP, keystate);
                    continue;
                }

                // Caps+Backspace → удаление слова
                if (keycode == KEY_BACKSPACE)
                    backspace_held = (keystate != 0);

                if (caps_held && backspace_held) {
                    long now = now_us();
                    if (now - last_delete_us > DELETE_REPEAT_DELAY_US) {
                        send_ctrl_backspace();
                        last_delete_us = now;
                    }
                    continue;
                }

                // Caps + навигация
                if (caps_held) {
                    auto it = NAV_MAP.find(keycode);
                    if (it != NAV_MAP.end()) {
                        send_key(it->second, keystate);
                        consumed_while_caps.insert(keycode);
                        continue;
                    }
                }

                if (consumed_while_caps.count(keycode) && keystate == 0) {
                    consumed_while_caps.erase(keycode);
                    continue;
                }

                // Всё остальное — как есть
                emit(ie.type, keycode, keystate);
                syn();
            }

            if (r == 0 || (r < 0 && errno != EAGAIN && errno != EINTR)) {
                disconnected = true;
            }
        }
    }

    ioctl(dev_fd, EVIOCGRAB, 0);
    close(dev_fd);
    close(ep);
    return true;
}

int main() {
    ui_fd = setup_uinput();
    if (ui_fd < 0) return 1;

    usleep(200000);

    while (true) {
        std::string path = detect_keyboard();
        if (!path.empty())
            run_keyboard(path);
    }

    ioctl(ui_fd, UI_DEV_DESTROY);
    close(ui_fd);
    return 0;
}