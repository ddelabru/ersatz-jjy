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

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "portaudio.h"
#include "ersatz-jjy-config.h"

/* Macro constants */
#define MAX_NANOSEC (1000000000L)
#define SAMPLE_RATE (48000)
#define FRAMES_PER_BUFFER (64)
#define JJY_FREQ (20000)  /* One-third the actual JJY longwave frequency */
#define WT_SIZE (12)  /* Should hold a whole number of cycles at sample rate */

/* Calculated constants */
const double PI = acos(-1);
const unsigned long JJY_B0_HIGH_SAMPLES = SAMPLE_RATE * 4 / 5;
const unsigned long JJY_B1_HIGH_SAMPLES = SAMPLE_RATE / 2;
const unsigned long JJY_M_HIGH_SAMPLES = SAMPLE_RATE / 5;

/* Global variables */
bool INTERRUPTED = false;
float WT_HIGH[WT_SIZE];
float WT_LOW[WT_SIZE];

typedef struct
{
    time_t seconds;
    struct tm* local;
    unsigned long sample_index;
    unsigned long wt_index;
    unsigned long high_samples;
    PaStream* stream;
}
jjy_data;

bool jjy_b01(const struct tm* t) {
    return (t->tm_min >= 40);
}

bool jjy_b02(const struct tm* t){
    return ((t->tm_min % 40) >= 20);
}

bool jjy_b03(const struct tm* t){
    return ((t->tm_min % 20) >= 10);
}

bool jjy_b05(const struct tm* t){
    return ((t->tm_min % 10) >= 8);
}

bool jjy_b06(const struct tm* t){
    return (((t->tm_min % 10) % 8) >= 4);
}

bool jjy_b07(const struct tm* t){
    return (((t->tm_min % 10) % 4) >= 2);
}

bool jjy_b08(const struct tm* t){
    return ((t->tm_min % 2) > 0);
}

bool jjy_b12(const struct tm* t){
    return (t->tm_hour >= 20);
}

bool jjy_b13(const struct tm* t){
    return ((t->tm_hour % 20) >= 10);
}

bool jjy_b15(const struct tm* t){
    return ((t->tm_hour % 10) >= 8);
}

bool jjy_b16(const struct tm* t){
    return (((t->tm_hour % 10) % 8) >= 4);
}

bool jjy_b17(const struct tm* t){
    return (((t->tm_hour % 10) % 4) >= 2);
}

bool jjy_b18(const struct tm* t){
    return ((t->tm_hour % 2) > 0);
}

bool jjy_b22(const struct tm* t){
    return ((t->tm_yday + 1) >= 200);
}

bool jjy_b23(const struct tm* t){
    return (((t->tm_yday + 1) % 200) >= 100);
}

bool jjy_b25(const struct tm* t){
    return (((t->tm_yday + 1) % 100) >= 80);
}

bool jjy_b26(const struct tm* t){
    return ((((t->tm_yday + 1) % 100) % 80) >= 40);
}

bool jjy_b27(const struct tm* t){
    return ((((t->tm_yday + 1) % 100) % 40) >= 20);
}

bool jjy_b28(const struct tm* t){
    return (((t->tm_yday + 1) % 20) >= 10);
}

bool jjy_b30(const struct tm* t){
    return (((t->tm_yday + 1) % 10) >= 8);
}

bool jjy_b31(const struct tm* t){
    return ((((t->tm_yday + 1) % 10) % 8) >= 4);
}

bool jjy_b32(const struct tm* t){
    return ((((t->tm_yday + 1) % 10) % 4) >= 2);
}

bool jjy_b33(const struct tm* t){
    return (((t->tm_yday + 1) % 2) > 0);
}

bool jjy_b36(const struct tm* t){
    bool even_parity = false;
    even_parity = (even_parity != jjy_b12(t));
    even_parity = (even_parity != jjy_b13(t));
    even_parity = (even_parity != jjy_b15(t));
    even_parity = (even_parity != jjy_b16(t));
    even_parity = (even_parity != jjy_b17(t));
    even_parity = (even_parity != jjy_b18(t));
    return even_parity;
}

bool jjy_b37(const struct tm* t){
    bool even_parity = false;
    even_parity = (even_parity != jjy_b01(t));
    even_parity = (even_parity != jjy_b02(t));
    even_parity = (even_parity != jjy_b03(t));
    even_parity = (even_parity != jjy_b05(t));
    even_parity = (even_parity != jjy_b06(t));
    even_parity = (even_parity != jjy_b07(t));
    even_parity = (even_parity != jjy_b08(t));
    return even_parity;
}

bool jjy_b41(const struct tm* t){
    return ((t->tm_year % 100) >= 80);
}

bool jjy_b42(const struct tm* t){
    return (((t->tm_year % 100) % 80) >= 40);
}

bool jjy_b43(const struct tm* t){
    return (((t->tm_year % 100) % 40) >= 20);
}

bool jjy_b44(const struct tm* t){
    return ((t->tm_year % 20) >= 10);
}

bool jjy_b45(const struct tm* t){
    return ((t->tm_year % 10) >= 8);
}

bool jjy_b46(const struct tm* t){
    return (((t->tm_year % 10) % 8) >= 4);
}

bool jjy_b47(const struct tm* t){
    return (((t->tm_year % 10) % 4) >= 2);
}

bool jjy_b48(const struct tm* t){
    return ((t->tm_year % 2) > 0);
}

bool jjy_b50(const struct tm* t){
    return (t->tm_wday >= 4);
}

bool jjy_b51(const struct tm* t){
    return ((t->tm_wday % 4) >= 2);
}

bool jjy_b52(const struct tm* t){
    return ((t->tm_wday % 2) > 0);
}

bool jjy_b53(const struct tm* t){
    /* TODO: implement leap seconds
        struct tm next_month = {
            .tm_year = t->tm_mon < 11 ? t->tm_year : t->tm_year + 1,
            .tm_mon = (t->tm_mon + 1) % 12,
            .tm_mday = 1
        };
    */
    return 0;
}

bool jjy_b54(const struct tm* t){
    /* TODO: implement leap seconds */
    return 0;
}

unsigned long sec_high_samples(const struct tm* t) {
    bool (*jjy_bit_func[]) (const struct tm*) = {
        NULL,
        jjy_b01,
        jjy_b02,
        jjy_b03,
        NULL,
        jjy_b05,
        jjy_b06,
        jjy_b07,
        jjy_b08,
        NULL,
        NULL,
        NULL,
        jjy_b12,
        jjy_b13,
        NULL,
        jjy_b15,
        jjy_b16,
        jjy_b17,
        jjy_b18,
        NULL,
        NULL,
        NULL,
        jjy_b22,
        jjy_b23,
        NULL,
        jjy_b25,
        jjy_b26,
        jjy_b27,
        jjy_b28,
        NULL,
        jjy_b30,
        jjy_b31,
        jjy_b32,
        jjy_b33,
        NULL,
        NULL,
        jjy_b36,
        jjy_b37,
        NULL,
        NULL,
        NULL,
        jjy_b41,
        jjy_b42,
        jjy_b43,
        jjy_b44,
        jjy_b45,
        jjy_b46,
        jjy_b47,
        jjy_b48,
        NULL,
        jjy_b50,
        jjy_b51,
        jjy_b52,
        jjy_b53,
        jjy_b54,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL,
        NULL
    };
    switch (t->tm_sec) {
        case 0:
        case 9:
        case 19:
        case 29:
        case 39:
        case 49:
        case 59:
        case 60:  /* Leap second */
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
            return jjy_bit_func[t->tm_sec](t) ? JJY_B1_HIGH_SAMPLES : JJY_B0_HIGH_SAMPLES;
        default:
            return JJY_B0_HIGH_SAMPLES;
    }
}

int handle_pa_err(PaError err) {
    Pa_Terminate();
    fprintf(stderr, "PortAudio error %d\n", err);
    fprintf(stderr, "%s\n", Pa_GetErrorText(err));
    return err;
}

void handle_pa_stream_finished(void* userData) {
    INTERRUPTED = true;
}

static int jjy_stream_callback(
    const void *inputBuffer,
    void *outputBuffer,
    unsigned long framesPerBuffer,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags,
    void *userData
) {
    float* out = (float*) outputBuffer;
    unsigned long i;
    jjy_data* d = (jjy_data*) userData;

    for (i = 0; i < framesPerBuffer; i++) {
        if (d->sample_index < d->high_samples) {
            out[i] = WT_HIGH[d->wt_index];
        } else {
            out[i] = WT_LOW[d->wt_index];
        }
        d->wt_index = (d->wt_index + 1) % WT_SIZE;
        d->sample_index += 1;
        if (d->sample_index >= SAMPLE_RATE) {
            d->seconds += 1;
            d->sample_index = 0;
            d->local = localtime(&d->seconds);
            d->high_samples = sec_high_samples(d->local);
        }
    }
    return INTERRUPTED ? paComplete : paContinue;
}

void jjy_populate_wavetables( float WT_HIGH[WT_SIZE], float WT_LOW[WT_SIZE]) {
    const double cycles_per_sample = (double) JJY_FREQ / (double) SAMPLE_RATE;
    for (int i = 0; i < WT_SIZE; i++) {
        WT_HIGH[i] = sin((double) i * 2.0 * PI * cycles_per_sample);
    }
    for (int i = 0; i < WT_SIZE; i++) {
        WT_LOW[i] = 0.1 * sin((double) i * 2.0 * PI * cycles_per_sample);
    }
}

int main(int argc, char* argv[])
{
    PaStreamParameters outputParameters;
    PaError err;
    struct timespec now;
    jjy_data data;

    printf(
        "ersatz-jjy v%d.%d\n",
        ERSATZ_JJY_VERSION_MAJOR,
        ERSATZ_JJY_VERSION_MINOR
    );
    jjy_populate_wavetables(WT_HIGH, WT_LOW);
    err = Pa_Initialize();
    if (err != paNoError) {
        return handle_pa_err(err);
    }
    outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = 1;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;
    err = Pa_OpenStream(
        &data.stream,
        NULL,  /* No input */
        &outputParameters,
        SAMPLE_RATE,
        FRAMES_PER_BUFFER,
        paClipOff,
        jjy_stream_callback,
        &data
    );
    if (err != paNoError) {
        return handle_pa_err(err);
    }
    err = Pa_SetStreamFinishedCallback(
        data.stream,
        handle_pa_stream_finished
    );
    if (err != paNoError) {
        return handle_pa_err(err);
    }

    data.wt_index = 0;
    timespec_get(&now, TIME_UTC);
    data.seconds = now.tv_sec;
    data.sample_index = now.tv_nsec * SAMPLE_RATE / MAX_NANOSEC;
    data.local = localtime(&now.tv_sec);
    data.high_samples = sec_high_samples(data.local);
    err = Pa_StartStream(data.stream);
    if (err != paNoError) {
        return handle_pa_err(err);
    }
    while (!INTERRUPTED) {
        Pa_Sleep(500);
    }
    err = Pa_StopStream(data.stream);
    if (err != paNoError) {
        return handle_pa_err(err);
    }
    err = Pa_CloseStream(data.stream);
    if (err != paNoError) {
        return handle_pa_err(err);
    }
    err = Pa_Terminate();
    return err;
}
