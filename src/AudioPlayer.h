#include <FS.h>


class AudioPlayer {
  public:
    AudioPlayer(int pinApmplifierSD) {_pinAmplifierSD = pinApmplifierSD;}
    unsigned int getAudioSampleRate() {return _audioSampleRate;};
    unsigned int getBitsPerSample() {return _bitsPerSample;};
    bool playFile(String);
    bool isPlaying();
  protected:
    volatile static bool _playing;
    unsigned int _audioSampleRate;
    unsigned int _bitsPerSample;
    bool readWavHeader();
    bool openAudioFile(String);
    void closeAudioFile();
    bool nextSample();
    File _audioFile;
    uint16_t _audioSample; // mono sample
    int _pinAmplifierSD;
};
