# ascope
An Arduino Uno oscilloscope.

## Features
* real-time sampling rates up to 300 kSps,
* equivalent-time sampling rates up to 16 MSps,
* 8-bit resolution,
* multiple channels.

## The principle of operation
The oscilloscope takes its input from the analog pins A0, A1, etc. In
the normal mode, acquisition is triggered when the voltage on the AIN0
pin matches that on the AIN1 pin, either on the rising (default) or the
falling edge. In the auto-trigger mode, acquisition starts immediately.

In the real-time sampling mode, the ADC is free-running. AC ISR polls
the ADC for conversion results and fills the output buffers. Sampling
rate is switched by changing the ADC clock division factor.

In the equivalent-time sampling mode, the time interval between an AC
interrupt and ADC conversion is measured with the 16-bit Timer/Counter1.
The conversion is triggered by the TC1 output compare match. ADC ISR
reads the conversion result and schedules the next one. Sampling rate is
switched by changing the TC1 clock division factor.

## Control and data exchange protocol
Settings from the control program and data from the oscilloscope are
exchanged with a simple protocol described below.

#### Control flow
The oscilloscope takes its settings from a single-byte control word:

![](docs/cw.svg)

#### Data flow
The oscilloscope returns data in the following format:

![](docs/data.svg)

## Limitations
The analog bandwidth of the Arduino ADC input circuits is not much above
100 kHz. Signals of higher frequency are considerably distorted.

## Indication
The onboard LED is turned on while the acquisition is in progress.

## Control software
We provide a simple control program for Unix-like operating systems with
X11 graphics. It requires just plain `Xlib` and `libpng` and is
controlled mostly from keyboard:

key               | action
------------------|-------
`1`, `2`, ...     | Use 1, 2, ... channels
`/`, `\`          | Trigger on rising or falling edge
`a`               | Set auto-trigger mode (RT sampling only)
`+`, `-`          | Increase or decrease sampling rate
`m`               | Toggle sampling mode (RT or ET)
`→`, `←`          | Increase or decrease time scale zoom
`i`               | Toggle interpolation mode (linear or sinc)
`s`               | Turn on single sweep mode
`<space>`         | Freeze or thaw
`d`               | Dump the raw data to `stderr`
`w`               | Write the oscillogram to `./out.png`
`q`               | Quit
any mouse button  | Show the time and voltage values below the pointer

## Examples
A multivibrator running at 75 kHz (collector and base voltages):

![](docs/out.png)

A signal conditioning circuit for -5..+5 V inputs, used to produce
the above oscillogram:

![](docs/cond.svg)

## License
MIT.
