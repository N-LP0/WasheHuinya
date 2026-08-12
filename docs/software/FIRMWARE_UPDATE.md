# Firmware update

Устройство поддерживает обновление через web UI на вкладке `Update`.
Обновление выполняется одной сессией: сначала загружается прошивка, затем
LittleFS, после чего устройство перезагружается.

## Что можно обновить

- `Firmware image` - файл `.pio/build/esp32s3zero/firmware.bin`.
- `LittleFS image` - файл `.pio/build/esp32s3zero/littlefs.bin`.

После успешной загрузки обоих файлов устройство автоматически перезагружается.
Web UI через несколько секунд перезагрузит страницу, очистит состояние вкладки
`Update` и вернется на главную вкладку `Control`.

## Как собрать файлы

Команды выполняются из корня репозитория.

Сборка прошивки:

```powershell
pio run -e esp32s3zero
```

Сборка LittleFS:

```powershell
pio run -e esp32s3zero -t buildfs
```

## API

- `POST /api/update/firmware` - загрузка `firmware.bin`.
- `POST /api/update/filesystem` - загрузка `littlefs.bin`.
- `GET /api/update/progress` - текущий прогресс обновления.

Файлы отправляются как `multipart/form-data`; web UI добавляет заголовок
`X-Update-Size` с размером файла.

Для первого шага web UI отправляет `X-Restart-After-Update: 0`, чтобы
устройство не перезагрузилось между прошивкой и LittleFS. На втором шаге
отправляется `X-Restart-After-Update: 1`.

## Важные замечания

- Нельзя выключать питание во время обновления.
- LittleFS-образ заменяет файлы web UI и демо-профили из `data/`.
- Если обновление LittleFS прошло успешно, текущий web UI может стать недоступен
  до перезагрузки.
- Web update прошивки требует OTA-разметку flash с двумя app-разделами `ota_0` и `ota_1`.
- Если устройство уже прошито старой разметкой без OTA-разделов, первый переход
  на новую разметку нужно сделать обычной прошивкой через USB/PlatformIO.
  Web update не может безопасно заменить partition table работающего устройства.
- Если устройство находится в ROM download mode, Web UI и OTA недоступны. Верни
  плату в рабочий режим кнопкой `RESET` без удержания `BOOT`.

## Текущая разметка flash

Текущий [`partitions.csv`](../../partitions.csv) рассчитан на 4MB flash:

```text
nvs      0x009000..0x00e000  20 KB
otadata  0x00e000..0x010000   8 KB
ota_0    0x010000..0x190000   1.50 MB
ota_1    0x190000..0x310000   1.50 MB
littlefs 0x310000..0x400000   0.94 MB
```

Текущая версия прошивки задается в `platformio.ini` через
`HIDPAD_FIRMWARE_VERSION`. Проверяй фактический размер `firmware.bin` после
сборки: он должен помещаться в один OTA-раздел `0x180000`.

После перехода со старой таблицы разделов обязательно прошей `partitions.bin`
и `littlefs.bin` через USB: web update не переносит границы разделов.
