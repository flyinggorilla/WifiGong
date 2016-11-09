#include <Wire.h>
#include <i2s.h>
#include "AudioPlayerInterruptDriven.h"



// taken from source: core_esp8266_i2s.c
#define SLC_BUF_CNT (8) //Number of buffers in the I2S circular buffer
#define SLC_BUF_LEN (64) //Length of one buffer, in 32-bit words.
//--> 512 the buffer can hold 512 samples. we want it always at least 75% full, so we must more than 500 fills per second for 44khz


static AudioPlayer* audioPlayer;
ICACHE_RAM_ATTR void t1I2SIntHandler() {
  while (!i2s_is_full()) {
    if (audioPlayer->nextSample()) {
      i2s_write_sample_nb(audioPlayer->getAudioSample());
    } else {
      digitalWrite(audioPlayer->_pinAmplifierSD, LOW); // turn amp off
      i2s_end();
      timer1_disable();
      audioPlayer->closeAudioFile();
      audioPlayer->_playing = false;
      return;
    }
  }
}

bool AudioPlayerInterruptDriven::playFile(String audioFileName) {
  if (!openAudioFile(audioFileName))
    return false;

  if (!readWavHeader()){
    closeAudioFile();
    return false;
  }

  _playing = true;
  digitalWrite(_pinAmplifierSD, HIGH); // turn amp on
  i2s_begin();
  i2s_set_rate(_audioSampleRate);

  wdt_enable(1000*60);
  audioPlayer = this;

  unsigned int bufferFillsPerSecond = _audioSampleRate * 32 / (SLC_BUF_CNT*SLC_BUF_LEN) ;


  timer1_disable();
  timer1_isr_init();
  timer1_attachInterrupt(t1I2SIntHandler);
  timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
  timer1_write((clockCyclesPerMicrosecond() * 1000000) / bufferFillsPerSecond);

  return true;
}
