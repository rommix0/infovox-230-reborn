/* Speaker output for ivxsay: 16-bit mono PCM to the default device.
 * cli/audio_win.c (waveOut) on Windows, cli/audio_alsa.c (ALSA "default") on Linux. */
#ifndef AUDIO_H
#define AUDIO_H

int audio_open(int fs);                     /* 0, or -1 if the device cannot be opened */
int audio_write(const short *pcm, int n);   /* blocks while the device buffers are full */
void audio_close(int drain);                /* drain: play what is buffered; else drop it */

#endif
