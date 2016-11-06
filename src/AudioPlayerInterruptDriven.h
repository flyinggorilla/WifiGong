#include <FS.h>
#include "AudioPlayer.h"

class AudioPlayerInterruptDriven : public AudioPlayer {
public:
    void playFile(String);
protected:
    ICACHE_RAM_ATTR void t1I2SIntHandler();
};
