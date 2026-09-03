#include "nlog.h"

int main(void)
{
    NLOG_D("test", "这条 DEBUG 默认不会输出");
    NLOG_I("test", "info value=%d", 1);
    NLOG_W("test", "warn value=%d", 2);
    NLOG_E("test", "error value=%d", 3);

    nlog_set_level(NLOG_LEVEL_DEBUG);
    NLOG_D("test", "debug value=%d", 4);

    return 0;
}
