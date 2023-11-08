#ifndef _FN_TASK_H
#define _FN_TASK_H

#include <stdint.h>

class fnTaskManager;

class fnTask
{
public:
    enum task_state
    {
        TASK_READY = 0,
        TASK_RUNNING,
        TASK_PAUSED,
        TASK_DONE
    };

    enum done_reason
    {
        TASK_COMPLETED = 0,
        TASK_ABORTED
    };

    fnTask();
    virtual ~fnTask() = 0;

    // state, progress and results
    task_state get_state() {return _state;};
    done_reason get_done_reason() {return _reason;};
    virtual int get_progress() {return 0;};         // optional
    virtual void * get_result() {return nullptr;};  // optional

protected:
    // task state management
    // READY -> RUNNING
    virtual int start() = 0;                        // mandatory, must be implemented in sub-class
    // RUNNING -> PAUSED
    virtual int pause() {return 0;};                // optional
    // PAUSED -> RUNNING
    virtual int resume() {return 0;};               // optional
    // -> DONE/ABORTED, result is not available
    virtual int abort() {return 0;};                // optional
    // do some work
    virtual int step() = 0;                         // mandatory, must be implemented in sub-class

    friend fnTaskManager;

    uint8_t _id;                                    // task ID 1..255, 0 is invalid / not yet assigned ID
    task_state _state;
    done_reason _reason;
    void (*_callback)(fnTask *t, task_state new_state);
};

class fnTestTask : public fnTask
{
public:
    fnTestTask(int count);
    virtual ~fnTestTask() override;
    virtual int get_progress() override;
    virtual void * get_result() override;

protected:
    virtual int start() override;
    virtual int pause() override;
    virtual int resume() override;
    virtual int abort() override;
    virtual int step() override;

private:
    int _count;
    int _i;
};

#endif // _FN_TASK_H
