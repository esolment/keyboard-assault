# keyboard-assault

Низкоуровневый ремаппер клавиатуры для Linux и Windows. На Linux работает через `/dev/input` и `uinput`, перехватывает события на уровне ядра — функционирует в любом окружении (X11, Wayland, TTY). На Windows использует Low-Level Keyboard Hook.

Набор активных функций зависит от конкретного устройства: базовый remap работает на любой клавиатуре, а расширенный набор (заточенный под сплит-клавиатуры с нестандартным расположением клавиш) включается только для устройств, явно перечисленных в конфиге.

---

## Возможности

### Базовые (работают на любой клавиатуре)

| Сочетание           | Действие                             |
| ------------------- | ------------------------------------ |
| `Caps + I/J/K/L`    | Стрелки ↑ ← ↓ →                      |
| `Caps + U`          | Home                                 |
| `Caps + O`          | End                                  |
| `Caps + Backspace`  | Удалить слово назад (Ctrl+Backspace) |
| `Shift + Backspace` | Delete                               |

Caps Lock используется исключительно как модификатор и не переключает регистр.

### Расширенные (только для устройств из `full_layout_devices`)

| Сочетание      | Действие                 |
| -------------- | ------------------------ |
| `Ctrl + F1`    | Mute / Unmute             |
| `Ctrl + F2`    | Громкость —               |
| `Ctrl + F3`    | Громкость +               |
| `Alt + Space`  | Super+Space / Win+Space   |
| `RightShift`   | / ?                       |
| физический `/` | ↑                         |

Эти сочетания рассчитаны на сплит-клавиатуру, где соответствующие клавиши находятся в неудобных местах. На обычной клавиатуре они не нужны и мешают — поэтому включаются автоматически только для устройств, ID которых указан в конфиге, и не затрагивают остальные клавиатуры.

---

## Конфиг

Секретная последовательность и список устройств хранятся в `config.ini`.

**Linux:** `~/.config/keyboard-assault/config.ini` (или `$XDG_CONFIG_HOME/keyboard-assault/config.ini`)
**Windows:** `config.ini` рядом с `keyboard_assault.exe`

```ini
# Последовательность символов (a-z, 0-9) для привязки к устройству
secret_sequence=1234

# Устройства (vendor:product, hex), для которых включён полный набор функций
[full_layout_devices]
046d:c52b
1234:5678
```

Если файла нет, используются значения по умолчанию: `secret_sequence=1234` и пустой список устройств (то есть везде работает только базовый remap).

### Как узнать vendor:product своей клавиатуры

**Linux:**

```bash
cat /proc/bus/input/devices | grep -A5 -B2 "Vendor"
```

Формат вывода: `Vendor=046d Product=c52b` — это и есть нужные значения.

**Windows:**

Диспетчер устройств → Клавиатуры → нужное устройство → Свойства → Сведения → «ИД оборудования». Там будет строка вида `HID\VID_046D&PID_C52B...` — берите `VID_` и `PID_` в hex.

Добавьте пару в секцию `[full_layout_devices]` конфига в формате `vendor:product` (без `VID_`/`PID_`/`0x`, только hex-цифры, регистр не важен).

---

## Привязка клавиатуры к устройству (Linux)

Программа не использует фиксированный путь к устройству и не требует указывать `eventX` в конфиге — она определяет, к какой клавиатуре привязаться, по вводу секретной последовательности, а полный или базовый набор функций выбирается автоматически по vendor:product ID устройства.

**Как это работает:**

1. Слушает все устройства в `/dev/input`
2. Ожидает ввод секретной последовательности (по умолчанию `1234`)
3. Привязывается к устройству, на котором она введена
4. Определяет vendor:product ID устройства и включает полный или базовый набор функций согласно конфигу
5. При отключении устройства — возвращается в режим ожидания
6. Новые устройства отслеживаются через `inotify`

Ввод последовательности не требует фокуса окна. Привязка идёт не по имени файла устройства (`eventX` может меняться между переподключениями), а по vendor:product ID, который остаётся постоянным.

---

## Linux

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

### Установка как сервис (systemd, user-сервис)

**1. Установка бинарника и конфига:**

```bash
sudo cp build/linux/keyboard_assault /usr/local/bin/keyboard-assault
sudo chmod +x /usr/local/bin/keyboard-assault

mkdir -p ~/.config/keyboard-assault
cp config.example.ini ~/.config/keyboard-assault/config.ini
```

Отредактируйте `~/.config/keyboard-assault/config.ini` под свои устройства.

**2. Создание сервиса:**

```bash
mkdir -p ~/.config/systemd/user
nano ~/.config/systemd/user/keyboard-assault.service
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
WantedBy=default.target
```

**3. Запуск:**

```bash
systemctl --user daemon-reload
systemctl --user enable --now keyboard-assault
```

**4. Управление:**

```bash
systemctl --user stop keyboard-assault
systemctl --user restart keyboard-assault
journalctl --user -u keyboard-assault -f
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
systemctl --user restart keyboard-assault
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
- **EVIOCGID** — получение vendor:product ID устройства для выбора набора функций

CPU в простое ≈ 0%.

---

## Windows

Работает для всех подключённых клавиатур. Raw Input используется для определения vendor:product ID устройства, с которого пришло нажатие (сам `WH_KEYBOARD_LL` этой информации не предоставляет) — набор функций выбирается по этому ID согласно конфигу, как и на Linux.

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

Положите `config.ini` рядом с `keyboard_assault.exe` (см. `config.example.ini`), затем запустите `keyboard_assault.exe` — окна не появится, программа работает в фоне. Для остановки — завершить процесс через Диспетчер задач.

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
- **Raw Input (`WM_INPUT`)** — определение vendor:product ID устройства, с которого пришло нажатие
- **SendInput** — генерация виртуальных нажатий взамен перехваченных

---

## Настройка

Секретная последовательность и список устройств задаются в `config.ini` (см. раздел «Конфиг» выше). Изменения в `config.ini` применяются со следующего запуска программы — после правки перезапустите процесс или сервис.

Навигационная таблица (`NAV_MAP`: Caps+I/J/K/L/U/O) и задержка повтора удаления слова задаются в исходном коде:

```cpp
// Linux
#define DELETE_REPEAT_DELAY_US 150000

// Windows
static const DWORD DELETE_REPEAT_MS = 150;
```

После изменений пересоберите проект:

```bash
cmake --build build
```

---

## Вдохновение

https://github.com/DreymaR/BigBagKbdTrixPKL