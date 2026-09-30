/* audio.h on Linux: ALSA's "default" device (PipeWire and PulseAudio provide it too) */
#include <alsa/asoundlib.h>
#include "audio.h"

static snd_pcm_t *pcm_dev;

int audio_open(int fs)
{
    if (snd_pcm_open(&pcm_dev, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return -1;
    if (snd_pcm_set_params(pcm_dev, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,
                           (unsigned)fs, 1, 200000) < 0) {
        snd_pcm_close(pcm_dev);
        return -1;
    }
    return 0;
}

int audio_write(const short *pcm, int n)
{
    snd_pcm_sframes_t k;

    while (n > 0) {
        k = snd_pcm_writei(pcm_dev, pcm, (snd_pcm_uframes_t)n);
        if (k < 0) {
            if (snd_pcm_recover(pcm_dev, (int)k, 1) < 0)
                return -1;
            continue;
        }
        pcm += k;
        n -= (int)k;
    }
    return 0;
}

void audio_close(int drain)
{
    if (drain)
        snd_pcm_drain(pcm_dev);
    else
        snd_pcm_drop(pcm_dev);
    snd_pcm_close(pcm_dev);
}
