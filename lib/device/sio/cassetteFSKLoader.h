#ifndef CASSETTE_FSK_LOADER_H
#define CASSETTE_FSK_LOADER_H

// cassetteFSKLoader.h — the task that loads one `fsk ` run progressively into PSRAM (see
// ../../media/atari/casFSKLoader.h for the protocol) and the store it fills. While it runs it is the
// only reader of the file. stop() returns once it has left its last file read, so a caller that is
// about to close or replace the file calls it first.
//
// Declared, and implemented, only for the Atari build, like cassetteFSK.h.

#if defined(ESP_PLATFORM) && defined(BUILD_ATARI)

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

#include "fnio.h"
#include "global_types.h"
#include "../../media/atari/casFSKLoader.h"
#include "cassetteFSKRead.h"

enum class CassetteFSKLoaderStart : uint8_t
{
    started,
    no_memory, // the block table could not be allocated
    no_task,   // the loader task could not be created
};

enum class CassetteFSKLoaderRestart : uint8_t
{
    restarted,
    refused, // not failed, restart budget spent, or the task could not be created
};

class CassetteFSKLoader
{
public:
    CassetteFSKLoader() = default;
    CassetteFSKLoader(const CassetteFSKLoader &) = delete;
    CassetteFSKLoader &operator=(const CassetteFSKLoader &) = delete;
    ~CassetteFSKLoader();

    // Allocates the store for the run whose first chunk header is at `header_offset` and starts the task.
    CassetteFSKLoaderStart start(fnFile *file, size_t filesize, size_t header_offset,
                                 uint16_t first_length);

    // Resumes a failed run from its confirmed cursor, a bounded number of times.
    CassetteFSKLoaderRestart restart();

    // Callable from any task, and more than once: asks the task to stop and waits for it to be gone.
    // It also cancels the run, even if the task has already exited: restart() refuses until the next
    // start().
    void stop();

    // Frees the store. Only once nothing reads it any more (the RMT transmission has ended).
    void release();

    const FSKProgressiveRun &run() const { return _run; }

private:
    static constexpr uint32_t MAX_RESTARTS = 8;

    success_is_true spawn();
    void join_locked();    // _life held: ask the task to stop and wait for it, without cancelling
    void release_locked(); // _life held
    static void task_main(void *arg);
    static size_t read_block(void *ctx, size_t off, uint8_t *dst, size_t n);
    static uint8_t *alloc_block(void *ctx, size_t n);
    static long file_tell(void *ctx);
    static int file_seek(void *ctx, size_t off);
    static size_t file_read(void *ctx, uint8_t *dst, size_t n);
    static bool file_stopping(void *ctx);

    FSKProgressiveRun _run;
    FSKRunLoader _loader;
    uint8_t **_table = nullptr;
    size_t _table_blocks = 0;
    fnFile *_file = nullptr;
    size_t _filesize = 0;
    bool _suspect = false; // the file's position is unknown after a read fault; loader task only
    uint32_t _restarts = 0;
    std::atomic<bool> _running{false};

    // Serializes start, restart, stop and release, so no task is created once stop() has returned.
    // The loader task never takes it.
    std::mutex _life;
    bool _cancelled = false; // under _life: stop() was called since start()
};

#endif // ESP_PLATFORM && BUILD_ATARI

#endif // CASSETTE_FSK_LOADER_H
