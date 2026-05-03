# keyboard-assault

Низкоуровневый ремаппер клавиатуры для Linux. Работает через `/dev/input` и `uinput`, перехватывает события на уровне ядра — функционирует в любом окружении (X11, Wayland, TTY).

---

## Возможности

| Сочетание           | Действие                             |
| ------------------- | ------------------------------------ |
| `Caps + I/J/K/L`    | Стрелки ↑ ← ↓ →                      |
| `Caps + U`          | Home                                 |
| `Caps + O`          | End                                  |
| `Caps + Backspace`  | Удалить слово назад (Ctrl+Backspace) |
| `Shift + Backspace` | Delete                               |
| `Ctrl + F1`         | Mute / Unmute                        |
| `Ctrl + F2`         | Громкость —                          |
| `Ctrl + F3`         | Громкость +                          |
| `Alt + Space`       | Super+Space (смена раскладки)        |
| `RightShift`        | / ?                                  |
| `физический /`      | ↑                                    |

Caps Lock используется исключительно как модификатор и не переключает регистр.

---

## Сборка

### Зависимости

```bash
# Debian / Ubuntu
sudo apt install cmake g++ linux-headers-$(uname -r)

# Arch
sudo pacman -S cmake gcc
```

---

### Компиляция

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Бинарник будет находиться в:

```bash
./build/keyboard_assault
```

---

## Настройка

Откройте `main.cpp` и измените параметры в начале файла:

```cpp
#define SECRET_SEQUENCE "1234"
#define DELETE_REPEAT_DELAY_US 150000
```

Для изменения навигации:

```cpp
const std::map<int, int> NAV_MAP = {
    {KEY_I, KEY_UP},
    {KEY_J, KEY_LEFT},
    {KEY_K, KEY_DOWN},
    {KEY_L, KEY_RIGHT},
    {KEY_U, KEY_HOME},
    {KEY_O, KEY_END},
};
```

После изменений пересоберите проект:

```bash
cmake --build build
```

---

## Привязка клавиатуры

Программа не использует фиксированный путь к устройству.

**Как это работает:**

1. Слушает все устройства в `/dev/input`
2. Ожидает ввод секретной последовательности (по умолчанию `1234`)
3. Привязывается к устройству, на котором она введена
4. При отключении устройства — возвращается в режим ожидания
5. Новые устройства отслеживаются через `inotify`

Ввод последовательности не требует фокуса окна.

---

## Установка как сервис (systemd)

### 1. Установка бинарника

```bash
sudo cp build/keyboard_assault /usr/local/bin/keyboard-assault
sudo chmod +x /usr/local/bin/keyboard-assault
```

---

### 2. Создание сервиса

```bash
sudo nano /etc/systemd/system/keyboard-assault.service
```

```ini
[Unit]
Description=Keyboard remapper
After=systemd-udev-settle.service
Wants=systemd-udev-settle.service

[Service]
Type=simple
ExecStart=/usr/local/bin/keyboard-assault
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
```

---

### 3. Запуск

```bash
sudo systemctl daemon-reload
sudo systemctl enable keyboard-assault
sudo systemctl start keyboard-assault
```

---

### 4. Проверка

```bash
sudo systemctl status keyboard-assault
```

---

## Управление

```bash
sudo systemctl stop keyboard-assault
sudo systemctl restart keyboard-assault
sudo journalctl -u keyboard-assault -f
```

---

## Устранение неполадок

### Нет доступа к клавиатуре

```bash
sudo usermod -aG input $USER
```

(после этого перелогиниться)

---

### Пересборка и обновление

```bash
cmake --build build
sudo cp build/keyboard_assault /usr/local/bin/keyboard-assault
sudo systemctl restart keyboard-assault
```

---

### Проверка устройств

```bash
cat /proc/bus/input/devices | grep -A5 "EV="
```

---

### Проверка событий клавиш

```bash
sudo evtest
```

---

## Как это работает

* **evdev** — чтение событий с `/dev/input/eventX`
* **uinput** — создание виртуального устройства
* **epoll** — эффективное ожидание событий
* **inotify** — отслеживание новых устройств

CPU в простое ≈ 0%.

---

## Вдохновение

https://github.com/DreymaR/BigBagKbdTrixPKL