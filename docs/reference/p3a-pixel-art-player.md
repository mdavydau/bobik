# p3a — Pixel Art Player (ESP32-P4)

**Repo:** [fabkury/p3a](https://github.com/fabkury/p3a) — ⭐85 — Apache 2.0
**Board:** Waveshare ESP32-P4-WIFI6-Touch-LCD-4B — **$39.99**

## Зачем

Пиксель-арт фоторамка на 4" IPS экране. Анимированные GIF, pixel art,
картины из музеев через IIIF, PICO-8 игры. Open source.

## Железо

| Компонент | Что |
|---|---|
| **ESP32-P4** | Dual-core RISC-V, 32MB PSRAM, 32MB Flash |
| **ESP32-C6** | Wi-Fi 6 / BLE 5 — отдельный чип |
| **Экран** | 4" IPS 720×720, 24-bit, ёмкостный touch 5 точек |
| **SD-карта** | microSD (4-bit SDMMC) |
| **Питание** | USB-C (без батареи) |
| **Цена** | $39.99 (Waveshare) |

## Фичи

- Makapix Club (соцсеть пиксель-арта)
- Giphy (трендовые гифки)
- Музеи (IIIF): Чикаго, Rijksmuseum, V&A
- Свои файлы через USB/Wi-Fi
- PICO-8 игры через WebSocket
- Web UI + REST API
- OTA обновления

## Связь с Bobik

p3a показывает что можно сделать на более мощном ESP32-P4.
У Bobik ESP32 слабее (обычный WROOM), но концепция похожая:
дисплей + WiFi + управление.

Можно подсмотреть:
- Как организован Play Scheduler
- Triple-buffered rendering
- Web UI для управления
</peer>
