# ascope
An Arduino Uno oscilloscope.

## Features
* equivalent-time sampling rates 16 MSps, 2 MSps, 250 kSps, 62.5 kSps, and 15.625 kSps,
* 8-bit resolution,
* multiple channels.

There's also a [real-time sampling](../../tree/realtime) version.
However, it offers much lower sampling rates.

## Hardware setup
The oscilloscope takes its input from the analog pins A0, A1, etc.
Acquisition is triggered when the voltage on the AIN0 pin rises above (or
falls below) that on the AIN1 pin.

## The principles of operation
Acquisition is started when an AC interrupt occurs. Time interval between
AC interrupt and ADC conversion is measured with the 16-bit
Timer/Counter1. This is facilitated by running the ADC in auto-trigger
mode, when the conversion is triggered by the Timer/Counter1 output
compare match. Sampling rate is switched by changing the TC1 clock
division factor.

## Control and data exchange protocol
Settings from the control program and data from the oscilloscope are
exchanged with a simple protocol described below.

#### Control flow
The oscilloscope takes its settings from a single-byte control word:

![](docs/cw.svg)

#### Data flow
The oscilloscope returns data in the following format:

![](docs/data.svg)

The details of the exchange protocol are documented thoroughly in the
source.

## Limitations
The analog bandwidth of the Arduino ADC input circuits is not much above
100 kHz. Signals of higher frequency are considerably distorted.

## Indication
The onboard LED is turned on while the acquisition is in progress.

## The X11 control and viewer program
We provide a reference implementation of a control program for Unix-like
operating systems.

#### Keyboard controls
key            | action
---------------|-------
`+`, `-`       | Increase or decrease sampling rate
`/`, `\`       | Trigger on rising or falling edge
`1`, `2`, etc. | Use 1, 2, or more (if compiled) channels
`→`, `←`       | Increase or decrease time scale zoom
`↑`, `↓`       | Increase or decrease voltage scale zoom
`i`            | Toggle interpolation mode (linear or sinc)
`<space>`      | Freeze or thaw
`d`            | Dump the raw data to `stderr`
`w`            | Write the oscillogram to `./out.png` (if compiled)
`q`            | Quit

Pressing a mouse button will show the time and voltage values under the
pointer.

#### Customization
Since the Arduino ADC accepts input in the range 0-5 V only, one would
probably use an external conversion circuit to fit the signal being
studied to the range suitable for the ADC. The relation between input
voltage and ADC reading is set with the `ZS` and `VPS` macros.

Other adjustable settings are:
* Number of samples in the buffer (must be the same as in the sketch),
* Maximum number of channels (must be the same as in the sketch),
* Oscillogram width and height,
* Grid steps (Volts per division and Samples per division),
* Display voltage range,
* Device file name,
* Option to save oscillograms to PNG files.

#### Requirements
The GUI uses plain Xlib only. Saving oscillograms to files uses GD
graphics library.

#### Compliance
The GUI source complies with the C99 standard.

## Calibration output
The source code contains provisions for producing square waveforms in
the wide range of frequencies (from 500 Hz to 500 kHz), which might be
useful for testing and calibration.

## Example
A 31.25 kHz calibration output, fed directly to the oscilloscope input:

![](docs/out.png)

## License
MIT.
