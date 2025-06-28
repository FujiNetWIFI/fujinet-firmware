
#include "samlib.h"

#ifdef ESP_PLATFORM
  #include <freertos/FreeRTOS.h>
  #include <freertos/timers.h>
  #include <driver/gpio.h>
  #ifndef CONFIG_IDF_TARGET_ESP32S3
  #include <driver/dac_oneshot.h>
  #endif
#else
  #define MA_NO_DECODING
  #define MA_NO_ENCODING
  #include "miniaudio.c"
  #include "compat_string.h"
#endif

#include "fnSystem.h"

#ifdef __cplusplus
extern char input[256];
extern char *buffer;
#endif

int debug = 0;
const uint32_t sample_rate = 22050;//110000l;


#ifdef CONFIG_IDF_TARGET_ESP32S3

  //PDM NEW API
#include <driver/i2s_pdm.h>

// ESP32S3_I2S_OUT
#ifdef ESP32S3_I2S_OUT
  //i2s Output
  #include <driver/i2s_std.h>
  bool i2sOut = true;
#endif


void SendI2S (i2s_chan_handle_t tx_handle, char *s, size_t n)
{
// number of frames to try and send at once (a frame is a left and right sample)
        const size_t NUM_FRAMES_TO_SEND=1023;//1024;
        int16_t * m_tmp_frames = (int16_t *)malloc(sizeof(int16_t) * NUM_FRAMES_TO_SEND);
        int16_t iTmp;
        esp_err_t res;
        size_t bytes_written;
        int samples_to_send;
        int sample_index = 0;

        while (sample_index < n)
        {
          samples_to_send = 0;

          for (int i = 0; i < NUM_FRAMES_TO_SEND && sample_index < n; i++)
          {
            // shift up to 16 bit samples
            iTmp = ((int16_t) (s[sample_index])) << 8;
            m_tmp_frames[i] = iTmp;
            samples_to_send++;
            sample_index++;
          }
          // write data to the i2s peripheral
          bytes_written = 0;
          res = i2s_channel_write(tx_handle, m_tmp_frames,  samples_to_send * sizeof(int16_t), &bytes_written, 1000 / portTICK_PERIOD_MS);
          if (res != ESP_OK)
          {
            printf ("Error sending audio data: %d", res);
          }
          printf ("i2s write : Send %d Bytes of %d/%d Samples \n", bytes_written, samples_to_send, n);
        }
        
        free(m_tmp_frames);

}


#endif

#ifndef ESP_PLATFORM

void WriteWav(char *filename, char *buffer, int bufferlength)
{
    FILE *file = fopen(filename, "wb");
    if (file == NULL)
        return;
    //RIFF header
    fwrite("RIFF", 4, 1, file);
    unsigned int filesize = bufferlength + 12 + 16 + 8 - 8;
    fwrite(&filesize, 4, 1, file);
    fwrite("WAVE", 4, 1, file);

    //format chunk
    fwrite("fmt ", 4, 1, file);
    unsigned int fmtlength = 16;
    fwrite(&fmtlength, 4, 1, file);
    unsigned short int format = 1; //PCM
    fwrite(&format, 2, 1, file);
    unsigned short int channels = 1;
    fwrite(&channels, 2, 1, file);
    unsigned int samplerate = 22050;
    fwrite(&samplerate, 4, 1, file);
    fwrite(&samplerate, 4, 1, file); // bytes/second
    unsigned short int blockalign = 1;
    fwrite(&blockalign, 2, 1, file);
    unsigned short int bitspersample = 8;
    fwrite(&bitspersample, 2, 1, file);

    //data chunk
    fwrite("data", 4, 1, file);
    fwrite(&bufferlength, 4, 1, file);
    fwrite(buffer, bufferlength, 1, file);

    fclose(file);
}
#endif // NOT ESP_PLATFORM

void PrintUsage()
{
    /*
    printf("\r\n");
    printf("Usage: sam [options] Word1 Word2 ....\r\n");
    printf("options\r\n");
    printf("    -phonetic         enters phonetic mode. (see below)\r\n");
    printf("    -pitch number        set pitch value (default=64)\r\n");
    printf("    -speed number        set speed value (default=72)\r\n");
    printf("    -throat number        set throat value (default=128)\r\n");
    printf("    -mouth number        set mouth value (default=128)\r\n");
    printf("    -wav filename        output to wav instead of libsdl\r\n");
    printf("    -sing            special treatment of pitch\r\n");
    printf("    -debug            print additional debug messages\r\n");
    printf("\r\n");

    printf("     VOWELS                            VOICED CONSONANTS    \r\n");
    printf("IY           f(ee)t                    R        red        \r\n");
    printf("IH           p(i)n                     L        allow        \r\n");
    printf("EH           beg                       W        away        \r\n");
    printf("AE           Sam                       W        whale        \r\n");
    printf("AA           pot                       Y        you        \r\n");
    printf("AH           b(u)dget                  M        Sam        \r\n");
    printf("AO           t(al)k                    N        man        \r\n");
    printf("OH           cone                      NX       so(ng)        \r\n");
    printf("UH           book                      B        bad        \r\n");
    printf("UX           l(oo)t                    D        dog        \r\n");
    printf("ER           bird                      G        again        \r\n");
    printf("AX           gall(o)n                  J        judge        \r\n");
    printf("IX           dig(i)t                   Z        zoo        \r\n");
    printf("                       ZH       plea(s)ure    \r\n");
    printf("   DIPHTHONGS                          V        seven        \r\n");
    printf("EY           m(a)de                    DH       (th)en        \r\n");
    printf("AY           h(igh)                        \r\n");
    printf("OY           boy                        \r\n");
    printf("AW           h(ow)                     UNVOICED CONSONANTS    \r\n");
    printf("OW           slow                      S         Sam        \r\n");
    printf("UW           crew                      Sh        fish        \r\n");
    printf("                                       F         fish        \r\n");
    printf("                                       TH        thin        \r\n");
    printf(" SPECIAL PHONEMES                      P         poke        \r\n");
    printf("UL           sett(le) (=AXL)           T         talk        \r\n");
    printf("UM           astron(omy) (=AXM)        K         cake        \r\n");
    printf("UN           functi(on) (=AXN)         CH        speech        \r\n");
    printf("Q            kitt-en (glottal stop)    /H        a(h)ead    \r\n");
    */
}

#ifdef USESDL

int pos = 0;
void MixAudio(void *unused, Uint8 *stream, int len)
{
    int bufferpos = GetBufferLength();
    char *buffer = GetBuffer();
    int i;
    if (pos >= bufferpos)
        return;
    if ((bufferpos - pos) < len)
        len = (bufferpos - pos);
    for (i = 0; i < len; i++)
    {
        stream[i] = buffer[pos];
        pos++;
    }
}

void OutputSound()
{
    int bufferpos = GetBufferLength();
    bufferpos /= 50;
    SDL_AudioSpec fmt;

    fmt.freq = 22050;
    fmt.format = AUDIO_U8;
    fmt.channels = 1;
    fmt.samples = 2048;
    fmt.callback = MixAudio;
    fmt.userdata = NULL;

    /* Open the audio device and start playing sound! */
    if (SDL_OpenAudio(&fmt, NULL) < 0)
    {
        printf("Unable to open audio: %s\r\n", SDL_GetError());
        exit(1);
    }
    SDL_PauseAudio(0);
    //SDL_Delay((bufferpos)/7);

    while (pos < bufferpos)
    {
        SDL_Delay(100);
    }

    SDL_CloseAudio();
}

#else //Not def USESDL

#ifdef ESP_PLATFORM
void OutputSound()
{
#ifndef CONFIG_IDF_TARGET_ESP32S3
    int n = GetBufferLength() / 50;
    char *s = GetBuffer();

    dac_oneshot_handle_t dac_handle;
    dac_oneshot_config_t config = {
      .chan_id = DAC_CHAN_0
    };

    if (dac_oneshot_new_channel(&config, &dac_handle) == ESP_OK) {
        for (int i = 0; i < n; i++) {
            dac_oneshot_output_voltage(dac_handle, (uint8_t)s[i]); // ensure unsigned
            fnSystem.delay_microseconds(40);
        }
        dac_oneshot_del_channel(dac_handle);
    }
#else //Defined CONFIG_IDF_TARGET_ESP32S3
//SampleRate = 22050
//8 Bits
    int n = GetBufferLength() / 50;
    char *s = GetBuffer();
    //PDMOutput *audioOutput = NULL;



//#ifdef ESP32S3_I2S_OUT
//    if (!i2sOut) {
//#endif

//PDM always but I2D only if defined ESP32S3_I2S_OUT and i2sOut is true (can change with print #1;"CTRL-A X") X : 0 Disable, 1 Enable. 

//New API
//Init/Config
        i2s_chan_handle_t tx_handle;
        /* Allocate an I2S tx channel */
        i2s_chan_config_t chan_cfg;
        chan_cfg.id = I2S_NUM_0;
        chan_cfg.role = I2S_ROLE_MASTER;
        chan_cfg.dma_desc_num = 4; //6
        chan_cfg.dma_frame_num = 1024; //240
        chan_cfg.auto_clear = false;
        i2s_new_channel(&chan_cfg, &tx_handle, NULL);

        /* Init the channel into PDM TX mode */
        i2s_pdm_tx_config_t pdm_tx_cfg;
        pdm_tx_cfg.clk_cfg.sample_rate_hz = sample_rate;
        pdm_tx_cfg.clk_cfg.clk_src = I2S_CLK_SRC_DEFAULT;
        pdm_tx_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
        pdm_tx_cfg.clk_cfg.up_sample_fp = 960;
        pdm_tx_cfg.clk_cfg.up_sample_fs = 480;

        pdm_tx_cfg.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
        pdm_tx_cfg.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO;
        pdm_tx_cfg.slot_cfg.slot_mode = I2S_SLOT_MODE_MONO; //I2S_SLOT_MODE_STEREO
        pdm_tx_cfg.slot_cfg.sd_prescale = 0;
        pdm_tx_cfg.slot_cfg.sd_scale = I2S_PDM_SIG_SCALING_MUL_1;
        pdm_tx_cfg.slot_cfg.hp_scale = I2S_PDM_SIG_SCALING_MUL_1; //I2S_PDM_SIG_SCALING_DIV_2
        pdm_tx_cfg.slot_cfg.lp_scale = I2S_PDM_SIG_SCALING_MUL_1;
        pdm_tx_cfg.slot_cfg.sinc_scale = I2S_PDM_SIG_SCALING_MUL_1;
        pdm_tx_cfg.slot_cfg.line_mode = I2S_PDM_TX_ONE_LINE_DAC; //I2S_PDM_TX_ONE_LINE_CODEC
        pdm_tx_cfg.slot_cfg.hp_en = true;
        pdm_tx_cfg.slot_cfg.hp_cut_off_freq_hz = 49; // 35.5
        pdm_tx_cfg.slot_cfg.sd_dither = 0;
        pdm_tx_cfg.slot_cfg.sd_dither2 = 1;

        pdm_tx_cfg.gpio_cfg.clk = GPIO_NUM_NC; //(gpio_num_t) I2S_PIN_NO_CHANGE; //GPIO_NUM_5
        pdm_tx_cfg.gpio_cfg.dout = PIN_DAC1; //GPIO_NUM_18
        pdm_tx_cfg.gpio_cfg.invert_flags.clk_inv = false;

        i2s_channel_init_pdm_tx_mode(tx_handle, &pdm_tx_cfg);
        i2s_channel_enable(tx_handle);

        SendI2S (tx_handle, s, n);

        /* Have to stop the channel before deleting it */
        i2s_channel_disable(tx_handle);
        /* If the handle is not needed any more, delete it to release the channel resources */
        i2s_del_channel(tx_handle);
        
//#ifdef ESP32S3_I2S_OUT
//    }
//    else //i2sOut : It need 3 Pins 

#ifdef ESP32S3_I2S_OUT
    if (i2sOut) 
    {
        i2s_chan_handle_t tx_handle;
        /* Get the default channel configuration by helper macro.
        * This helper macro is defined in 'i2s_common.h' and shared by all the i2s communication mode.
        * It can help to specify the I2S role, and port id */
        i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_AUTO, I2S_ROLE_MASTER);
        /* Allocate a new tx channel and get the handle of this channel */
        i2s_new_channel(&chan_cfg, &tx_handle, NULL);

        /* Setting the configurations, the slot configuration and clock configuration can be generated by the macros
        * These two helper macros is defined in 'i2s_std.h' which can only be used in STD mode.
        * They can help to specify the slot and clock configurations for initialization or updating */
        i2s_std_config_t std_cfg = {
            .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate),
            .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
            .gpio_cfg = {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = PIN_I2S_SCK,//sCLK
                .ws = PIN_I2S_WS,   //L/R
                .dout = PIN_I2S_SDO,//Data
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {
                    .mclk_inv = false,
                    .bclk_inv = false,
                    .ws_inv = false,
                },
            },
        };
        /* Initialize the channel */
        i2s_channel_init_std_mode(tx_handle, &std_cfg);

        /* Before write data, start the tx channel first */
        i2s_channel_enable(tx_handle);

        //i2s_channel_write(tx_handle, src_buf, bytes_to_write, bytes_written, ticks_to_wait);
        //i2s_channel_write(tx_handle, s, n, &bytes_written, 1000 / portTICK_PERIOD_MS);
        SendI2S (tx_handle, s, n);

        /* Have to stop the channel before deleting it */
        i2s_channel_disable(tx_handle);
        /* If the handle is not needed any more, delete it to release the channel resources */
        i2s_del_channel(tx_handle);

// /I2S_STD

    }
#endif    //ESP32S3_I2S_OUT

#endif //CONFIG_IDF_TARGET_ESP32S3
}

// end of ESP_PLATFORM
#else
// !ESP_PLATFORM

static int pos = 0;
void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    int bufferpos = GetBufferLength() / 50;
    char *buffer = GetBuffer();
    int i;
    bool *done_ptr = static_cast<bool *>(pDevice->pUserData);
    if (pos >= bufferpos)
    {
        *done_ptr = true;
        return;
    }
    if ((bufferpos - pos) < frameCount)
        frameCount = (bufferpos - pos);
    for (i = 0; i < frameCount; i++)
    {
        ((char *)pOutput)[i] = buffer[pos];
        pos++;
    }
}

void OutputSound()
{
    pos = 0;
    bool done = false;
    ma_device_config config  = ma_device_config_init(ma_device_type_playback);
    config.playback.format   = ma_format_u8;    // Set to ma_format_unknown to use the device's native format.
    config.playback.channels = 1;               // Set to 0 to use the device's native channel count.
    config.sampleRate        = sample_rate;     // Set to 0 to use the device's native sample rate.
    config.dataCallback      = data_callback;   // This function will be called when miniaudio needs more data.
    config.pUserData         = &done;           // Can be accessed from the device object (device.pUserData).

    ma_device device;
    if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
        return;  // Failed to initialize the device.
    }

    ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.

    // Do something here.
    while (!done) {
        fnSystem.delay(100);
    }

    ma_device_uninit(&device);
}

    // end of !ESP_PLATFORM
#endif

#endif //USESDL

int sam(int argc, char **argv)
{
    int i;
    int phonetic = 0;

#ifndef ESP_PLATFORM
    char *wavfilename = NULL;
#endif

    for (i = 0; i < 256; i++)
        input[i] = 0;

    if (argc <= 1)
    {
        PrintUsage();
        return 1;
    }

    i = 1;
    while (i < argc)
    {
        if (argv[i][0] != '-')
        {
            strlcat(input, argv[i], 255);
            strlcat(input, " ", 255);
        }
        else
        {
        
            if (strcmp(&argv[i][1], "wav") == 0)
            {
#ifndef ESP_PLATFORM
                wavfilename = argv[i + 1];
#endif
                i++;
            }
            else

            if (strcmp(&argv[i][1], "sing") == 0)
            {
                EnableSingmode();
            }
            else if (strcmp(&argv[i][1], "no-sing") == 0)
            {
                DisableSingmode();
            }
//            else if (strcmp(&argv[i][1], "samplerate") == 0)
//            {
//                sample_rate = (uint32_t) atol(argv[++i]);
//            }
            else if (strcmp(&argv[i][1], "phonetic") == 0)
            {
                phonetic = 1;
            }
//            else if (strcmp(&argv[i][1], "no-phonetic") == 0)
//            {
//                phonetic = 0;
//            }
            else if (strcmp(&argv[i][1], "debug") == 0)
            {
                debug = 1;
            }
            else if (strcmp(&argv[i][1], "pitch") == 0)
            {
                SetPitch(atoi(argv[i + 1]));
                i++;
            }
            else if (strcmp(&argv[i][1], "speed") == 0)
            {
                SetSpeed(atoi(argv[i + 1]));
                i++;
            }
            else if (strcmp(&argv[i][1], "mouth") == 0)
            {
                SetMouth(atoi(argv[i + 1]));
                i++;
            }
            else if (strcmp(&argv[i][1], "throat") == 0)
            {
                SetThroat(atoi(argv[i + 1]));
                i++;
            }
#ifdef ESP32S3_I2S_OUT
            else if (strcmp(&argv[i][1], "i2sOut") == 0)
            {
                i2sOut = (atoi(argv[++i])!=0);
            }
#endif
            else
            {
                PrintUsage();
                return 1;
            }
        }

        i++;
    } //while

    // printf("arg parsing done\r\n");

    for (i = 0; input[i] != 0; i++)
        input[i] = toupper((int)input[i]);

    if (debug)
    {
        if (phonetic)
            printf("phonetic input: %s\r\n", input);
        else
            printf("text input: %s\r\n", input);
    }

    if (!phonetic)
    {
        strlcat(input, "[", sizeof(input) - strlen(input));
        // printf("TextToPhonemes\r\n");
        if (!TextToPhonemes((unsigned char *)input))
            return 1;
        if (debug)
            printf("phonetic input: %s\r\n", input);
    }
    else
        strlcat(input, "\x9b", sizeof(input) - strlen(input));

        // printf("done phonetic processing\r\n");

#ifdef USESDL
    if (SDL_Init(SDL_INIT_AUDIO) < 0)
    {
        printf("Unable to init SDL: %s\r\n", SDL_GetError());
        exit(1);
    }
    atexit(SDL_Quit);
#endif

#ifndef __cplusplus
    SetInput(input);
#endif

    // printf("right before SAMMain");

    if (!SAMMain()) // buffer is allocated in SAMMain, used by OutputSound and WriteWav
    {
        PrintUsage();
        return 1;
    }
    // printf("right after SAMMain");

// apc: any use of WriteWav on fujinet-pc?
// #ifndef ESP_PLATFORM
    
//     if (wavfilename != NULL)
//         WriteWav(wavfilename, GetBuffer(), GetBufferLength() / 50);
//     else
// #endif // ESP_PLATFORM
        OutputSound();

    FreeBuffer();
    return 0;
}
