#ifndef GLOBAL_STATUS_H
#define GLOBAL_STATUS_H

enum PlayerState {
    STATE_IDLE,
    STATE_READY,
    STATE_PLAYING,
    STATE_PAUSED,
    STATE_ENDED
};

class GlobalVars {
public:
    static PlayerState playerState;
};

#endif
