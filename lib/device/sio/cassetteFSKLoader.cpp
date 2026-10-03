#if defined(ESP_PLATFORM) && defined(BUILD_ATARI)
#include "cassetteFSKLoader.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "../../include/debug.h"

// Core 0 carries the network stack (remote TNFS and HTTP reads); the cassette task and the RMT interrupt
// are on core 1. The stack must hold a whole file read on any backing filesystem.
#define CASSETTE_FSK_LOADER_STACK_BYTES 16384
#define CASSETTE_FSK_LOADER_PRIORITY 5
#define CASSETTE_FSK_LOADER_CORE 0
#define CASSETTE_FSK_LOADER_FAULT_PAUSE_MS 250

CassetteFSKLoader::~CassetteFSKLoader()
{
    release();
}

long CassetteFSKLoader::file_tell(void *ctx)
{
    return fnio::ftell(static_cast<CassetteFSKLoader *>(ctx)->_file);
}

int CassetteFSKLoader::file_seek(void *ctx, size_t off)
{
    return fnio::fseek(static_cast<CassetteFSKLoader *>(ctx)->_file, static_cast<long>(off), SEEK_SET);
}

size_t CassetteFSKLoader::file_read(void *ctx, uint8_t *dst, size_t n)
{
    return fnio::fread(dst, 1, n, static_cast<CassetteFSKLoader *>(ctx)->_file);
}

bool CassetteFSKLoader::file_stopping(void *ctx)
{
    return fsk_pub_load(&static_cast<CassetteFSKLoader *>(ctx)->_run.stop_req) != 0;
}

size_t CassetteFSKLoader::read_block(void *ctx, size_t off, uint8_t *dst, size_t n)
{
    CassetteFSKLoader *self = static_cast<CassetteFSKLoader *>(ctx);
    if (self->_file == nullptr)
        return 0;
    const FSKFileOps ops = {self, &file_tell, &file_seek, &file_read, &file_stopping};
    FSKReadStats stats; // per call; not kept
    return fsk_resilient_read(ops, self->_filesize, self->_suspect, stats, off, dst, n);
}

// Payload blocks live in PSRAM like the preloaded payload: the RMT refill reads them through the cache.
uint8_t *CassetteFSKLoader::alloc_block(void *, size_t n)
{
    return static_cast<uint8_t *>(heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
}

void CassetteFSKLoader::task_main(void *arg)
{
    CassetteFSKLoader *self = static_cast<CassetteFSKLoader *>(arg);
    FSKStep step;
    do
    {
        const uint32_t faults_before = self->_loader.faults;
        step = fsk_loader_step(self->_loader);
        if (step == FSKStep::progress && self->_loader.faults != faults_before)
            vTaskDelay(pdMS_TO_TICKS(CASSETTE_FSK_LOADER_FAULT_PAUSE_MS)); // already retried inside; breathe
        taskYIELD(); // a step served from a block already in the read cache does not block
    } while (step == FSKStep::progress);

    // Last touch of shared state; after this only the task's own stack is used.
    self->_running.store(false);
    vTaskDelete(nullptr);
}

success_is_true CassetteFSKLoader::spawn()
{
    _running.store(true);
    if (xTaskCreatePinnedToCore(&CassetteFSKLoader::task_main, "fskload", CASSETTE_FSK_LOADER_STACK_BYTES,
                                this, CASSETTE_FSK_LOADER_PRIORITY, nullptr,
                                CASSETTE_FSK_LOADER_CORE) != pdPASS)
    {
        _running.store(false);
        Debug_println("FSK: loader task creation failed");
        RETURN_ERROR_AS_FALSE();
    }
    RETURN_SUCCESS_AS_TRUE();
}

CassetteFSKLoaderStart CassetteFSKLoader::start(fnFile *file, size_t filesize, size_t header_offset,
                                                uint16_t first_length)
{
    release();

    const size_t blocks = fsk_loader_table_blocks(filesize, header_offset);
    // The refill callback reads this table, so it is internal RAM; the payload blocks are PSRAM.
    _table = static_cast<uint8_t **>(
        heap_caps_calloc(blocks, sizeof(uint8_t *), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    if (_table == nullptr)
    {
        Debug_printf("FSK: block table allocation failed (%u entries)\n", (unsigned)blocks);
        return CassetteFSKLoaderStart::no_memory;
    }
    _table_blocks = blocks;

    fsk_run_bind(_run, _table, _table_blocks);
    fsk_loader_init(_loader, _run, &CassetteFSKLoader::read_block, this, &CassetteFSKLoader::alloc_block,
                    this, filesize, header_offset, first_length);
    _file = file;
    _filesize = filesize;
    _suspect = false;
    _restarts = 0;

    if (spawn().is_error())
    {
        release();
        return CassetteFSKLoaderStart::no_task;
    }
    return CassetteFSKLoaderStart::started;
}

CassetteFSKLoaderRestart CassetteFSKLoader::restart()
{
    if (_table == nullptr || _file == nullptr || _restarts >= MAX_RESTARTS ||
        fsk_run_state(_run) != FSKLoaderState::failed)
        return CassetteFSKLoaderRestart::refused;

    stop(); // the failed task is gone; its cursor and the published part stay as they were
    fsk_loader_recover(_loader);
    _suspect = true; // unknown until a read completes after a real re-seek
    ++_restarts;
    if (spawn().is_error())
    {
        fsk_pub_store(&_run.state, static_cast<uint32_t>(FSKLoaderState::failed));
        return CassetteFSKLoaderRestart::refused;
    }
    return CassetteFSKLoaderRestart::restarted;
}

void CassetteFSKLoader::stop()
{
    if (_running.load())
        fsk_pub_store(&_run.stop_req, 1);
    while (_running.load())
        vTaskDelay(1);
}

void CassetteFSKLoader::release()
{
    stop();
    if (_table != nullptr)
    {
        for (size_t i = 0; i < _table_blocks; i++)
            heap_caps_free(_table[i]);
        heap_caps_free(_table);
    }
    _table = nullptr;
    _table_blocks = 0;
    _run.blocks = nullptr;
    _run.block_capacity = 0;
    _file = nullptr;
}

#endif // ESP_PLATFORM && BUILD_ATARI
