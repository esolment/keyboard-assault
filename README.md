# keyboard-assault

Низкоуровневый ремаппер клавиатуры для Linux и Windows. На Linux работает через `/dev/input` и `uinput`, перехватывает события на уровне ядра — функционирует в любом окружении (X11, Wayland, TTY). На Windows использует Low-Level Keyboard Hook.

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
| `Alt + Space`       | Super+Space / Win+Space              |
| `RightShift`        | / ?                                  |
| `физический /`      | ↑                                    |

Caps Lock используется исключительно как модификатор и не переключает регистр.

---

## Linux

### Привязка клавиатуры

Программа не использует фиксированный путь к устройству.

**Как это работает:**

1. Слушает все устройства в `/dev/input`
2. Ожидает ввод секретной последовательности (по умолчанию `1234`)
3. Привязывается к устройству, на котором она введена
4. При отключении устройства — возвращается в режим ожидания
5. Новые устройства отслеживаются через `inotify`

Ввод последовательности не требует фокуса окна.

### Зависимости

```bash
# Debian / Ubuntu
sudo apt install cmake g++ linux-headers-$(uname -r)

# Arch
sudo pacman -S cmake gcc
```

### Сборка

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Бинарник:

```
./build/linux/keyboard_assault
```

### Установка как сервис (systemd)

**1. Установка бинарника:**

```bash
sudo cp build/linux/keyboard_assault /usr/local/bin/keyboard-assault
sudo chmod +x /usr/local/bin/keyboard-assault
```

**2. Создание сервиса:**

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

**3. Запуск:**

```bash
sudo systemctl daemon-reload
sudo systemctl enable keyboard-assault
sudo systemctl start keyboard-assault
```

**4. Управление:**

```bash
sudo systemctl stop keyboard-assault
sudo systemctl restart keyboard-assault
sudo journalctl -u keyboard-assault -f
```

### Устранение неполадок

**Нет доступа к клавиатуре:**

```bash
sudo usermod -aG input $USER
```

После этого перелогиниться.

**Пересборка и обновление:**

```bash
cmake --build build
sudo cp build/linux/keyboard_assault /usr/local/bin/keyboard-assault
sudo systemctl restart keyboard-assault
```

**Проверка устройств:**

```bash
cat /proc/bus/input/devices | grep -A5 "EV="
```

**Проверка событий клавиш:**

```bash
sudo evtest
```

### Как это работает

- **evdev** — чтение событий с `/dev/input/eventX`
- **uinput** — создание виртуального устройства
- **epoll** — эффективное ожидание событий
- **inotify** — отслеживание новых устройств

CPU в простое ≈ 0%.

---

## Windows

Работает для всех подключённых клавиатур.

### Права администратора

`WH_KEYBOARD_LL` — user-mode хук, запускается от обычного пользователя. Однако Windows не доставляет события хука в процессы с более высоким уровнем привилегий, чем у самого хука. Это означает:

| Ситуация | Нужны права администратора? |
| --- | --- |
| Обычные приложения (браузер, редактор, терминал) | Нет |
| UAC-диалоги | Да |
| Приложения, запущенные от администратора | Да |

Если ремаппинг нужен везде — запускайте от администратора.

### Зависимости

Для кросс-компиляции из Linux:

```bash
# Debian / Ubuntu
sudo apt install cmake mingw-w64

# Arch
sudo pacman -S cmake mingw-w64-gcc
```

### Сборка из Linux (кросс-компиляция)

```bash
cmake -B build-win \
  -DCMAKE_SYSTEM_NAME=Windows \
  -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build-win
```

Бинарник:

```
./build-win/windows/keyboard_assault.exe
```

### Сборка на Windows

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Запуск

Просто запустить `keyboard_assault.exe` — окна не появится, программа работает в фоне. Для остановки — завершить процесс через Диспетчер задач.

### Автозапуск при входе в систему

**Без прав администратора** — через папку автозагрузки:

```
Win+R → shell:startup → скопировать ярлык на keyboard_assault.exe
```

**С правами администратора** — через Планировщик задач (иначе программа стартует без повышения, даже если вы администратор):

1. `Win+R` → `taskschd.msc`
2. «Создать задачу» → вкладка «Общие»
3. Поставить галку **«Выполнять с наивысшими правами»**
4. Вкладка «Триггеры» → «Создать» → **При входе в систему**
5. Вкладка «Действия» → «Создать» → указать путь к `keyboard_assault.exe`

### Как это работает

- **WH_KEYBOARD_LL** — системный хук, перехватывает нажатия со всех клавиатур до того, как они достигают приложений
- **SendInput** — генерация виртуальных нажатий взамен перехваченных

---

## Настройка

Откройте `main.cpp` нужной платформы и измените параметры в начале файла.

**Задержка повтора удаления слова:**

```cpp
// Linux
#define DELETE_REPEAT_DELAY_US 150000

// Windows
static const DWORD DELETE_REPEAT_MS = 150;
```

**Секретная последовательность (только Linux):**

```cpp
#define SECRET_SEQUENCE "1234"
```

**Навигационные сочетания:**

```cpp
// Linux
const std::map<int, int> NAV_MAP = {
    {KEY_I, KEY_UP},
    {KEY_J, KEY_LEFT},
    {KEY_K, KEY_DOWN},
    {KEY_L, KEY_RIGHT},
    {KEY_U, KEY_HOME},
    {KEY_O, KEY_END},
};

// Windows
static const std::map<DWORD, DWORD> NAV_MAP = {
    {'I', VK_UP},
    {'J', VK_LEFT},
    {'K', VK_DOWN},
    {'L', VK_RIGHT},
    {'U', VK_HOME},
    {'O', VK_END},
};
```

После изменений пересоберите проект:

```bash
cmake --build build
```

---

## Вдохновение

https://github.com/DreymaR/BigBagKbdTrixPKL