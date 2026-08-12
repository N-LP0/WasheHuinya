# Прошивка готовыми `.bin` файлами

Этот документ описывает, как прошить устройство сторонним программным обеспечением, если уже собраны готовые `.bin` файлы.

Все команды и относительные пути ниже предполагают запуск из корня репозитория.

## Файлы

После сборки PlatformIO файлы лежат в:

```text
.pio/build/esp32s3zero/
```

Основные файлы:

- `bootloader.bin` - загрузчик ESP32-S3;
- `partitions.bin` - таблица разделов flash;
- `firmware.bin` - основная прошивка устройства;
- `littlefs.bin` - образ файловой системы web UI и демо-профилей.

Для полностью чистого устройства обычно нужны все четыре файла.

Для обновления устройства, где уже записаны корректные `bootloader.bin` и `partitions.bin`, обычно достаточно записать:

- `firmware.bin`;
- `littlefs.bin`.

## Адреса записи

Текущая разметка [`partitions.csv`](../../partitions.csv) использует такие адреса:

```text
bootloader.bin  -> 0x0000
partitions.bin  -> 0x8000
firmware.bin    -> 0x10000
littlefs.bin    -> 0x310000
```

Важно: `firmware.bin` записывается в первый OTA-раздел `ota_0`.
После этого web update сможет обновлять прошивку через второй OTA-раздел `ota_1`.

## Текущая таблица разделов

```text
nvs      0x009000..0x00e000  20 KB
otadata  0x00e000..0x010000   8 KB
ota_0    0x010000..0x190000   1.50 MB
ota_1    0x190000..0x310000   1.50 MB
littlefs 0x310000..0x400000   0.94 MB
```

## Пример для esptool

Пример команды для записи всех файлов:

```powershell
esptool.py --chip esp32s3 --baud 921600 write_flash `
  0x0000 .pio/build/esp32s3zero/bootloader.bin `
  0x8000 .pio/build/esp32s3zero/partitions.bin `
  0x10000 .pio/build/esp32s3zero/firmware.bin `
  0x310000 .pio/build/esp32s3zero/littlefs.bin
```

Если сторонняя программа просит указать файлы и адреса вручную, используй те же адреса из раздела выше.

## Espressif Flash Download Tool

Для прошивки через Espressif Flash Download Tool используй режим для ESP32-S3.

0. Скачай [flash_download_tool](https://dl.espressif.com/public/flash_download_tool.zip)
1. Открой `flash_download_tool`.
2. Выбери chip type `ESP32-S3`.
3. Выбери режим `Develop` или обычный режим загрузки прошивки, если инструмент показывает такой выбор.
4. В таблице файлов добавь строки:

    ```text
    0x0000   .pio/build/esp32s3zero/bootloader.bin
    0x8000   .pio/build/esp32s3zero/partitions.bin
    0x10000  .pio/build/esp32s3zero/firmware.bin
    0x310000 .pio/build/esp32s3zero/littlefs.bin
    ```

    ![Espressif Flash Download Tool](<Flash Download Tool.png>)

5. Отметь галочками все четыре строки.
6. Укажи COM-порт устройства.
7. Рекомендуемые параметры:

    ```text
    SPI SPEED: 80MHz или 40MHz
    SPI MODE:  QIO
    FLASH SIZE: 4MB
    BAUD: 921600, если стабильно; иначе 460800 или 115200
    ```

8. Нажми `START` и дождись завершения записи.
9. После успешной прошивки перезагрузи устройство.

## Рабочий режим и режим прошивки

ESP32-S3 имеет два практически важных режима загрузки:

- рабочий режим - запускается прошивка из flash;
- ROM download mode - устройство ожидает загрузку через USB/UART и не запускает Web UI, Wi-Fi, BLE или LED-задачи прошивки.

На типичной плате ESP32-S3 для входа в ROM download mode удерживай `BOOT`, кратко нажми `RESET`, затем отпусти `BOOT`. После завершения загрузки нажми `RESET` без удержания `BOOT`, чтобы вернуться в рабочий режим. Названия кнопок могут отличаться на конкретной плате.

Не переключай DTR/RTS вручную через serial monitor, если плата не поддерживает надёжный автоматический reset: она может остаться в ROM download mode. В таком случае верни рабочий режим физическими кнопками.

Если прошивается уже размеченное устройство и partition table не менялась, можно записывать только:

```text
0x10000  .pio/build/esp32s3zero/firmware.bin
0x310000 .pio/build/esp32s3zero/littlefs.bin
```

Но после изменения `partitions.csv` обязательно прошивай полный набор, включая `bootloader.bin` и `partitions.bin`.

## Важные замечания

- При смене `partitions.csv` нужно обязательно прошить `partitions.bin` обычным способом.
- Web update не может безопасно заменить partition table работающего устройства.
- Если записать только `firmware.bin`, web UI может не совпасть с API прошивки.
- Если записать только `littlefs.bin`, новая web UI может ожидать API, которого нет в старой прошивке.
- Для согласованного обновления лучше записывать `firmware.bin` и `littlefs.bin` вместе.
