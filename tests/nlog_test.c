#include "ncore/nlog.h"

int main(void)
{
    NLOG_D("这条 DEBUG 默认不会输出");
    NLOG_I("info value=%d", 1);
    NLOG_W("warn value=%d", 2);
    NLOG_E("error value=%d", 3);

    nlog_set_level(NLOG_LEVEL_DEBUG);
    NLOG_D("debug value=%d", 4);

    return 0;
}
