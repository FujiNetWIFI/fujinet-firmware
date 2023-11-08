#ifndef ESP_PLATFORM

#include "fnTask.h"
#include "debug.h"

fnTask::fnTask()
{
    Debug_println("fnTask::fnTask");
    _id = 0;
    _state = TASK_READY;
    _reason = TASK_COMPLETED;
    _callback = nullptr;
}


fnTask::~fnTask()
{
    Debug_printf("fnTask::~fnTask #%d\n", _id);
}


fnTestTask::fnTestTask(int count)
{
    Debug_printf("fnTestTask::fnTestTask(%d)\n", count);
    _count = count;
    _i = 0;
}

fnTestTask::~fnTestTask() 
{
    Debug_printf("fnTestTask::~fnTestTask #%d\n", _id);
}

int fnTestTask::get_progress()
{
    Debug_printf("fnTestTask::get_progress #%d\n", _id);
    return 0;
}

void * fnTestTask::get_result()
{
    Debug_printf("fnTestTask::get_result #%d\n", _id);
    return nullptr;
}


int fnTestTask::start()
{
    Debug_printf("fnTestTask::start #%d\n", _id);
    return 0;
}

int fnTestTask::pause()
{
    Debug_printf("fnTestTask::pause #%d\n", _id);
    return 0;
}

int fnTestTask::resume()
{
    Debug_printf("fnTestTask::resume #%d\n", _id);
    return 0;
}

int fnTestTask::abort()
{
    Debug_printf("fnTestTask::abort #%d\n", _id);
    return 0;
}

int fnTestTask::step()
{
    Debug_printf("fnTestTask::step #%d - %d\n", _id, _i);
    if (++_i<_count)
        return 0;   // continue
    return 1;       // done
}

#endif // !ESP_PLATFORM