# Тузик (Tuzik) — Pixel Art Player на ESP32-P4

**Репозиторий:** [fabkury/p3a](https://github.com/fabkury/p3a) — ⭐85 — Apache 2.0
**Плата:** Waveshare ESP32-P4-WIFI6-Touch-LCD-4B — **$39.99**

## Что это

Пиксель-арт фоторамка на 4" IPS 720×720 с触摸. Показывает:
- Makapix Club (соцсеть пиксель-арта)
- Giphy (трендовые гифки)
- Музеи через IIIF (Чикаго, Rijksmuseum, V&A...)
- PICO-8 игры через WebSocket
- Свои файлы через USB/Wi-Fi

Всё open source, Apache 2.0. ⭐85 на GitHub.

## Железо

- ESP32-P4 (RISC-V dual-core + single-core) + ESP32-C6 (Wi-Fi 6/BLE)
- 32MB PSRAM + 32MB Flash
- 4" IPS 720×720, ёмкостный touch 5 точек (GT911)
- microSD (SDIO 3.0)
- Два микрофона + спикер (ES8311 + ES7210)
- MIPI-CSI для камеры

## Связь с Bobik

| | Bobik | Tuzik |
|---|---|---|
| Чип | ESP32 WROOM | ESP32-P4 + C6 |
| PSRAM | 520KB | 32MB |
| Экран | 1.3" OLED 128×64 | 4" IPS 720×720 |
| WiFi | 4 | 6 |
| Цена | ~60 zł | $39.99 |

Tuzik — это Bobik на стероидах. В 60× больше памяти,
в 30× больше пикселей. Для тяжёлого арта и анимации.

## Что можно подсмотреть в код

- Play Scheduler — алгоритм показа контента
- Triple buffering + VSYNC
- REST API для управления
- OTA обновления
- Web UI dashboard
