# Building the NB4 firmware

The build requires Python 3.11 or newer, an ARM GNU toolchain, CMake, and SDL2
for native tests. Create the Python environment once:

```sh
python3 -m venv .venv
./.venv/bin/python -m pip install -r tools/requirements-nb4.txt
```

Configure and build the device firmware:

```sh
cmake -S . -B build/nb4-device \
  -DPCB=PL18 -DPCBREV=NB4 -DCMAKE_BUILD_TYPE=Release \
  -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
  -DNB4_RF_PROFILE=RECOVERED_USART6 \
  -DTRANSLATIONS=EN -DDISABLE_COMPANION=ON
cmake --build build/nb4-device --target firmware-size -j8
```

Configure and build the native tests and simulator from the same build tree:

```sh
cmake --build build/nb4-device --target native-configure -j8
cmake --build build/nb4-device/native --target tests-radio simu -j8
```

The complete firmware image is
`build/nb4-device/arm-none-eabi/firmware.bin` when using the commands above.
It contains the EdgeTX bootloader and application and is the file accepted by
`tools/nb4-flash.sh`.
