cmake_minimum_required(VERSION 3.5)

set(srcs
 		"FastLED.cpp"
		"bitswap.cpp"
		"colorpalettes.cpp"
		"colorutils.cpp"
		"hsv2rgb.cpp"
		"lib8tion.cpp"
		"noise.cpp"
		"platforms.cpp"
		"power_mgt.cpp"
		"wiring.cpp"
		"hal/esp32-hal-misc.c"
		"hal/esp32-hal-gpio.c"
# remove the following if you want I2S instead of RMT hardware, just put a pound in front
#		"platforms/esp/32/clockless_rmt_esp32.cpp"
		)

# everything needs the ESP32 flag, not sure why this won't work
# going to hack by adding the ESP32 define in the h file
	#`target_compile_options(${COMPONENT_LIB} PRIVATE "-DESP32")

idf_component_register(SRCS "${srcs}"
			INCLUDE_DIRS "." "./hal" )
