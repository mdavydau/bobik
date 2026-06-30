# mBobik 🤖

Мой Tabbie — DIY desktop robot buddy.

## Структура

```
firmware/     — ESP32 прошивка (PlatformIO)
app/          — React dashboard
hardware/     — 3D STL файлы для печати корпуса
docs/         — документация
scripts/      — скрипты управления
  demo_expressions.sh  — демо всех выражений лица
  tabbie_mood.sh       — авто-режим по времени суток
  make_face.py         — генерация лиц
```

## Быстрый старт

1. Подключи OLED → ESP32 (GND/VCC/SCL→D22/SDA→D21)
2. Настрой WiFi: `firmware/src/.env`
3. Прошей: `cd firmware && pio run --target upload`
4. Запусти дашборд: `cd app && npm install && npm run dev`

## Управление

```bash
./scripts/demo_expressions.sh     # показать все лица
./scripts/tabbie_mood.sh          # авто-режим (angry после 18:00)
```
