# ascope
An Arduino oscilloscope.

## Features
* up to 16 MSps equivalent-time sampling rate,
* 8-bit resolution,
* multiple channels.

## Hardware setup
The oscilloscope takes its input from the analog pins A0, A1, etc.
Aquisition is triggered when voltage on the AIN0 pin first exceeds that
on the AIN1 pin.

## Controls
key                   | action
----------------------|-------
`↓` `↑`               | Decrease or increase the time step by 1/16 μs.
`Ctrl`+`↓` `Ctrl`+`↑` | Decrease or increase the time step by 1 μs.
`<space>`             | Freeze or thaw
`d`                   | Dump the raw data to `stderr`
`q`                   | Quit

## Examples
A __ kHz sinewave.
![](docs/sin.png)

A __ kHz multivibrator output along with the corresponding base voltage.
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
