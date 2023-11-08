#ifndef _FN_TASKMANAGER_H
#define _FN_TASKMANAGER_H

#include <stdint.h>
#include <map>

#include "fnTask.h"


class fnTaskManager
{

public:
    fnTaskManager();
    ~fnTaskManager();
    int submit_task(fnTask * t);
    fnTask * get_task(uint8_t tid);
    int pause_task(uint8_t tid);
    int resume_task(uint8_t tid);
    int abort_task(uint8_t tid);
    bool service();

private:
    int complete_task(uint8_t tid);
    uint8_t get_free_tid();
    void shutdown();

    std::map<uint8_t, fnTask *> _task_map;
    uint8_t _next_tid;
    uint8_t _task_count;
};

// global task manager
extern fnTaskManager taskMgr;

#endif // _FN_TASKMANAGER_H
