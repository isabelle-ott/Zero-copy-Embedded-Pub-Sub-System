# 注意：Makefile 里的缩进必须是 Tab 键，不能是空格！

.PHONY: config build clean flash

# 重新生成配置，并自动建立 compile_commands.json 的软链接
config:
	cmake -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/gcc-arm-none-eabi.cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	ln -sf build/compile_commands.json .

# 编译代码
build:
	cmake --build build

# 彻底清理缓存
clean:
	rm -rf build

# 一键烧录 (假设用 ST-Link 和 STM32F4)
flash:
	openocd -f interface/stlink.cfg -f target/stm32f4x.cfg -c "program build/FreeRTOS_TETS.elf verify reset exit"

serial:
	picocom -b 115200 -c --omap crcrlf /dev/ttyUSB0

rebuild_download:
	make clean
	make config
	make build
	make flash
