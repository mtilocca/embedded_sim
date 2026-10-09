# STM32 Wi-Fi (X-NUCLEO-67W61M1 on NUCLEO-U575ZI-Q)

- Shield: ST67W611M1 coprocessor (NCP), SPI via Arduino header. Zephyr shield `x_nucleo_67w61m1`
  (driver `drivers/wifi/st67w611m1`), selected in `scripts/build.sh`.
- Only the **T02** firmware profile (TCP/IP on host) works with Zephyr, NCP fw >= v2.0.106.
- NCP firmware comes from https://github.com/STMicroelectronics/x-cube-st67w61
  (`Projects/ST67W6X_Scripts/Binaries/NCP_update_mission_profile_t02.sh`).
  It needs STM32CubeProgrammer (`STM32_Programmer_CLI`) in
  `/usr/local/STMicroelectronics/STM32Cube/STM32CubeProgrammer/bin`.
  WARNING: the script locks the NCP (eFuse) if not already locked. Irreversible.
- After NCP flashing, build/flash our app: `scripts/build.sh stm32 -p always && scripts/flash.sh stm32`.
- Test: `curl http://192.168.1.45/` (ESP32: 192.168.1.44).
