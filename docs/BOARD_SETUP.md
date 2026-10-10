# Reusing ESP32-S3 N16R8 board settings

The IDE forgetting a selected board/port is a host configuration or USB-discovery problem, not a firmware setting. The exact cause on your PC is unconfirmed. A different USB connector, bootloader/runtime USB identity or COM-port association can make it look like a new board. Use the same connector/cable and explicitly select ESP32S3 Dev Module for the currently connected port.

For your N16R8, the supplied configuration is:

| Option | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| Flash size | 16 MB |
| PSRAM | OPI PSRAM (8 MB module) |
| Flash mode | QIO 80 MHz |
| Partition scheme | 16M Flash (3MB APP/9.9MB FATFS) |
| Upload speed | 921600 |
| USB mode | Hardware CDC and JTAG |
| USB CDC on boot, native USB connector | Enabled |
| USB CDC on boot, USB-to-UART connector | Disabled |
| Monitor baud | 115200 |

Menu keys were checked against Espressif's Arduino-ESP32 3.3.12 boards.txt. Keep your existing working 3.x core and libraries; this package does not automatically install or upgrade them. Native USB and a USB-to-UART bridge can be different physical sockets and different COM ports. Choose the connector you actually use for uploads and logs. If a menu choice differs in your installed core, the script stops at board validation instead of uploading.

## Keep using Arduino IDE

Open the same saved sketch folder each time. In the board selector or Tools → Board, choose ESP32S3 Dev Module. In Tools → Port, select the current COM port, including when it is shown as Unknown. Reapply the table if the IDE has lost its board association. An Unknown label does not by itself mean the installed core has been removed. Check Boards Manager only if the ESP32 board entry itself is missing.

The included sketch.yaml does not promise that the IDE Tools menu will load or retain these settings; it is an Arduino CLI defaults file. If you want settings reapplied reliably for this project without clicking every menu, use the scripts below.

## Saved Windows build/upload commands

Install Arduino CLI from https://docs.arduino.cc/arduino-cli/installation/ and make arduino-cli available on PATH. Use the same Arduino sketchbook/library directory as your IDE; if yours is custom, configure the CLI user directory to match. Your existing AudioTools/Foxen/U8g2/Adafruit dependencies must be installed there, along with the ESP32 core.

Open PowerShell in the extracted package folder:

```powershell
# Check the current port; this does not guess a target.
.uild_upload.ps1 -Action list

# Compile with all saved board options.
.uild_upload.ps1 -Action build

# Compile and upload through the native ESP32 USB connector.
# Replace COM7 with the port from list.
.uild_upload.ps1 -Action upload -Port COM7

# If using the separate USB-to-UART bridge connector instead:
.uild_upload.ps1 -Action upload -Port COM7 -Uart

# Monitor logs; reset after opening to capture startup output.
.uild_upload.ps1 -Action monitor -Port COM7
```

The script checks the saved FQBN against your installed core, compiles, and only uploads if compilation succeeds. Port is mandatory for upload/monitor and never automatically picked. No core/libraries are installed, no device is erased as a separate operation, and no system execution-policy change is made. If PowerShell blocks scripts, you can run the equivalent arduino-cli commands manually using the FQBN text files.

The firmware folder also contains sketch.yaml with default_fqbn for direct CLI builds. The saved defaults cannot force Windows to keep the same COM-port number or fix USB drivers. A changed port still needs selection. The scripts were prepared and reviewed, but could not be executed here because PowerShell, Arduino CLI, ESP32 toolchains and the board were unavailable.

Primary documentation:
- https://docs.arduino.cc/arduino-cli/sketch-project-file
- https://support.arduino.cc/hc/en-us/articles/4406856349970-Select-board-and-port-in-Arduino-IDE
- https://docs.espressif.com/projects/arduino-esp32/en/latest/guides/tools_menu.html
- https://github.com/espressif/arduino-esp32/blob/3.3.12/boards.txt
