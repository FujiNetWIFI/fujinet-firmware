#include "Console.h"

#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "Commands/CoreCommands.h"
#include "Commands/SystemCommands.h"
#include "Commands/NetworkCommands.h"
#include "Commands/VFSCommands.h"
#include "Commands/GPIOCommands.h"
#include "Commands/XFERCommands.h"
#include "driver/uart.h"
#include "esp_vfs_dev.h"
#include "linenoise/linenoise.h"
#include "Helpers/PWDHelpers.h"
#include "Helpers/InputParser.h"

#include "../../include/debug.h"
#include "string_utils.h"

using namespace ESP32Console::Commands;

namespace ESP32Console
{
    void Console::registerCoreCommands()
    {
        registerCommand(getClearCommand());
        registerCommand(getHistoryCommand());
        registerCommand(getEchoCommand());
        registerCommand(getSetMultilineCommand());
        registerCommand(getEnvCommand());
        registerCommand(getDeclareCommand());
#ifdef ENABLE_DISPLAY
        registerCommand(getLEDCommand());
#endif
    }

    void Console::registerSystemCommands()
    {
        registerCommand(getSysInfoCommand());
        registerCommand(getRestartCommand());
        registerCommand(getMemInfoCommand());
        registerCommand(getTaskInfoCommand());
        registerCommand(getDateCommand());
    }

    void ESP32Console::Console::registerNetworkCommands()
    {
        registerCommand(getPingCommand());
        registerCommand(getIpconfigCommand());
        registerCommand(getScanCommand());
        registerCommand(getConnectCommand());
        registerCommand(getIMPROVCommand());
    }

    void Console::registerVFSCommands()
    {
        registerCommand(getCatCommand());
        registerCommand(getCDCommand());
        registerCommand(getPWDCommand());
        registerCommand(getLsCommand());
        registerCommand(getMvCommand());
        registerCommand(getCPCommand());
        registerCommand(getRMCommand());
        registerCommand(getRMDirCommand());
        registerCommand(getMKDirCommand());
        registerCommand(getEditCommand());
        registerCommand(getMountCommand());
        registerCommand(getWgetCommand());
    }

    void Console::registerGPIOCommands()
    {
        registerCommand(getPinModeCommand());
        registerCommand(getDigitalReadCommand());
        registerCommand(getDigitalWriteCommand());
        registerCommand(getAnalogReadCommand());
    }

    void Console::registerXFERCommands()
    {
        registerCommand(getRXCommand());
        registerCommand(getTXCommand());
    }


    void Console::beginCommon()
    {
        /* Tell linenoise where to get command completions and hints */
        linenoiseSetCompletionCallback(&esp_console_get_completion);
        linenoiseSetHintsCallback((linenoiseHintsCallback *)&esp_console_get_hint);

        /* Set command history size */
        linenoiseHistorySetMaxLen(max_history_len_);

        /* Set command maximum length */
        linenoiseSetMaxLineLen(max_cmdline_len_);

        // Load history if defined
        if (history_save_path_)
        {
            linenoiseHistoryLoad(history_save_path_);
        }

        // Register core commands like echo
        esp_console_register_help_command();
        registerCoreCommands();
    }

    void Console::begin(int baud, int rxPin, int txPin, uint8_t channel)
    {
        Debug_printv("Initialize console");

        if (channel >= SOC_UART_NUM)
        {
            Debug_printv("Serial number is invalid, please use numers from 0 to %u", SOC_UART_NUM - 1);
            return;
        }

        this->uart_channel_ = channel;

        //Reinit the UART driver if the channel was already in use
        if (uart_is_driver_installed((uart_port_t)channel)) {
            uart_driver_delete((uart_port_t)channel);
        }

        /* Drain stdout before reconfiguring it */
        fflush(stdout);
        fsync(fileno(stdout));

        /* Disable buffering on stdin */
        setvbuf(stdin, NULL, _IONBF, 0);

        /* Minicom, screen, idf_monitor send CR when ENTER key is pressed */
        esp_vfs_dev_uart_port_set_rx_line_endings(channel, ESP_LINE_ENDINGS_CR);
        /* Move the caret to the beginning of the next line on '\n' */
        esp_vfs_dev_uart_port_set_tx_line_endings(channel, ESP_LINE_ENDINGS_CRLF);

        /* Enable non-blocking mode on stdin and stdout */
        fcntl(fileno(stdout), F_SETFL, 0);
        fcntl(fileno(stdin), F_SETFL, 0);


        /* Configure UART. Note that REF_TICK is used so that the baud rate remains
         * correct while APB frequency is changing in light sleep mode.
         */
        const uart_config_t uart_config = {
            .baud_rate = baud,
            .data_bits = UART_DATA_8_BITS,
            .parity = UART_PARITY_DISABLE,
            .stop_bits = UART_STOP_BITS_1,
            .source_clk = UART_SCLK_DEFAULT,
        };
    

        ESP_ERROR_CHECK(uart_param_config((uart_port_t)channel, &uart_config));

        // Set the correct pins for the UART of needed
        if (rxPin > 0 || txPin > 0) {
            if (rxPin < 0 || txPin < 0) {
                Debug_printv("Both rxPin and txPin has to be passed!");
            }
            uart_set_pin((uart_port_t)channel, txPin, rxPin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
        }

        /* Install UART driver for interrupt-driven reads and writes */
        ESP_ERROR_CHECK(uart_driver_install((uart_port_t)channel, 256, 0, 0, NULL, 0));

        /* Tell VFS to use UART driver */
        esp_vfs_dev_uart_use_driver((uart_port_t)channel);

        esp_console_config_t console_config = {
            .max_cmdline_length = max_cmdline_len_,
            .max_cmdline_args = max_cmdline_args_,
            .hint_color = 333333
        };

        ESP_ERROR_CHECK(esp_console_init(&console_config));

        beginCommon();

        // Start REPL task
        if (xTaskCreatePinnedToCore(&Console::repl_task, "console_repl", task_stack_size_, this, task_priority_, &task_, 0) != pdTRUE)
        {
            Debug_printv("Could not start REPL task!");
        }
    }

    static void resetAfterCommands()
    {
        //Reset all global states a command could change

        //Reset getopt parameters
        optind = 0;
    }

    void Console::repl_task(void *args)
    {
        Console const &console = *(static_cast<Console *>(args));

        /* Change standard input and output of the task if the requested UART is
         * NOT the default one. This block will replace stdin, stdout and stderr.
         * We have to do this in the repl task (not in the begin, as these settings are only valid for the current task)
         */
        // if (console.uart_channel_ != CONFIG_ESP_CONSOLE_UART_NUM)
        // {
        //     char path[13] = {0};
        //     snprintf(path, 13, "/dev/uart/%1d", console.uart_channel_);

        //     stdin = fopen(path, "r");
        //     stdout = fopen(path, "w");
        //     stderr = stdout;
        // }

        //setvbuf(stdin, NULL, _IONBF, 0);

        /* This message shall be printed here and not earlier as the stdout
         * has just been set above. */
        // printf("\r\n"
        //        "Type 'help' to get the list of commands.\r\n"
        //        "Use UP/DOWN arrows to navigate through command history.\r\n"
        //        "Press TAB when typing command name to auto-complete.\r\n");

        // Probe terminal status
        int probe_status = linenoiseProbe();
        if (probe_status)
        {
            linenoiseSetDumbMode(1);
        }

        // if (linenoiseIsDumbMode())
        // {
        //     printf("\r\n"
        //            "Your terminal application does not support escape sequences.\n\n"
        //            "Line editing and history features are disabled.\n\n"
        //            "On Windows, try using Putty instead.\r\n");
        // }

        linenoiseSetMaxLineLen(console.max_cmdline_len_);
        while (true)
        {
            std::string prompt = console.prompt_;

            // Insert current PWD into prompt if needed
            mstr::replaceAll(prompt, "%pwd%", console_getpwd());

            char *line = linenoise(prompt.c_str());
            if (line == NULL)
            {
                Debug_printv("empty line");
                /* Ignore empty lines */
                continue;
            }

            //Debug_printv("Line received from linenoise: [%s]\n", line);

            // /* Add the command to the history */
            // linenoiseHistoryAdd(line);
            
            // /* Save command history to filesystem */
            // if (console.history_save_path_)
            // {
            //     linenoiseHistorySave(console.history_save_path_);
            // }

            //Interpolate the input line
            std::string interpolated_line = interpolateLine(line);
            //Debug_printv("Interpolated line: [%s]\n", interpolated_line.c_str());

            // Flush trailing CR
            uart_flush((uart_port_t)CONSOLE_UART);

            /* Try to run the command */
            int ret;
            esp_err_t err = esp_console_run(interpolated_line.c_str(), &ret);

            //Reset global state
            resetAfterCommands();

            if (err == ESP_ERR_NOT_FOUND)
            {
                printf("Unrecognized command\n");
            }
            else if (err == ESP_ERR_INVALID_ARG)
            {
                // command was empty
            }
            else if (err == ESP_OK && ret != ESP_OK)
            {
                // printf("Command returned non-zero error code: 0x%x (%s)\n", ret, esp_err_to_name(ret));
            }
            else if (err != ESP_OK)
            {
                printf("Internal error: %s\n", esp_err_to_name(err));
            }
            /* linenoise allocates line buffer on the heap, so need to free it */
            linenoiseFree(line);
        }
        //Debug_printv("REPL task ended");
        vTaskDelete(NULL);
        esp_console_deinit();
    }

    void Console::end()
    {
    }
};