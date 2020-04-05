audio
=====

A simple tool to trigger command A or B on the 0x70 #FujiNet device, which will emit an audio test signal out of the 8-bit DAC connected to AUDIOIN. The MOTOR control line on the PIA is also either asserted or desserted depending on the passed in argument.

Usage

```
AUDIO <0|1>
```

* 0 = turn off
* 1 = turn on

