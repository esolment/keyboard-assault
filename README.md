# keyboard-assault

Низкоуровневый ремаппер клавиатуры для Linux. Работает через `/dev/input` и `uinput`, перехватывает события на уровне ядра — работает в любом окружении (X11, Wayland, TTY).

## Возможности

| Сочетание | Действие |
|---|---|
| `Caps + I/J/K/L` | Стрелки ↑ ← ↓ → |
| `Caps + U` | Home |
| `Caps + O` | End |
| `Caps + Backspace` | Удалить слово назад (Ctrl+Backspace) |
| `Shift + Backspace` | Delete |
| `Ctrl + F1` | Mute / Unmute |
| `Ctrl + F2` | Громкость — |
| `Ctrl + F3` | Громкость + |
| `Alt + Space` | Super+Space (смена раскладки) |
| `RightShift` | / ? |
| `физический /` | ↑ |

Caps Lock используется исключительно как модификатор и не переключает регистр.

---

## Сборка

### Зависимости

```bash
# Debian / Ubuntu
sudo apt install g++ linux-headers-$(uname -r)

# Arch
sudo pacman -S gcc
```

### Компиляция

```bash
g++ -O2 -o keyboard main.cpp
```

---

## Настройка

Откройте `main.cpp` и измените нужные параметры вверху файла:

```cpp
// Секретная последовательность для привязки клавиатуры
#define SECRET_SEQUENCE "1234"

// Задержка повторения удаления слова (в микросекундах)
#define DELETE_REPEAT_DELAY_US 150000
```

Для изменения навигационных клавиш отредактируйте `NAV_MAP`:

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

После изменений — пересоберите:

```bash
g++ -O2 -o keyboard main.cpp
```

---

## Привязка клавиатуры

Программа не использует фиксированный путь к устройству. Вместо этого при каждом запуске она ждёт ввода секретной последовательности и автоматически определяет нужную клавиатуру.

**Как это работает:**

1. Программа слушает все клавиатуры в `/dev/input` одновременно
2. Введите секретную последовательность (по умолчанию `1234`) на нужной клавиатуре
3. Программа привязывается к этому устройству
4. Если клавиатура отключается — программа снова ждёт последовательности
5. Новые устройства подхватываются автоматически через `inotify`

Последовательность вводится системно — не нужно фокусироваться на каком-либо окне или терминале.

---

## Добавление в автозагрузку (systemd)

### 1. Скопируйте бинарник

```bash
sudo cp keyboard /usr/local/bin/keyboard-assault
sudo chmod +x /usr/local/bin/keyboard-assault
```

### 2. Создайте systemd-сервис

```bash
sudo nano /etc/systemd/system/keyboard-assault.service
```

Содержимое файла:

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

### 3. Включите и запустите сервис

```bash
# Перезагрузить конфигурацию systemd
sudo systemctl daemon-reload

# Включить автозапуск при загрузке
sudo systemctl enable keyboard-assault

# Запустить сейчас
sudo systemctl start keyboard-assault
```

### 4. Проверьте статус

```bash
sudo systemctl status keyboard-assault
```

### Управление сервисом

```bash
# Остановить
sudo systemctl stop keyboard-assault

# Перезапустить (например после пересборки)
sudo systemctl restart keyboard-assault

# Посмотреть логи
sudo journalctl -u keyboard-assault -f
```

---

## Устранение неполадок

**Программа не видит клавиатуру**

Убедитесь что запускаете с `sudo` или что пользователь состоит в группе `input`:

```bash
sudo usermod -aG input $USER
# После этого нужно перелогиниться
```

**После пересборки нужно перезапустить сервис**

```bash
sudo cp keyboard /usr/local/bin/keyboard-assault
sudo systemctl restart keyboard-assault
```

**Посмотреть какие устройства определяются как клавиатуры**

```bash
cat /proc/bus/input/devices | grep -A5 "EV="
```

**Проверить коды клавиш**

```bash
sudo evtest
```

---

## Как работает внутри

Программа использует два механизма Linux:

- **evdev** (`/dev/input/eventX`) — чтение сырых событий с физической клавиатуры. После `EVIOCGRAB` устройство полностью захватывается — события больше не попадают в систему напрямую.
- **uinput** (`/dev/uinput`) — создание виртуального устройства, через которое отправляются преобразованные события.
- **epoll** — эффективное ожидание событий без busy loop. CPU в простое ~0%.
- **inotify** — отслеживание появления новых устройств в `/dev/input` без polling.

---

**Вдохновлено (EPKL)[https://github.com/DreymaR/BigBagKbdTrixPKL]**