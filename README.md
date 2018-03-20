# ascope
An Arduino oscilloscope.

## Features
* up to 300 kSps sampling rate,
* 8-bit resolution,
* linear and sinc interpolation up to 256x.

## Hardware setup
The oscilloscope takes its input from the A0 pin. Aquisition is
triggered when voltage on the AIN0 pin first exceeds that on the AIN1
pin.

## Controls
key       | action
----------|-------
`↓` `↑`   | Divide or multiply the sampling rate by 2
`←` `→`   | Divide or multiply the zoom factor by 2
`i`       | Toggle interpolation mode (linear or sinc)
`<space>` | Freeze or thaw
`d`       | Dump the raw data to `stderr`
`q`       | Quit

## Examples
A __ kHz sinewave.
![](docs/sin.png)

A __ kHz square waveform.
![](docs/sq.png)

## Customization
Since the Arduino ADC accepts input signal in the range 0-5V only,
one would probably use an external conversion circuit to fit the signal
being studied to the range suitable for Arduino. The original voltage
range can be set with the `V_MIN` and `V_MAX` macros.

Other adjustable settings are:
* Window width and height,
* Grid steps (Volts per division and Samples per division),
* Device file name,
* Number of samples in buffer (must be set to the same value as in the sketch).

## Compliance
The GUI source complies with the C99 standard and should compile on any
POSIX system with X11 support.

## License
The programs are in the public domain.
