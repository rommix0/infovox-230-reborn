/* audio.h on Windows: waveOut with 4 buffers of 100 ms, waiting on WHDR_DONE through an
 * event */
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

#define NBUF 4

static HWAVEOUT wo;
static HANDLE done_ev;
static WAVEHDR hdr[NBUF];
static short *bufs[NBUF];
static int cur, fill, bufsamples;

int audio_open(int fs)
{
    WAVEFORMATEX f;
    int k;

    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = (DWORD)fs;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = 2 * (DWORD)fs;
    done_ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (done_ev == NULL)
        return -1;
    if (waveOutOpen(&wo, WAVE_MAPPER, &f, (DWORD_PTR)done_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(done_ev);
        return -1;
    }
    bufsamples = fs / 10;
    for (k = 0; k < NBUF; k++) {
        bufs[k] = malloc((size_t)bufsamples * sizeof(short));
        memset(&hdr[k], 0, sizeof hdr[k]);
        hdr[k].lpData = (LPSTR)bufs[k];
        hdr[k].dwBufferLength = (DWORD)bufsamples * sizeof(short);
        waveOutPrepareHeader(wo, &hdr[k], sizeof hdr[k]);
        hdr[k].dwFlags |= WHDR_DONE;   /* free */
    }
    cur = fill = 0;
    return 0;
}

static void submit(void)
{
    hdr[cur].dwBufferLength = (DWORD)fill * sizeof(short);
    hdr[cur].dwFlags &= ~WHDR_DONE;
    waveOutWrite(wo, &hdr[cur], sizeof hdr[cur]);
    cur = (cur + 1) % NBUF;
    fill = 0;
}

int audio_write(const short *pcm, int n)
{
    int k;

    while (n > 0) {
        while (fill == 0 && !(hdr[cur].dwFlags & WHDR_DONE))
            WaitForSingleObject(done_ev, INFINITE);
        k = bufsamples - fill;
        if (k > n)
            k = n;
        memcpy(bufs[cur] + fill, pcm, (size_t)k * sizeof(short));
        fill += k;
        pcm += k;
        n -= k;
        if (fill == bufsamples)
            submit();
    }
    return 0;
}

void audio_close(int drain)
{
    int k;

    if (drain && fill > 0)
        submit();
    if (drain) {
        for (k = 0; k < NBUF; k++)
            while (!(hdr[k].dwFlags & WHDR_DONE))
                WaitForSingleObject(done_ev, INFINITE);
    } else {
        waveOutReset(wo);
    }
    for (k = 0; k < NBUF; k++) {
        waveOutUnprepareHeader(wo, &hdr[k], sizeof hdr[k]);
        free(bufs[k]);
    }
    waveOutClose(wo);
    CloseHandle(done_ev);
}
