#ifndef ESP_PLATFORM

#include <list>

#include "fnTaskManager.h"
#include "debug.h"

// global task manager object
fnTaskManager taskMgr;


fnTaskManager::fnTaskManager()
{
    // Debug_println("fnTaskManager::fnTaskManager");
    _next_tid = 1;
    _task_count = 0;
}

fnTaskManager::~fnTaskManager()
{
    // Debug_println("fnTaskManager::~fnTaskManager");
    shutdown();
}

void fnTaskManager::shutdown()
{
    // abort tasks, if any
    for (auto it = _task_map.begin(); it != _task_map.end(); ++it)
    {
        Debug_printf("Aborting task %d\n", it->first);
        it->second->abort();
        delete it->second;
    }
    _task_map.clear();
    _task_count = 0;
}

int fnTaskManager::submit_task(fnTask * t)
{
    Debug_println("submit_task");

    for (auto it = _task_map.begin(); it != _task_map.end(); ++it)
    {
        if (it->second == t)
        {
            Debug_printf(" alredy submitted (task %d)!\n", it->first);
            return 0;
        }
    }

    uint8_t tid = get_free_tid();
    if (tid == 0) 
    {
        Debug_println(" failed to get free task ID");
    }
    else 
    {
        // store task
        t->_id = tid;
        _task_count += 1;
        _task_map[tid] = t;
        _next_tid = tid+1;
        Debug_printf(" submitted #%d\n", tid);
    }
    return tid;
}

uint8_t fnTaskManager::get_free_tid()
{
    uint8_t stop = _next_tid;
    uint8_t tid = _next_tid;
    while(_task_map.find(tid) != _task_map.end())
    {
        // try next ID, skip ID 0
        if (++tid == 0) ++tid;
        if(tid == stop)
        {
            // wrapped around, no free ID
            tid = 0;
            break;
        }
    }
    return tid;
}

fnTask * fnTaskManager::get_task(uint8_t tid)
{
    Debug_printf("get_task %d\n", tid);
    std::map<uint8_t, fnTask *>::iterator it = _task_map.find(tid);
    if (it == _task_map.end())
        return nullptr;
    return it->second;
}

int fnTaskManager::pause_task(uint8_t tid)
{
    Debug_printf("pause_task %d\n", tid);
    fnTask *task = get_task(tid);
    if (task == nullptr)
        return -1;
    if (task->_state != fnTask::TASK_RUNNING)
        return -1;
    int result = task->pause();
    task->_state = fnTask::TASK_PAUSED;
    // TODO callback
    return result;
}

int fnTaskManager::resume_task(uint8_t tid)
{
    Debug_printf("resume_task %d\n", tid);
    fnTask *task = get_task(tid);
    if (task == nullptr)
        return -1;
    if (task->_state != fnTask::TASK_PAUSED)
        return -1;
    int result = task->resume();
    task->_state = fnTask::TASK_RUNNING;
    // TODO callback
    return result;
}

int fnTaskManager::abort_task(uint8_t tid)
{
    Debug_printf("abort_task %d\n", tid);
    fnTask *task = get_task(tid);
    if (task == nullptr)
        return -1;
    int result = task->abort();
    task->_state = fnTask::TASK_DONE;
    task->_reason = fnTask::TASK_ABORTED;
    // TODO callback
    // remove aborted task
    _task_count -= 1;
    _task_map.erase(tid);
    delete task;
    return result;
}

int fnTaskManager::complete_task(uint8_t tid)
{
    Debug_printf("complete_task %d\n", tid);
    fnTask *task = get_task(tid);
    if (task == nullptr)
        return -1;
    task->_state = fnTask::TASK_DONE;
    task->_reason = fnTask::TASK_COMPLETED;
    // TODO callback
    // remove completed task
    _task_count -= 1;
    _task_map.erase(tid);
    delete task;
    return 0;
}

bool fnTaskManager::service()
{
    if (_task_count == 0)
        return true; // idle

    bool idle = true; // was service() idle?
    int result;
    fnTask *task;
    std::list <uint8_t> failed;
    std::list <uint8_t> completed;

    // update READY and RUNNING tasks
    for (auto it = _task_map.begin(); it != _task_map.end(); ++it)
    {
        task = it->second;
        switch (task->_state)
        {
        case fnTask::TASK_READY:
            idle = false;
            result = task->start();
            if (result < 0)
                // failed to start task
                failed.push_back(it->first);
            else
                task->_state = fnTask::TASK_RUNNING;
            break;

        case fnTask::TASK_RUNNING:
            idle = false;
            result = task->step();
            if (result < 0)
                // failure in task execution
                failed.push_back(it->first);
            else if (result > 0)
            {
                // task completed
                completed.push_back(it->first);
            }
            break;
        default:
            ;
        }
    }

    if (!idle)
    {
        // handle failed tasks, if any
        for (auto it = failed.begin(); it != failed.end(); ++it)
            abort_task(*it);
        // handle completed tasks, if any
        for (auto it = completed.begin(); it != completed.end(); ++it)
            complete_task(*it);
    }

    return idle;
}

#endif // !ESP_PLATFORM