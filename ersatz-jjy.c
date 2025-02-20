/*  ersatz-jjy: Simulate JJY radio time signal
    Copyright (C) 2024 Dominic Delabruere
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>. */

#include "ersatz-jjy-config.h"
#include "portaudio.h"
#include <math.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Macro constants */
#define MAX_NANOSEC (1000000000L)
#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (64)
#define WT_CAP (18)
#define NINE_HOURS (32400) /* JST offset from UTC in seconds */

/* Calculated constants */
const unsigned long JJY_B0_HIGH_SAMPLES = SAMPLE_RATE * 4 / 5;
const unsigned long JJY_B1_HIGH_SAMPLES = SAMPLE_RATE / 2;
const unsigned long JJY_M_HIGH_SAMPLES = SAMPLE_RATE / 5;

/* Global variables determined from CLI flags */
double JJY_FREQ; /* One-third the actual JJY longwave frequency */
int WT_SIZE;

/* Global PulseAudio stream reference */
PaStream *STREAM = NULL;

/*  Wavetables holding sequential audio samples for high (full amplitude) and
    low (10% amplitude) signal states. These are populated by
    populate_jjy_wavetables() at startup, then samples are repeatedly copied
    from them directly into the audio buffer. This eliminates the need for
    performing computationally expensive sine calculations while writing to the
    buffer, allowing for smooth sine-wave playback. The size of the wavetables
    is chosen so that it contains a whole number of sine-wave cycles for the
    given sample rate; for example, 12 samples at a 48kHz sample rate contain
    exactly 5 cycles of a 20kHz sine-wave; this ensures that consecutive
    repetitions of the wavetable encode a continuous sine-wave at a constant
    frequency.
*/
float WT_HIGH[WT_CAP];
float WT_LOW[WT_CAP];

typedef struct
{
  bool fukushima;
  bool help;
  bool jst;
  bool version;
} jjy_args;

typedef struct
{
  char short_form;
  char *long_form;
  char *help_text;
  void (*setter) (jjy_args *);
} jjy_cli_flag;

typedef struct
{
  time_t seconds;
  struct tm *local;
  unsigned long sample_index;
  unsigned long wt_index;
  unsigned long high_samples;
  bool jst;
} jjy_data;

/* Functions that calculate individual bits of the JJY time code */

bool
jjy_b01 (const struct tm *t)
{
  return (t->tm_min >= 40);
}

bool
jjy_b02 (const struct tm *t)
{
  return ((t->tm_min % 40) >= 20);
}

bool
jjy_b03 (const struct tm *t)
{
  return ((t->tm_min % 20) >= 10);
}

bool
jjy_b05 (const struct tm *t)
{
  return ((t->tm_min % 10) >= 8);
}

bool
jjy_b06 (const struct tm *t)
{
  return (((t->tm_min % 10) % 8) >= 4);
}

bool
jjy_b07 (const struct tm *t)
{
  return (((t->tm_min % 10) % 4) >= 2);
}

bool
jjy_b08 (const struct tm *t)
{
  return ((t->tm_min % 2) > 0);
}

bool
jjy_b12 (const struct tm *t)
{
  return (t->tm_hour >= 20);
}

bool
jjy_b13 (const struct tm *t)
{
  return ((t->tm_hour % 20) >= 10);
}

bool
jjy_b15 (const struct tm *t)
{
  return ((t->tm_hour % 10) >= 8);
}

bool
jjy_b16 (const struct tm *t)
{
  return (((t->tm_hour % 10) % 8) >= 4);
}

bool
jjy_b17 (const struct tm *t)
{
  return (((t->tm_hour % 10) % 4) >= 2);
}

bool
jjy_b18 (const struct tm *t)
{
  return ((t->tm_hour % 2) > 0);
}

bool
jjy_b22 (const struct tm *t)
{
  return ((t->tm_yday + 1) >= 200);
}

bool
jjy_b23 (const struct tm *t)
{
  return (((t->tm_yday + 1) % 200) >= 100);
}

bool
jjy_b25 (const struct tm *t)
{
  return (((t->tm_yday + 1) % 100) >= 80);
}

bool
jjy_b26 (const struct tm *t)
{
  return ((((t->tm_yday + 1) % 100) % 80) >= 40);
}

bool
jjy_b27 (const struct tm *t)
{
  return ((((t->tm_yday + 1) % 100) % 40) >= 20);
}

bool
jjy_b28 (const struct tm *t)
{
  return (((t->tm_yday + 1) % 20) >= 10);
}

bool
jjy_b30 (const struct tm *t)
{
  return (((t->tm_yday + 1) % 10) >= 8);
}

bool
jjy_b31 (const struct tm *t)
{
  return ((((t->tm_yday + 1) % 10) % 8) >= 4);
}

bool
jjy_b32 (const struct tm *t)
{
  return ((((t->tm_yday + 1) % 10) % 4) >= 2);
}

bool
jjy_b33 (const struct tm *t)
{
  return (((t->tm_yday + 1) % 2) > 0);
}

bool
jjy_b36 (const struct tm *t)
{
  /*  Even parity over time code bits 12-18. Bit 14 has a constant value of 0
      and therefore does not affect the calculation. The result is effectively
      an XOR of all bits in the range.
  */
  bool even_parity = false;
  even_parity = (even_parity != jjy_b12 (t));
  even_parity = (even_parity != jjy_b13 (t));
  even_parity = (even_parity != jjy_b15 (t));
  even_parity = (even_parity != jjy_b16 (t));
  even_parity = (even_parity != jjy_b17 (t));
  even_parity = (even_parity != jjy_b18 (t));
  return even_parity;
}

bool
jjy_b37 (const struct tm *t)
{
  /*  Even parity over time code bits 1-8. Bit 4 has a constant value of 0 and
      therefore does not affect the calculation.
  */
  bool even_parity = false;
  even_parity = (even_parity != jjy_b01 (t));
  even_parity = (even_parity != jjy_b02 (t));
  even_parity = (even_parity != jjy_b03 (t));
  even_parity = (even_parity != jjy_b05 (t));
  even_parity = (even_parity != jjy_b06 (t));
  even_parity = (even_parity != jjy_b07 (t));
  even_parity = (even_parity != jjy_b08 (t));
  return even_parity;
}

bool
jjy_b41 (const struct tm *t)
{
  return ((t->tm_year % 100) >= 80);
}

bool
jjy_b42 (const struct tm *t)
{
  return (((t->tm_year % 100) % 80) >= 40);
}

bool
jjy_b43 (const struct tm *t)
{
  return (((t->tm_year % 100) % 40) >= 20);
}

bool
jjy_b44 (const struct tm *t)
{
  return ((t->tm_year % 20) >= 10);
}

bool
jjy_b45 (const struct tm *t)
{
  return ((t->tm_year % 10) >= 8);
}

bool
jjy_b46 (const struct tm *t)
{
  return (((t->tm_year % 10) % 8) >= 4);
}

bool
jjy_b47 (const struct tm *t)
{
  return (((t->tm_year % 10) % 4) >= 2);
}

bool
jjy_b48 (const struct tm *t)
{
  return ((t->tm_year % 2) > 0);
}

bool
jjy_b50 (const struct tm *t)
{
  return (t->tm_wday >= 4);
}

bool
jjy_b51 (const struct tm *t)
{
  return ((t->tm_wday % 4) >= 2);
}

bool
jjy_b52 (const struct tm *t)
{
  return ((t->tm_wday % 2) > 0);
}

/*  Bits 53 and 54 have function stubs here because they should warn about
    upcoming leap seconds. A bit 53 value of 1 (true) indicates that the
    current UTC month ends with a leap second; if a leap second is upcoming
    then bit 54 indicates whether it will be a positive leap second (1) or a
    negative leap second (0). In practice, negative leap seconds have never
    been implemented by international timekeeping bodies, and as of 2024 it
    appears likely that no more leap seconds of either kind will occur before
    they are scheduled to be phased out in 2035. Furthermore, many
    implementations of the time_t type that stores datetimes in C (especially
    on POSIX systems) are not leap second-aware and therefore do not allow C
    code to discover upcoming leap econds, so implementing these would require
    code from outside the C standard libraries, for example by incorporating
    C++20 standard libraries.
*/

bool
jjy_b53 (const struct tm *t)
{
  return false;
}

bool
jjy_b54 (const struct tm *t)
{
  return false;
}

unsigned long
sec_high_samples (const struct tm *t)
{
  /*  Return the number of high (full amplitude) samples that should be played
      at the start of the second represented by t. The length of the high
      signal at the start of each second represents either a 0 bit, a 1 bit,
      or a marker that allows the receiver to recognize the structure of the
      time code and where the encoded minute begins and ends.

      In the real JJY time code, minutes 15 and 45 of every hour follow an
      altered format where bits 41-48 are replaced by a Morse code station
      identifier and bits 50 through 55 are replaced by bits providing
      information about upcoming planned service interruptions. This program
      does not replicate this behavior and instead follows the same format
      for all other minutes of the hour during minutes 15 and 45, expecting
      the receiver to ignore information in the affected time-frames.
  */

  /*  Lookup table for functions that determine bit value for each second;
      a null pointer is provided for seconds that encode markers or a constant
      value of zero.
  */
  bool (*jjy_bit_func[]) (const struct tm *) = {
    NULL,    jjy_b01, jjy_b02, jjy_b03, NULL,    jjy_b05, jjy_b06, jjy_b07,
    jjy_b08, NULL,    NULL,    NULL,    jjy_b12, jjy_b13, NULL,    jjy_b15,
    jjy_b16, jjy_b17, jjy_b18, NULL,    NULL,    NULL,    jjy_b22, jjy_b23,
    NULL,    jjy_b25, jjy_b26, jjy_b27, jjy_b28, NULL,    jjy_b30, jjy_b31,
    jjy_b32, jjy_b33, NULL,    NULL,    jjy_b36, jjy_b37, NULL,    NULL,
    NULL,    jjy_b41, jjy_b42, jjy_b43, jjy_b44, jjy_b45, jjy_b46, jjy_b47,
    jjy_b48, NULL,    jjy_b50, jjy_b51, jjy_b52, jjy_b53, jjy_b54, NULL,
    NULL,    NULL,    NULL,    NULL,    NULL /* Second 60, a leap second */
  };

  switch (t->tm_sec)
    {
    /*  This code does not correctly implement leap seconds; if a minute
        ends in a positive leap second, then second 59 should encode a value
        of 0, instead of a marker as it does during any other minute.
        Conversely, if a minute ends with a negative leap second, then
        second 58 should encode a marker instead of its usual value of 0.
        Although the C11 standard allows a minute with 61 seconds according
        to the struct tm type, the underlying implementation of the time_t
        type that canonically represents a datetime is often incapable of
        representing leap seconds.
    */
    case 0:
    case 9:
    case 19:
    case 29:
    case 39:
    case 49:
    case 59:
    case 60: /* Leap second */
      /* These seconds of the 60-second time code encode markers */
      return JJY_M_HIGH_SAMPLES;
    case 4:
    case 10:
    case 11:
    case 14:
    case 20:
    case 21:
    case 24:
    case 34:
    case 35:
    case 38:
    case 40:
    case 55:
    case 56:
    case 57:
    case 58:
      /* These seconds of the 60-second time code always encode 0 */
      return JJY_B0_HIGH_SAMPLES;
    case 1:
    case 2:
    case 3:
    case 5:
    case 6:
    case 7:
    case 8:
    case 12:
    case 13:
    case 15:
    case 16:
    case 17:
    case 18:
    case 22:
    case 23:
    case 25:
    case 26:
    case 27:
    case 28:
    case 30:
    case 31:
    case 32:
    case 33:
    case 36:
    case 37:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
    case 47:
    case 48:
    case 50:
    case 51:
    case 52:
    case 53:
    case 54:
      /* These seconds encode variable bits with time information */
      return (jjy_bit_func[t->tm_sec](t) ? JJY_B1_HIGH_SAMPLES
                                         : JJY_B0_HIGH_SAMPLES);
    default:
      /* In practice, this block should be unreachable */
      return JJY_B0_HIGH_SAMPLES;
    }
}

int
handle_pa_err (PaError err)
{
  Pa_Terminate ();
  fprintf (stderr, "PortAudio error %d\n", err);
  fprintf (stderr, "%s\n", Pa_GetErrorText (err));
  return err;
}

struct tm *
get_tm (time_t *t, bool jst)
{
  time_t t_with_offset = *t;

  if (jst)
    {
      t_with_offset += NINE_HOURS;
      return gmtime (&t_with_offset);
    }
  return localtime (&t_with_offset);
}

static int
jjy_stream_callback (const void *inputBuffer, void *outputBuffer,
                     unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo *timeInfo,
                     PaStreamCallbackFlags statusFlags, void *userData)
{
  float *out = (float *)outputBuffer;
  unsigned long i;
  jjy_data *d = (jjy_data *)userData;

  for (i = 0; i < framesPerBuffer; i++)
    {
      if (d->sample_index < d->high_samples)
        {
          out[i] = WT_HIGH[d->wt_index];
        }
      else
        {
          out[i] = WT_LOW[d->wt_index];
        }
      d->wt_index = (d->wt_index + 1) % WT_SIZE;
      d->sample_index += 1;
      if (d->sample_index >= SAMPLE_RATE)
        {
          /*  Move on to the next second. Here we assume that the time_t type
              encodes the time as a number of seconds since an arbitrary point
              in time. Technically this is not specified in the C standard but
              this is how it is typically implemented in practice.
          */
          d->seconds += 1;
          d->sample_index = 0;
          d->local = get_tm (&d->seconds, d->jst);
          d->high_samples = sec_high_samples (d->local);
        }
    }
  return paContinue;
}

void
jjy_populate_wavetables (float WT_HIGH[WT_CAP], float WT_LOW[WT_CAP],
                         bool fukushima)
{
  JJY_FREQ = fukushima ? (40000.0 / 3.0) : 20000.0;
  WT_SIZE = fukushima ? 18 : 12;
  const double PI = acos (-1);
  const double cycles_per_sample = (double)JJY_FREQ / (double)SAMPLE_RATE;
  int i;

  for (i = 0; i < WT_SIZE; i++)
    {
      WT_HIGH[i] = sin ((double)i * 2.0 * PI * cycles_per_sample);
    }
  for (i = 0; i < WT_SIZE; i++)
    {
      WT_LOW[i] = 0.1 * sin ((double)i * 2.0 * PI * cycles_per_sample);
    }
}

/* CLI flag setter functions */

void
fukushima_flag_setter (jjy_args *argsp)
{
  argsp->fukushima = true;
}

void
help_flag_setter (jjy_args *argsp)
{
  argsp->help = true;
}

void
jst_flag_setter (jjy_args *argsp)
{
  argsp->jst = true;
}

void
version_flag_setter (jjy_args *argsp)
{
  argsp->version = true;
}

const jjy_cli_flag cli_flags[]
    = { { 'f', "fukushima", "simulate 40kHz signal", fukushima_flag_setter },
        { 'h', "help", "show this help message and exit", help_flag_setter },
        { 'j', "jst", "force JST timezone", jst_flag_setter },
        { 'v', "version", "print version number and exit",
          version_flag_setter } };
const int flags_count = (sizeof cli_flags) / (sizeof *cli_flags);

bool
parse_jjy_args (jjy_args *argsp, int argc, const char *argv[])
{
  int i;
  int j;
  int k;
  bool arg_parsed;
  bool flag_char_parsed;
  jjy_cli_flag *flag;

  argsp->help = false;
  argsp->fukushima = false;
  argsp->jst = false;
  argsp->version = false;
  for (i = 1; i < argc; i++)
    {
      arg_parsed = false;
      if (strncmp ("--", argv[i], 2) == 0)
        {
          for (j = 0; j < flags_count; j++)
            {
              if (strcmp (cli_flags[j].long_form, &argv[i][2]) == 0)
                {
                  arg_parsed = true;
                  cli_flags[j].setter (argsp);
                  break;
                }
            }
        }
      else if (argv[i][0] == '-')
        {
          arg_parsed = true;
          for (j = 1; argv[i][j] != '\0'; j++)
            {
              flag_char_parsed = false;
              for (k = 0; k < flags_count; k++)
                {
                  if (argv[i][j] == cli_flags[k].short_form)
                    {
                      flag_char_parsed = true;
                      cli_flags[k].setter (argsp);
                      break;
                    }
                }
              if (!flag_char_parsed)
                {
                  fprintf (stderr, "Error: Unrecognized CLI flag -%c\n",
                           argv[i][j]);
                  return false;
                }
            }
        }
      if (!arg_parsed)
        {
          fprintf (stderr, "Error: Unrecognized CLI argument %s\n", argv[i]);
          return false;
        }
    }
  return true;
}

void
print_help (const char *ename)
{
  const char *display_name
      = (ename != NULL && ename[0] != '\0') ? ename : "ersatz_jjy";
  int i;
  int j;
  int spaces;

  printf ("usage: %s", display_name);
  for (i = 0; i < flags_count; i++)
    {
      printf (" [-%c]", cli_flags[i].short_form);
    }
  printf ("\n\n");
  printf ("Output audio simulating JJY radio time signal\n\n");
  printf ("options:\n");
  for (i = 0; i < flags_count; i++)
    {
      printf ("  -%c, --%s", cli_flags[i].short_form, cli_flags[i].long_form);
      spaces = 11 - strlen (cli_flags[i].long_form);
      for (j = 0; j < spaces; j++)
        {
          printf (" ");
        }
      printf ("%s\n", cli_flags[i].help_text);
    }
}

void
print_version (void)
{
  printf ("v%d.%d\n", ERSATZ_JJY_VERSION_MAJOR, ERSATZ_JJY_VERSION_MINOR);
}

void
handle_keyboard_interrupt (int sig)
{
  if (STREAM == NULL)
    {
      quick_exit (0);
    }
  else
    {
      Pa_AbortStream (STREAM);
    }
}

int
main (int argc, const char *argv[])
{
  jjy_args args;
  PaStreamParameters outputParameters;
  PaError err;
  struct timespec now;
  jjy_data data;

  if (!parse_jjy_args (&args, argc, argv))
    {
      return 1;
    }
  if (args.help)
    {
      print_help (argv[0]);
      return 0;
    }
  if (args.version)
    {
      print_version ();
      return 0;
    }
  data.jst = args.jst;

  printf ("ersatz-jjy v%d.%d\n", ERSATZ_JJY_VERSION_MAJOR,
          ERSATZ_JJY_VERSION_MINOR);
  jjy_populate_wavetables (WT_HIGH, WT_LOW, args.fukushima);
  err = Pa_Initialize ();
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  outputParameters.device = Pa_GetDefaultOutputDevice ();
  outputParameters.channelCount = 1;
  outputParameters.sampleFormat = paFloat32;
  outputParameters.suggestedLatency
      = Pa_GetDeviceInfo (outputParameters.device)->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  err = Pa_OpenStream (&STREAM, NULL, /* No input */
                       &outputParameters, SAMPLE_RATE, FRAMES_PER_BUFFER,
                       paClipOff, jjy_stream_callback, &data);
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  signal (SIGINT, handle_keyboard_interrupt);

  timespec_get (&now, TIME_UTC);
  data.seconds = now.tv_sec;
  data.sample_index = now.tv_nsec * SAMPLE_RATE / MAX_NANOSEC;
  data.wt_index = data.sample_index % WT_SIZE;
  data.local = get_tm (&now.tv_sec, args.jst);
  data.high_samples = sec_high_samples (data.local);
  err = Pa_StartStream (STREAM);
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  while (Pa_IsStreamActive (STREAM))
    {
      Pa_Sleep (500);
    }
  err = Pa_CloseStream (STREAM);
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  err = Pa_Terminate ();
  return err;
}
