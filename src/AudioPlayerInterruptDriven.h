#include <FS.h>
#include "AudioPlayer.h"

class AudioPlayerInterruptDriven : public AudioPlayer {
public:
    AudioPlayerInterruptDriven(int pinApmplifierSD) : AudioPlayer(pinApmplifierSD) {};

    bool playFile(String);
};
