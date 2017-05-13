#include <stdio.h>
#include <string.h>
#include <stdarg.h>

//#define MLOG_ERROR(msg,args...) MLog::Log(MLog::MLOG_TYPE_ERROR, msg"\n", args)
//#define MLOG_INFO(msg,args...) MLog::Log(MLog::MLOG_TYPE_INFO, msg"\n", args)
#define MLOG(level,msg,args...) MLog::Log(MLog::MLOG_TYPE_##level, msg"\n", ##args)

class MLog {
    enum {
        MLOG_BUFF_MAX = 1024,
    };
    static char buf[MLOG_BUFF_MAX];
public:
    enum {
        MLOG_TYPE_ERROR,
        MLOG_TYPE_INFO,
    };
    static void Log(int type, const char *msg, ...) {
        va_list ap;
        va_start(ap, msg);
        int len;
        if ((len = vsnprintf(buf, MLOG_BUFF_MAX, msg, ap)) > 0) {
            printf("%s", buf);
        }
        va_end(ap);
    }
};

char MLog::buf[];
    

