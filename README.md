# ersatz-jjy

Simulate JJY radio time signal. The real JJY radio time signal is broadcast
from two transmission sites in Japan, one at 40kHz and the other at 60kHz.
ersatz-jjy fakes the 60kHz JJY signal using a 20kHz audio sine wave; when played
at high volume through a wired speaker or headphones placed close to the radio
antenna of a JJY-compatible watch or clock, the analog audio signal leaks enough
electromagnetic radiation at the third harmonic for the antenna to receive it as
a 60kHz longwave radio signal.

As currently implemented, ersatz-jjy is written in C using C11 standard
libraries and PortAudio for audio output.

## Usage notes
* This program outputs an audio signal. For best results, place a wired speaker
  or wired headphones playing this signal at high volume close to the antenna
  of the device that you want to synchronize.
* Once invoked from the command line, the program keeps playing its audio signal
  indefinitely until interrupted, for example using a keyboard interrupt.
* While the real JJY time signal always encodes Japan Standard Time (JST), this
  program encodes the time in the system timezone. In a POSIX environment, you
  can change the effective system timezone for just this program using the `TZ`
  environment variable, for example with `TZ="JST-9" ersatz-jjy` to encode JST.
* On some systems, depending on the version of PortAudio used, the initial probe
  to find the default audio output device may cause a lot of ALSA errors to be
  printed to the terminal although they have been effectively handled by
  PortAudio. In this case, the program will continue as normal after the errors
  are printed.

## Planned improvements

* Implement leap seconds
* Implement --help and --version CLI flags
* Implement CLI flag to force a JST time signal regardless of system timezone
