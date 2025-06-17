/*  ersatz-wwvb: Simulate WWVB radio time signal
    Copyright (C) 2024-2025 Dominic Delabruere
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
#define SAMPLE_SCALE (32767) /* Maximum value of an audio sample */
#define FRAMES_PER_BUFFER (512)
#define WWVB_FREQ (20000) /* One-third the actual WWVB longwave frequency */
#define WT_SIZE (12)
#define PS_INDEX (6) /* wavetable index phase-shifted 180 degrees */

/* Calculated constants */
const unsigned long WWVB_B0_LOW_SAMPLES = SAMPLE_RATE / 5;
const unsigned long WWVB_B1_LOW_SAMPLES = SAMPLE_RATE / 2;
const unsigned long WWVB_M_LOW_SAMPLES = SAMPLE_RATE * 4 / 5;
const unsigned long long HALF_HOUR_SEQ_BITS[]
    = { 0x34bd771e648ab67f, 0xb5037c1610e8c4e5 };
const unsigned long long FIXED_TIMING_WORD[]
    = { 0x42a5cb431d9a6b8b, 0x0000009207fb6b47 };

/* Global PulseAudio stream reference */
PaStream *STREAM = NULL;

/*  Wavetables holding sequential audio samples for high (full amplitude) and
    low (10% amplitude) signal states. These are populated by
    populate_wwvb_wavetables() at startup, then samples are repeatedly copied
    from them directly into the audio buffer. This eliminates the need for
    performing computationally expensive sine calculations while writing to the
    buffer, allowing for smooth sine-wave playback. The size of the wavetables
    is chosen so that it contains a whole number of sine-wave cycles for the
    given sample rate; for example, 12 samples at a 48kHz sample rate contain
    exactly 5 cycles of a 20kHz sine-wave; this ensures that consecutive
    repetitions of the wavetable encode a continuous sine-wave at a constant
    frequency.
*/
int16_t WT_HIGH[WT_SIZE];
int16_t WT_LOW[WT_SIZE];

typedef struct
{
  bool help;
  bool version;
} wwvb_args;

typedef struct
{
  char short_form;
  char *long_form;
  char *help_text;
  void (*setter) (wwvb_args *);
} wwvb_cli_flag;

typedef struct
{
  time_t seconds;
  unsigned long sample_index;
  unsigned long wt_index;
  unsigned long low_samples;
} wwvb_data;

/* Functions that calculate individual bits of the WWVB AM time code */

bool
wwvb_b01 (const time_t *t)
{
  return (gmtime (t)->tm_min >= 40);
}

bool
wwvb_b02 (const time_t *t)
{
  return ((gmtime (t)->tm_min % 40) >= 20);
}

bool
wwvb_b03 (const time_t *t)
{
  return ((gmtime (t)->tm_min % 20) >= 10);
}

bool
wwvb_b05 (const time_t *t)
{
  return ((gmtime (t)->tm_min % 10) >= 8);
}

bool
wwvb_b06 (const time_t *t)
{
  return (((gmtime (t)->tm_min % 10) % 8) >= 4);
}

bool
wwvb_b07 (const time_t *t)
{
  return (((gmtime (t)->tm_min % 10) % 4) >= 2);
}

bool
wwvb_b08 (const time_t *t)
{
  return ((gmtime (t)->tm_min % 2) > 0);
}

bool
wwvb_b12 (const time_t *t)
{
  return (gmtime (t)->tm_hour >= 20);
}

bool
wwvb_b13 (const time_t *t)
{
  return ((gmtime (t)->tm_hour % 20) >= 10);
}

bool
wwvb_b15 (const time_t *t)
{
  return ((gmtime (t)->tm_hour % 10) >= 8);
}

bool
wwvb_b16 (const time_t *t)
{
  return (((gmtime (t)->tm_hour % 10) % 8) >= 4);
}

bool
wwvb_b17 (const time_t *t)
{
  return (((gmtime (t)->tm_hour % 10) % 4) >= 2);
}

bool
wwvb_b18 (const time_t *t)
{
  return ((gmtime (t)->tm_hour % 2) > 0);
}

bool
wwvb_b22 (const time_t *t)
{
  return ((gmtime (t)->tm_yday + 1) >= 200);
}

bool
wwvb_b23 (const time_t *t)
{
  return (((gmtime (t)->tm_yday + 1) % 200) >= 100);
}

bool
wwvb_b25 (const time_t *t)
{
  return (((gmtime (t)->tm_yday + 1) % 100) >= 80);
}

bool
wwvb_b26 (const time_t *t)
{
  return ((((gmtime (t)->tm_yday + 1) % 100) % 80) >= 40);
}

bool
wwvb_b27 (const time_t *t)
{
  return ((((gmtime (t)->tm_yday + 1) % 100) % 40) >= 20);
}

bool
wwvb_b28 (const time_t *t)
{
  return (((gmtime (t)->tm_yday + 1) % 20) >= 10);
}

bool
wwvb_b30 (const time_t *t)
{
  return (((gmtime (t)->tm_yday + 1) % 10) >= 8);
}

bool
wwvb_b31 (const time_t *t)
{
  return ((((gmtime (t)->tm_yday + 1) % 10) % 8) >= 4);
}

bool
wwvb_b32 (const time_t *t)
{
  return ((((gmtime (t)->tm_yday + 1) % 10) % 4) >= 2);
}

bool
wwvb_b33 (const time_t *t)
{
  return (((gmtime (t)->tm_yday + 1) % 2) > 0);
}

/* Bits 36-38 and 40-43 of the WWVB time code provide DUT1 information. The C
   standard libraries provide no information about DUT1, so this code assumes
   a constant DUT1 value of +0.0s, and expects that a receiving device will
   ignore the DUT1 value.
*/

bool
wwvb_b36 (const time_t *t)
{
  return true;
}

bool
wwvb_b37 (const time_t *t)
{
  return false;
}

bool
wwvb_b38 (const time_t *t)
{
  return true;
}

bool
wwvb_b40 (const time_t *t)
{
  return false;
}

bool
wwvb_b41 (const time_t *t)
{
  return false;
}

bool
wwvb_b42 (const time_t *t)
{
  return false;
}

bool
wwvb_b43 (const time_t *t)
{
  return false;
}

bool
wwvb_b45 (const time_t *t)
{
  return ((gmtime (t)->tm_year % 100) >= 80);
}

bool
wwvb_b46 (const time_t *t)
{
  return (((gmtime (t)->tm_year % 100) % 80) >= 40);
}

bool
wwvb_b47 (const time_t *t)
{
  return (((gmtime (t)->tm_year % 100) % 40) >= 20);
}

bool
wwvb_b48 (const time_t *t)
{
  return ((gmtime (t)->tm_year % 20) >= 10);
}

bool
wwvb_b50 (const time_t *t)
{
  return ((gmtime (t)->tm_year % 10) >= 8);
}

bool
wwvb_b51 (const time_t *t)
{
  return (((gmtime (t)->tm_year % 10) % 8) >= 4);
}

bool
wwvb_b52 (const time_t *t)
{
  return (((gmtime (t)->tm_year % 10) % 4) >= 2);
}

bool
wwvb_b53 (const time_t *t)
{
  return ((gmtime (t)->tm_year % 2) > 0);
}

bool
wwvb_b55 (const time_t *t)
{
  const unsigned int year = gmtime (t)->tm_year + 1900;

  return (year % 4 == 0) && ((year % 100 == 0) == (year % 400 == 0));
}

bool
wwvb_b56 (const time_t *t)
{
  /*  Bit 56 should indicate whether the current UTC month ends with a
      (positive) leap second, but the system time used by C standard libraries
      does not capture leap seconds in many implementations, so here we always
      assume no upcoming leap second.
  */
  return false;
}

bool
wwvb_b57 (const time_t *t)
{
  time_t local_offset;
  struct tm *utc_now;
  struct tm eod_utc;
  time_t eod_local;

  utc_now = gmtime (t);
  local_offset = *t - mktime (utc_now);
  eod_utc.tm_year = utc_now->tm_year, eod_utc.tm_mon = utc_now->tm_mon,
  eod_utc.tm_mday = utc_now->tm_mday, eod_utc.tm_hour = 23,
  eod_utc.tm_min = 59, eod_utc.tm_sec = 59, eod_utc.tm_wday = utc_now->tm_wday,
  eod_utc.tm_yday = utc_now->tm_yday, eod_utc.tm_isdst = 0;
  eod_local = mktime (&eod_utc) + local_offset;
  return localtime (&eod_local)->tm_isdst;
}

bool
wwvb_b58 (const time_t *t)
{
  time_t local_offset;
  struct tm *utc_now;
  struct tm bod_utc;
  time_t bod_local;

  utc_now = gmtime (t);
  local_offset = *t - mktime (utc_now);
  bod_utc.tm_year = utc_now->tm_year;
  bod_utc.tm_mon = utc_now->tm_mon;
  bod_utc.tm_mday = utc_now->tm_mday;
  bod_utc.tm_hour = 0;
  bod_utc.tm_min = 0;
  bod_utc.tm_sec = 0;
  bod_utc.tm_wday = utc_now->tm_wday;
  bod_utc.tm_yday = utc_now->tm_yday;
  bod_utc.tm_isdst = 0;
  bod_local = mktime (&bod_utc) + local_offset;
  return localtime (&bod_local)->tm_isdst;
}

unsigned long
minute_of_century (const struct tm *t)
{
  int year;
  int first_year;
  unsigned long total_minutes;
  int i;
  const unsigned int minutes_per_day = 1440;

  total_minutes = 0;
  year = t->tm_year + 1900;
  first_year = year - (year % 100);
  for (i = first_year; i < year; i++)
    {
      if ((i % 4 == 0) && ((i % 100 == 0) == (i % 400 == 0)))
        {
          total_minutes += (366 * minutes_per_day);
        }
      else
        {
          total_minutes += (365 * minutes_per_day);
        }
    }
  total_minutes += (t->tm_yday * minutes_per_day);
  total_minutes += (t->tm_hour * 60);
  total_minutes += t->tm_min;
  return total_minutes;
}

bool
wwvb_pm_time (const struct tm *t, const unsigned long *mins)
{
  int i;

  if (t->tm_sec >= 40)
    {
      i = 46 - t->tm_sec;
    }
  else if (t->tm_sec >= 30)
    {
      i = 45 - t->tm_sec;
    }
  else if (t->tm_sec >= 20)
    {
      i = 44 - t->tm_sec;
    }
  else if (t->tm_sec == 19)
    {
      i = 0;
    }
  else
    {
      /* Only remaining case should be second 18 */
      i = 25;
    }
  return (*mins & (1 << i)) != 0;
}

bool
wwvb_pm_ecc (const struct tm *t, const unsigned long *mins)
{
  /* Odd-parity Hamming code over the 26 time code bits except bit 0 */
  int p;
  int i;
  bool b;
  struct tm data_bit_tm;

  p = 17 - t->tm_sec;
  b = true;
  data_bit_tm = *t;
  for (i = 1; i < 26; i++)
    {
      if (!((1 << p) & i))
        {
          continue;
        }
      if (i <= 6)
        {
          data_bit_tm.tm_sec = 46 - i;
        }
      else if (i <= 15)
        {
          data_bit_tm.tm_sec = 45 - i;
        }
      else if (i <= 24)
        {
          data_bit_tm.tm_sec = 44 - i;
        }
      else
        {
          data_bit_tm.tm_sec = 18;
        }
      b = (b != wwvb_pm_time (&data_bit_tm, mins));
    }
  return b;
}

bool
access_bit (const unsigned long long a[], int index)
{
  return ((1 << (index % 64)) & a[index / 64]) != 0;
}

int
half_hour_seq (const struct tm *t, bool dst_eod, bool dst_bod)
{
  if (!(dst_eod || dst_bod))
    {
      return (t->tm_hour * 4) + (t->tm_min / 17) + 1;
    }
  else if (dst_eod && dst_bod)
    {
      return (t->tm_hour * 4) + (t->tm_min / 17) + 2;
    }
  else if (dst_eod && !dst_bod)
    {
      if (t->tm_hour <= 3)
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 1;
        }
      else if (t->tm_hour <= 10)
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 81;
        }
      else /* t->tm_hour > 10 */
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 2;
        }
    }
  else /* !dst_eod && dst_bod */
    {
      if (t->tm_hour <= 3)
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 2;
        }
      else if (t->tm_hour <= 10)
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 82;
        }
      else /* t->tm_hour > 10 */
        {
          return (t->tm_hour * 4) + (t->tm_min / 17) + 1;
        }
    }
}

bool
wwvb_pm_six_min (const time_t *t)
{
  int frame_sec;
  int seq;
  struct tm *now;

  now = gmtime (t);
  frame_sec = ((now->tm_min % 10) * 60) + now->tm_sec;
  if (frame_sec < 127)
    {
      seq = half_hour_seq (now, wwvb_b57 (t), wwvb_b58 (t));
      return access_bit (HALF_HOUR_SEQ_BITS, (seq - 1 + frame_sec) % 127);
    }
  else if (frame_sec < 233)
    {
      return access_bit (FIXED_TIMING_WORD, frame_sec - 127);
    }
  else /* frame_sec >= 233 */
    {
      seq = half_hour_seq (now, wwvb_b57 (t), wwvb_b58 (t));
      return access_bit (HALF_HOUR_SEQ_BITS, (seq + 358 - frame_sec) % 127);
    }
}

bool
wwvb_pm (const time_t *t)
{
  struct tm *now;
  unsigned long mins;

  now = gmtime (t);
  if (((now->tm_min % 30 >= 10) && now->tm_min % 30 <= 16))
    {
      return wwvb_pm_six_min (t);
    }
  switch (now->tm_sec)
    {
    case 0:
    case 1:
    case 5:
    case 8:
    case 10:
    case 11:
    case 12:
    case 29:
    case 39:
    case 49:
    case 59:
    case 60:
      return false;
    case 2:
    case 3:
    case 4:
    case 6:
    case 7:
    case 9:
      return true;
    case 13:
    case 14:
    case 15:
    case 16:
    case 17:
      mins = minute_of_century (now);
      return wwvb_pm_ecc (now, &mins);
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23:
    case 24:
    case 25:
    case 26:
    case 27:
    case 28:
    case 30:
    case 31:
    case 32:
    case 33:
    case 34:
    case 35:
    case 36:
    case 37:
    case 38:
    case 40:
    case 41:
    case 42:
    case 43:
    case 44:
    case 45:
    case 46:
      mins = minute_of_century (now);
      return wwvb_pm_time (now, &mins);
    /*  Phase modulation code bits 47-52, excluding bit 49, encode leap second
        information together with DST status and error correction. This
        implementation is simplified because it assumes no upcoming leap
        second.
    */
    case 47:
    case 50:
      return wwvb_b57 (t) != wwvb_b58 (t);
    case 48:
      return !(wwvb_b57 (t) || wwvb_b58 (t));
    case 51:
      return wwvb_b57 (t);
    case 52:
      return wwvb_b58 (t);
    /*  Bits 53-59 of the phase modulation code denote the DST rules in effect
        for the U.S. For simplicity, this implementation assumes that
        established rules remain in effect: DST begins at 2:00 AM local time
        on the second Sunday in March, and ends at 2:00 AM local time on the
        first Sunday in November.
    */
    case 53:
      return false;
    case 54:
      return true;
    case 55:
      return true;
    case 56:
      return false;
    case 57:
      return true;
    case 58:
      return true;
    default:
      return false;
    }
}

unsigned long
sec_low_samples (const time_t *t)
{
  struct tm *utc = gmtime (t);
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
  bool (*wwvb_bit_func[]) (const time_t *) = {
    NULL,     wwvb_b01, wwvb_b02, wwvb_b03, NULL,     wwvb_b05, wwvb_b06,
    wwvb_b07, wwvb_b08, NULL,     NULL,     NULL,     wwvb_b12, wwvb_b13,
    NULL,     wwvb_b15, wwvb_b16, wwvb_b17, wwvb_b18, NULL,     NULL,
    NULL,     wwvb_b22, wwvb_b23, NULL,     wwvb_b25, wwvb_b26, wwvb_b27,
    wwvb_b28, NULL,     wwvb_b30, wwvb_b31, wwvb_b32, wwvb_b33, NULL,
    NULL,     wwvb_b36, wwvb_b37, wwvb_b38, NULL,     wwvb_b40, wwvb_b41,
    wwvb_b42, wwvb_b43, NULL,     wwvb_b45, wwvb_b46, wwvb_b47, wwvb_b48,
    NULL,     wwvb_b50, wwvb_b51, wwvb_b52, wwvb_b53, NULL,     wwvb_b55,
    wwvb_b56, wwvb_b57, wwvb_b58, NULL,     NULL /* Second 60, a leap second */
  };

  switch (utc->tm_sec)
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
      return WWVB_M_LOW_SAMPLES;
    case 4:
    case 10:
    case 11:
    case 14:
    case 20:
    case 21:
    case 24:
    case 34:
    case 35:
    case 44:
    case 54:
      /* These seconds of the 60-second time code always encode 0 */
      return WWVB_B0_LOW_SAMPLES;
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
    case 38:
    case 40:
    case 41:
    case 42:
    case 43:
    case 45:
    case 46:
    case 47:
    case 48:
    case 50:
    case 51:
    case 52:
    case 53:
    case 55:
    case 56:
    case 57:
    case 58:
      /* These seconds encode variable bits with time information */
      return (wwvb_bit_func[utc->tm_sec](t) ? WWVB_B1_LOW_SAMPLES
                                            : WWVB_B0_LOW_SAMPLES);
    default:
      /* In practice, this block should be unreachable */
      return WWVB_B0_LOW_SAMPLES;
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

static int
wwvb_stream_callback (const void *inputBuffer, void *outputBuffer,
                      unsigned long framesPerBuffer,
                      const PaStreamCallbackTimeInfo *timeInfo,
                      PaStreamCallbackFlags statusFlags, void *userData)
{
  int16_t *out = (int16_t *)outputBuffer;
  unsigned long i;
  wwvb_data *d = (wwvb_data *)userData;

  for (i = 0; i < framesPerBuffer; i++)
    {
      if (d->sample_index == (SAMPLE_RATE / 10))
        {
          d->wt_index = wwvb_pm (&d->seconds) ? PS_INDEX : 0;
        }
      if (d->sample_index < d->low_samples)
        {
          out[i] = WT_LOW[d->wt_index];
        }
      else
        {
          out[i] = WT_HIGH[d->wt_index];
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
          d->low_samples = sec_low_samples (&d->seconds);
        }
    }
  return paContinue;
}

void
wwvb_populate_wavetables (int16_t WT_HIGH[WT_SIZE], int16_t WT_LOW[WT_SIZE])
{
  const double PI = acos (-1);
  const double cycles_per_sample = (double)WWVB_FREQ / (double)SAMPLE_RATE;
  int i;

  for (i = 0; i < WT_SIZE; i++)
    {
      WT_HIGH[i]
          = SAMPLE_SCALE * sin ((double)i * 2.0 * PI * cycles_per_sample);
    }
  for (i = 0; i < WT_SIZE; i++)
    {
      WT_LOW[i] = SAMPLE_SCALE * 0.02
                  * sin ((double)i * 2.0 * PI * cycles_per_sample);
    }
}

/* CLI flag setter functions */

void
help_flag_setter (wwvb_args *argsp)
{
  argsp->help = true;
}

void
version_flag_setter (wwvb_args *argsp)
{
  argsp->version = true;
}

const wwvb_cli_flag cli_flags[]
    = { { 'h', "help", "show this help message and exit", help_flag_setter },
        { 'v', "version", "print version number and exit",
          version_flag_setter } };
const int flags_count = (sizeof cli_flags) / (sizeof *cli_flags);

bool
parse_wwvb_args (wwvb_args *argsp, int argc, const char *argv[])
{
  int i;
  int j;
  int k;
  bool arg_parsed;
  bool flag_char_parsed;
  wwvb_cli_flag *flag;

  argsp->help = false;
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
      = (ename != NULL && ename[0] != '\0') ? ename : "ersatz_wwvb";
  int i;
  int j;
  int spaces;

  printf ("usage: %s", display_name);
  for (i = 0; i < flags_count; i++)
    {
      printf (" [-%c]", cli_flags[i].short_form);
    }
  printf ("\n\n");
  printf ("Output audio simulating WWVB radio time signal\n\n");
  printf ("options:\n");
  for (i = 0; i < flags_count; i++)
    {
      printf ("  -%c, --%s", cli_flags[i].short_form, cli_flags[i].long_form);
      spaces = 9 - strlen (cli_flags[i].long_form);
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
      exit (0);
    }
  else
    {
      Pa_AbortStream (STREAM);
    }
}

int
main (int argc, const char *argv[])
{
  wwvb_args args;
  PaStreamParameters outputParameters;
  PaError err;
  struct timespec now;
  wwvb_data data;

  if (!parse_wwvb_args (&args, argc, argv))
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

  printf ("ersatz-wwvb v%d.%d\n", ERSATZ_JJY_VERSION_MAJOR,
          ERSATZ_JJY_VERSION_MINOR);
  wwvb_populate_wavetables (WT_HIGH, WT_LOW);
  err = Pa_Initialize ();
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  outputParameters.device = Pa_GetDefaultOutputDevice ();
  outputParameters.channelCount = 1;
  outputParameters.sampleFormat = paInt16;
  outputParameters.suggestedLatency
      = Pa_GetDeviceInfo (outputParameters.device)->defaultLowOutputLatency;
  outputParameters.hostApiSpecificStreamInfo = NULL;
  err = Pa_OpenStream (&STREAM, NULL, /* No input */
                       &outputParameters, SAMPLE_RATE, FRAMES_PER_BUFFER,
                       paClipOff, wwvb_stream_callback, &data);
  if (err != paNoError)
    {
      return handle_pa_err (err);
    }
  signal (SIGINT, handle_keyboard_interrupt);
  signal (SIGTERM, handle_keyboard_interrupt);

  timespec_get (&now, TIME_UTC);
  data.seconds = now.tv_sec;
  data.sample_index = now.tv_nsec * SAMPLE_RATE / MAX_NANOSEC;
  data.wt_index = data.sample_index % WT_SIZE;
  data.low_samples = sec_low_samples (&data.seconds);
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
