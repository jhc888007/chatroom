#ifndef _MESSAGE_H_
#define _MESSAGE_H_


class MMessageStream {
    struct MMessage {
        int type;
        int size;
        char buf[1];
    };
    MMessage *msg;
    int cur_size;
public:
    MMessageStream() {
        msg = NULL;
        cur_size = 0;
    }
    MMessageStream(char *b, int size) {
        msg = (MMessage *)b;
        cur_size = size;
    }
    GetType() {
        return type;
    }
};


#endif
