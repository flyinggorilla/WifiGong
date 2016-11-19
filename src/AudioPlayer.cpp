#include <Wire.h>
#include <i2s.h>
#include "AudioPlayer.h"

#define DEBUG true

bool AudioPlayer::readWavHeader() {
    // WAV format spec: http://soundfile.sapp.org/doc/WaveFormat/
    if (!_audioFile) {
      Serial.println("No valid file handle.");
      return false;
    }

    uint32_t uint32buf = 0;
    // read ChunkID "RIFF" header
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading header of file.");
      return false;
    }
    if (uint32buf != 0x46464952) {
      Serial.println("No RIFF format header ");
      Serial.print("header: "); Serial.println(uint32buf, HEX);
      return false;
    }
    // read Chunksize: remaining file size
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading size header.");
      return false;
    }
    Serial.println("Chunksize from here: " + String(uint32buf));

    // read Format header: "WAVE"
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error format header.");
      return false;
    }
    if (uint32buf != 0x45564157 ) {
      Serial.println("No WAVE format header");
      return false;
    }

    // read Subchunk1ID: Format header: "fmt "
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error subchunk header.");
      return false;
    }
    if (uint32buf != 0x20746d66  ) {
      Serial.println("No 'fmt ' format header");
      return false;
    }

    // read subChunk1size
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading subchunk1 size.");
      return false;
    }
    Serial.println("subChunk1size: " + String(uint32buf));

    uint16_t uint16buf;

    // read AudioFormat
    if (!_audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading audioformat.");
      return false;
    }
    if (uint16buf != 1  ) {
      Serial.println("Invalid audio format");
      return false;
    }

    // read NumChannels
    if (!_audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading NumChannels.");
      return false;
    }
    if (uint16buf != 1  ) {
      Serial.println("Too many channels. Only MONO files accepted. No Stereo.");
      return false;
    }


    // read sample rate
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading sample rate");
      return false;
    }
    _audioSampleRate = uint32buf;
    Serial.println("Sample rate: " + String(_audioSampleRate));

    // read byte rate
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading byte rate");
      return false;
    }
    Serial.println("Byte rate: " + String(uint32buf));

   // read BlockAlign
    if (!_audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading block align.");
      return false;
    }
    Serial.println("Block align: " + String(uint16buf));

   // read BitsPerSample
    if (!_audioFile.readBytes((char*)&uint16buf, sizeof(uint16buf))) {
      Serial.println("Error reading bits per sample.");
      return false;
    }
    _bitsPerSample = uint16buf;
    Serial.println("Bits per sample: " + String(_bitsPerSample) + " bits");

    // read Subchunk2ID: Format header: "data"
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error data header.");
      return false;
    }
    if (uint32buf != 0x61746164   ) {
      Serial.println("No 'data' header");
      return false;
    }

    // read subChunk1size
    if (!_audioFile.readBytes((char*)&uint32buf, sizeof(uint32buf))) {
      Serial.println("Error reading subchunk2/data size.");
      return false;
    }
    Serial.println("data size / subChunk2size: " + String(uint32buf));

    return true;
}

bool AudioPlayer::openAudioFile(String fileName) {
  _audioFile = SPIFFS.open(fileName, "r");
  if (!_audioFile) {
    Serial.println("Could not open : " + fileName);
    return false;
  }
  size_t size = _audioFile.size();
  Serial.println("Opening file: " + fileName + " size: " + String(size));
  return true;
}

void AudioPlayer::closeAudioFile() {
  _audioFile.close();
}

bool AudioPlayer::nextSample() {
    if (_bitsPerSample == 8) {
      uint8_t eightbitsample;
      if (!_audioFile.read(&eightbitsample, 1)) {
        return false;
      }

      int signedSample = eightbitsample;
      signedSample -= 128;
      signedSample *= 64;
      //_audioSample = _audioSample << 6; // scale 8 bit audio to 16 bit
      _audioSample = (0xFFFF & signedSample); // convert to signed and scale
    } else {

      if (!_audioFile.read((uint8_t*)&_audioSample, 2)) {
        return false;
      }
      //audioSample = audioSample + 0x7FFF; // convert signed to unsigned
      //audioSample = convertSinged2UnsignedSample(audioSample)/2;


    }
    return true;
}


bool AudioPlayer::playFile(String audioFileName) {
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

  playSamples();

  i2s_end();
  digitalWrite(_pinAmplifierSD, LOW); // turn amp off
  closeAudioFile();
  _playing = false;
  wdt_enable(1000*60);
  return true;
}

void AudioPlayer::playSamples() {

  while(nextSample()) {
      wdt_disable();
      /*while(i2s_is_full()) {
        yield();
      }
      i2s_write_sample_nb(_audioSample); // BLOCKING if FULL!
      */

      // the following works fine without the while-loop!
      i2s_write_sample(_audioSample); // BLOCKING if FULL!
  }

}







/*
ESP pin   - I2S signal
----------------------
GPIO2/TX1   - LRCK
GPIO3/RX0   - DATA - note, on the Huzzah/ESP-12, this is the pin on the chip, not on the breakout as the breakout pin is blocked through a diode!!
GPIO15      - BCLK

I2S Amplifier MAX98357A
LRCLK ONLY supports 8kHz, 16kHz, 32kHz, 44.1kHz, 48kHz, 88.2kHz and 96kHz frequencies.
LRCLK clocks at 11.025kHz, 12kHz, 22.05kHz and 24kHz are NOT supported.
Do not remove LRCLK while BCLK is present. Removing LRCLK while BCLK is present can cause unexpected output behavior including a large DC output voltage

*/

/*

void generatorIntHandler();

void playGeneratorGong(uint32_t sampleRate) {
    audioSampleRate = sampleRate;


    bufferFillsPerSecond = audioSampleRate * 8 / (SLC_BUF_CNT*SLC_BUF_LEN) ;
    audioTick = 0; // for generator only needed

    digitalWrite(PIN_I2SAMP, HIGH); // turn amp on
    i2s_begin();
    i2s_set_rate(audioSampleRate);
    audioEnded = false;

    timer1_disable();
    timer1_isr_init();
    timer1_attachInterrupt(generatorIntHandler);
    timer1_enable(TIM_DIV1, TIM_EDGE, TIM_LOOP);
    timer1_write((clockCyclesPerMicrosecond() * 1000000) / bufferFillsPerSecond);
}

boolean getGeneratorSample() {
  float v = sin((float)(audioTick*440*2)*PI/audioSampleRate);
  if (audioTick > audioSampleRate*5)
    audioSample = v*0x7FFE + 0x7FFF;
  else
    audioSample = v*1024 + 1024;

  audioSample = convertSinged2UnsignedSample(audioSample);

  if (audioTick > audioSampleRate*10)
    return false;

  audioTick++;
  return true;
}
ICACHE_RAM_ATTR void generatorIntHandler() {
  while (!i2s_is_full()) {
    if (getGeneratorSample()) {
      i2s_write_sample(audioSample);
      //i2s_write_lr(audioSample, audioSample);

    } else {
      audioEnded = true;
      digitalWrite(PIN_I2SAMP, LOW); // turn amp off
      i2s_end();
      timer1_disable();
      audioFile.close();
      return;
    }
  }
}*/
