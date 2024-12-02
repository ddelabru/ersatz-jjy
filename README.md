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

## Compiling from source code

The build dependencies are: a C compiler, C11 standard libraries, PortAudio with
development headers, pkg-config, Make, and CMake. A typical build looks like: 

```sh
cmake .
make
```

I've had success compiling with both gcc and clang on NixOS. In theory, the
program should run on any platform that supports PortAudio.

## Usage notes and limitations
* This program outputs an audio signal. For best results, place a wired speaker
  or wired headphones playing this signal at high volume close to the antenna
  of the device that you want to synchronize.
* Once invoked from the command line, the program keeps playing its audio signal
  indefinitely until interrupted, for example using a keyboard interrupt.
* While the real JJY time signal always encodes Japan Standard Time (JST), this
  program encodes the time in the system timezone. In a POSIX environment, you
  can change the effective system timezone for just this program using the `TZ`
  environment variable, for example with `TZ="JST-9" ersatz-jjy` to encode JST.
  You can also use the `-j` or `--jst` command line flag to force the program to
  encode JST regardless of the system timezone.
* On some systems, depending on the version of PortAudio used, the initial probe
  to find the default audio output device may cause a lot of ALSA errors to be
  printed to the terminal although they have been effectively handled by
  PortAudio. In this case, the program will continue as normal after the errors
  are printed.
* Leap seconds are not implemented. The primary obstacle to implementing leap
  seconds is that the basic C representation of system time is not aware of leap
  seconds on many systems; on Unix-like systems in particular the time is stored
  as a number of seconds since a pre-defined point in time (the "epoch"), _not_
  counting leap seconds. C++ standard libraries have other formats for storing
  time that are leap second-aware, but breaking out calendar information for a
  stored time still relies on the C representation of system time. This may be
  surmountable, but it may be not worth the effort because as of 2024 it appears
  that there may never be another leap second. Since 2016 the earth's rotation
  has been trending faster than atomic time and so _negative_ leap seconds would
  be called for, but in practice negative leap seconds have never yet been
  implemented and international timekeeping bodies have committed to phase out
  leap seconds altogether by 2035 in favor of some other mechanism to manage the
  drift between UTC and earth rotation.
