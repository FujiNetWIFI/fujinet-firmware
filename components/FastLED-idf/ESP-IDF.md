# Port to ESP-IDF

THis is based off the 3.3 version of FastLED. The 3.3 version is where a lot of development paused, although there are also
a lot of good small fixes. This is actually a port of Sam Guyer's awesome ESP32 focused fork, https://github.com/samguyer/FastLED .

If one is reporting, there's a few bits here and there to be done, but most of the work is in the platform directory.

I have not tested the "4 wire" I2S based LEDs or code.

# see section below about glitches!

# Environment

This port is to be used with ESP-IDF version 4.x, which went GA on about Feb, 2020.

I have tested it to the greatest extent, at this point, with the 4.2 release, as its more stable than master
but far closer to master than 4.0, which has some breaking changes.

In a number of cases, the code can't easily be made to support all versions of ESP-IDF.

In more recent versions of ESP-IDF, there is a new `_ll_` interface for the RMT system. Other posters
have re-tooled to that interface instead of writing directly to hardware addresses.

# menuconfig

I prefer running my code -O3 optimized. I haven't changed any of the stack depths.

# Differences

This code defaults to using two memory buffers instead of 1. There are tradeoffs, and you can change the values. See below.

It is not clear that using the ESP-IDF interrupt handler works anymore, although it should be tried. With larger
memory buffers, and using the translate function, it should work no better or worse than any other LEVEL3 interrupt.

Recent RMT driver code also includes setting the "ownership" of the shared DRAM. THis was overlooked in the FastLED
driver code, but has been implemented. It seemed to make no difference to the stability of the system.

# Difficulties

## Timing and glitches

The greatest difficulty with controlling any WS8211 system with the ESP32 / ESP8266 RMT system is timing.

The WS8211 single wire protocols require two transitions to make a bit.

The definition of the RMT interface means you put a time between the transition ( in divided 80mhz intervals, fit into a 15 bit 
field with the high bit being the value to emit ). A single RMT buffer has 64 values, but we use a "double buffer" strategy
(recommended by documentation). This means that 32 values, with 32 bits each, requires re-feeding the buffer about every 35 us.
The buffer won't fully run dry until 70us, but at 35us.

With a 400Khz LED, the RMT buffer is 2x longer in time.

Interupts can be run in C only at "medium priority", which means that there are a class of activities - such as Wifi - which can 
create enough jitter to disturb 35us timing requirement. I have observed this with a very simple REST web service using
the ESP-IDF supplied web server, which doesn't use Flash in any noticable way, other than executing from it - and still, 50us
interrupt jitter was observed.

The RMT interface allows using more buffering, which will overcome this latency. THis is controlled by `MEMORY_BUFFERS` parameter,
which is now configurable at the beginning of `clockless_rmt_esp32.h`. To absorb the latencies that I've seen, I need
two memory buffers instead of 1. If you're not using wifi, you can perhaps get away with 1. Maybe if you're doing other things
on the CPUs, 2 isn't enough and you're going to have to use 4.

Increasing this value means you can't use as many RMT hardware channels at the same time. IF you use a value of 2, which
works in my environment, the code will only use 4 hardware channels in parallel. If you create 8 LED strings, what should
happen is 4 run in parallel, and the other 4 get picked up as those finish, so you'll end up using as much parallelism
as you have available.

In order to tune this variable, you'll find another configuration, which is `FASTLED_ESP32_SHOWTIMING`, also in that clockless H file.
If you enable this, for each show call, the number of microseconds between calls will be emitted, and a prominent message
when a potential underflow is detected. This will allow you to stress the system, look at the interupt jitter, and decide
what setting you'd like for the `MEMORY_BUFFER`s.

Please note also that I've been testing with the fairly common 800Khz WS8211's. If you're using 400Khz, you can almost certainly
go back to 1 `MEMORY_BUFFER`. Likewise, if you've got faster LEDs, you might have to go even higher. The choice is yours.

## Reproducing the issue on other systems

I consider that the RTOS, at the highest level of interrupt available to the common user, even with 2 CPUs, in
an idle system, 10 uS jitter to be something of a bug. At first, I blamed the FastLED code, but have spent quite a bit
of energy exhonerating FastLED and placing the blame on ESP-IDF.

In order to determine the problem, I also did a quick port of the NeoPixelBus interface, and I also used the sample
code for WS8211 LEDs which I found in the ESP-IDF examples directory. All exhibited the same behavior.

Thus the issue is not in the FastLED library, but simply a jitter issue in the RTOS. It seems that people
using Arduino do not use the same TCP/IP stack, and do not suffer from these issues. Almost certainly, they are running at
lower priorities or with a different interrupt structure.

A simple test, using 99% espressif code, would open a simple HTTP endpoint and rest service, connect to WIFI and maintain an IP address,
then use the existing WS8211 sample code provided by Espressif. I contend that a web server which is simply returning "404", and
is being hit by mutiple requests per second ( I used 4 windows with a refresh interval of 2 seconds, but with cached content ) will
exhibit this latency.

# Todo: Running at a higher priority

ESP-IDF has a page on running at higher prioity: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/hlinterrupts.html

This page says (somewhat incorrectly) that C code may not be run from high priority interrupts. The document then 
disagrees with itself, and the example presented calls C saying some C code is safe, but being a bit cagey about 
why. 

Given this problem happens with all drivers ( custom and ESP-IDF provided ), writing a high-priority driver in assembly
which packes the RMT buffer from a "pixel" format buffer seems a very reasonable optimization. Or, using the current
interrupt driver, and simply throwing the disasm into a .S file.

I would greatly hope that ESP-IDF improves the documentation around higher priority interrupts, and about manipulating the interrupts
in the system. I'd like to be able to profile and find what's causing these long delays, and drop their priority a little.
The FreeRTOS documentation ( on which ESP-IDF is built ) clearly says that high priority interrupts are required for motor control,
and LED control falls into that category. Placing the unnecessary barrier of assembly language isn't a terrible thing,
but it's not the right way - show how to write safe C code and allow raising the priority, even if some people will
abuse the privledge.


# Todo - why the jitter?

The large glitches ( 50us and up ) at the highest possible prority level, even with 2 cores of 240Mhz, is almost implausible.

Possible causes might be linked to the Flash subsystem. However, disabling all flash reads and writes ( the SPI calls ) doesn't change the glitching behavior.

Increasing network traffic does. Thus, the Wifi system is implicated. Work to do would be to decrease, somehow, the priority of 
those interrupts. One forum poster said they found a way. As TCP and Wifi are meant to be unreliable, this would cause 
performance issues, and that would have to be weighed against the nature of the poor LED output.

# Todo - why visual artifacts?

I haven't put a scope on, but I'm a little surprised that if you only throw an "R" and not a "G and B" on the wire, a pixel changes.
This appears to be the reason you get a single pixel flash, and there's nothing one can do about that other than these deeper buffers.
I would hope that other WS8211 type LEDs are a bit more robust and would only change color when they get a full set of R,G, and B.


