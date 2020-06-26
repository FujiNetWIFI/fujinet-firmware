#include <cstring>

#include "fnSystem.h"
#include "fnBluetooth.h"
#include "led.h"
#include "keys.h"
#include "sio.h"

KeyManager fnKeyManager;

void KeyManager::setup()
{
    fnSystem.set_pin_mode(PIN_BOOT_KEY, PINMODE_INPUT);
    fnSystem.set_pin_mode(PIN_OTHER_KEY, PINMODE_INPUT);

    // Start a new task to check the status of the buttons
    xTaskCreate(_keystate_task, "fnKeys", 4096, this, 1, nullptr);
}

bool KeyManager::keyCurrentlyPressed(eKey key)
{
    return (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW);
}

eKeyStatus KeyManager::getKeyStatus(eKey key)
{
    eKeyStatus result = eKeyStatus::RELEASED;

    if (fnSystem.digital_read(mButtonPin[key]) == DIGI_LOW)
    {
        if (mButtonActive[key] == false)
        {
            mButtonActive[key] = true;
            mButtonTimer[key] = fnSystem.millis();
        }
        if ((fnSystem.millis() - mButtonTimer[key] > LONGPRESS_TIME) && (mLongPressActive[key] == false))
        {
            mLongPressActive[key] = true;
            // long press detected
            result = eKeyStatus::LONG_PRESSED;
        }
    }
    else
    {
        if (mButtonActive[key] == true)
        {
            if (mLongPressActive[key] == true)
            {
                mLongPressActive[key] = false;
                // long press released
            }
            else
            {
                // short press released
                result = eKeyStatus::SHORT_PRESSED;
            }
            mButtonActive[key] = false;
        }
    }
    return result;
}


#ifdef DEBUG
// Dumps list of current tasks
void _debug_print_tasks()
{
    static const char * status[] = {"Running", "Ready", "Blocked", "Suspened", "Deleted"};

    uint32_t n = uxTaskGetNumberOfTasks();
    TaskStatus_t *pTasks = (TaskStatus_t *) malloc(sizeof(TaskStatus_t) * n);
    n = uxTaskGetSystemState(pTasks, n, nullptr);

    for(int i = 0; i < n; i++)
    {
        Debug_printf("T%02d %p c%c (%2d,%2d) %4dh %10dr %8s: %s\n",
            i+1,
            pTasks[i].xHandle,
            pTasks[i].xCoreID == tskNO_AFFINITY ? '_' : ('0' + pTasks[i].xCoreID),
            pTasks[i].uxBasePriority, pTasks[i].uxCurrentPriority,
            pTasks[i].usStackHighWaterMark,
            pTasks[i].ulRunTimeCounter,
            status[pTasks[i].eCurrentState],
            pTasks[i].pcTaskName);
    }
    Debug_printf("\nCPU MHz: %d\n", fnSystem.get_cpu_frequency());
}
#endif

void KeyManager::_keystate_task(void *param)
{
#ifdef BOARD_HAS_PSRAM
#define BLUETOOTH_LED eLed::LED_BT
#else
#define BLUETOOTH_LED eLed::LED_SIO
#endif
    KeyManager *pKM = (KeyManager *)param;

    while (true)
    {
        vTaskDelay(200 / portTICK_PERIOD_MS);

        // Check on the status of the BOOT_KEY and do something useful
        switch (pKM->getKeyStatus(eKey::BOOT_KEY))
        {
        case eKeyStatus::LONG_PRESSED:
            Debug_println("BOOT_KEY: LONG PRESS");
#ifdef BLUETOOTH_SUPPORT
            if (fnBtManager.isActive())
            {
                fnBtManager.stop();
                fnLedManager.set(BLUETOOTH_LED, false);
            }
            else
            {
                fnLedManager.set(BLUETOOTH_LED, true); // SIO LED always ON in Bluetooth mode
                fnBtManager.start();
            }
#endif //BLUETOOTH_SUPPORT
            break;
        case eKeyStatus::SHORT_PRESSED:
            Debug_println("BOOT_KEY: SHORT PRESS");
            fnLedManager.blink(BLUETOOTH_LED, 2); // blink to confirm a button press
// Either toggle BT baud rate or do a disk image rotation on B_KEY SHORT PRESS
#ifdef BLUETOOTH_SUPPORT
            if (fnBtManager.isActive())
                fnBtManager.toggleBaudrate();
            else
#endif
            {
                Debug_println("Send image_rotate message to SIO queue");
                sio_message_t msg;
                msg.message_id = SIOMSG_DISKSWAP;
                xQueueSend(SIO.qSioMessages, &msg ,0);
            }
            break;
        default:
            break;
        } // BOOT_KEY

        // Check on the status of the OTHER_KEY and do something useful
        switch (pKM->getKeyStatus(eKey::OTHER_KEY))
        {
        case eKeyStatus::LONG_PRESSED:
            Debug_println("OTHER_KEY: LONG PRESS");
            fnSystem.reboot();
            break;
        case eKeyStatus::SHORT_PRESSED:
            Debug_println("OTHER_KEY: SHORT PRESS");
            #ifdef DEBUG
            _debug_print_tasks();
            #endif
            break;
        default:
            break;
        } // OTHER KEY

    }
}
