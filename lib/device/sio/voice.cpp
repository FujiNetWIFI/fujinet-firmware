#ifdef BUILD_ATARI

#include "voice.h"

#include <string>

#include "utils.h"

using namespace std;

#define EOL 0x9B


void sioVoice::sio_sam_presets(int pr)
{
//	DESCRIPTION          SPEED     PITCH     THROAT    MOUTH
//	SAM                   72        64        128       128
//	Elf                   72        64        110       160
//	Little Robot          92        60        190       190
//	Stuffy Guy            82        72        110       105
//	Little Old Lady       82        32        145       145
//	Extra-Terrestrial    100        64        150       200

    switch (pr)
    {
        case 0: //SAM
            speed = "72";
            pitch = "64";
            throat = "128";
            mouth = "128";
            break;
        case 1: //Elf
            speed = "72";
            pitch = "64";
            throat = "110";
            mouth = "160";
            break;
        case 2: //Little Robot
            speed = "92";
            pitch = "60";
            throat = "190";
            mouth = "190";
            break;
        case 3: //Stuffy Guy
            speed = "82";
            pitch = "72";
            throat = "110";
            mouth = "105";
            break;
        case 4: //Little Old Lady
            speed = "82";
            pitch = "32";
            throat = "145";
            mouth = "145";
            break;
        case 5: //Extra-Terrestrial
            speed = "100";
            pitch = "64";
            throat = "150";
            mouth = "200";
            break;
        default:
            break;
    }


}

void sioVoice::sio_sam_parameters()
{
    string s = string((char *)lineBuffer); // change to lineBuffer
    vector<string> tokens = util_tokenize(s, ' ');



    for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        string t = *it;

        
        switch (t[0])
        {
#ifdef ESP32S3_I2S_OUT
        case 0x01: // ^A i2sOut
            i2sOut = *(++it);
            break;
#endif            
//        case 0x02: // ^B SampleRate
//            samplerate = *(++it);
//            break;
        case 0x03: // ^C Preset
            sio_sam_presets(atoi((*(++it)).c_str()));
            break;
        case 0x07: // ^G SING
            sing = true;
            break;
        case 0x09: // ^I PITCH
            pitch = *(++it);
            break;
        case 0x0D: // ^M Mouth
            mouth = *(++it);
            break;
        case 0x10: // ^P Phonetic
            phonetic = true;
            break;
        case 0x12: // ^R RESET
            sing = false;
            phonetic = false;
            sio_sam_presets(0);
            break;
        case 0x13: // ^S Speed
            speed = *(++it);
            break;
        case 0x14: // ^T Throat
            throat = *(++it);
            break;
        default:
            if (it != tokens.begin())
                strcat((char *)samBuffer, " ");
            strcat((char *)samBuffer, t.c_str());
            break;
        }
    }
}

void sioVoice::sio_sam()
{
    int n = 0;
    char *a[16];
    // int i = 0;

    memset(samBuffer, 0, sizeof(samBuffer));
    memset(a, 0, sizeof(a));

    // Construct parameter buffer.
    a[n++] = (char *)("sam");
    a[n++] = (char *)("-debug");

    sio_sam_parameters();


//    if (!samplerate.empty())
//    {
//        a[n++] = (char *)("-samplerate");
//        a[n++] = (char *)(samplerate.c_str());
//    }

#ifdef ESP32S3_I2S_OUT
    if (!i2sOut.empty())
    {
        a[n++] = (char *)("-i2sOut");
        a[n++] = (char *)(i2sOut.c_str());
    }
#endif

    if (sing == true)
        a[n++] = (char *)("-sing");
    else 
        a[n++] = (char *)("-no-sing");

    if (!pitch.empty())
    {
        a[n++] = (char *)("-pitch");
        a[n++] = (char *)(pitch.c_str());
    }

    if (!mouth.empty())
    {
        a[n++] = (char *)("-mouth");
        a[n++] = (char *)(mouth.c_str());
    }

    if (phonetic == true)
        a[n++] = (char *)("-phonetic");
//    else
//        a[n++] = (char *)("-no-phonetic");


    if (!speed.empty())
    {
        a[n++] = (char *)("-speed");
        a[n++] = (char *)(speed.c_str());
    }

    if (!throat.empty())
    {
        a[n++] = (char *)("-throat");
        a[n++] = (char *)(throat.c_str());
    }

    a[n++] = (char *)samBuffer;
    sam(n, a);
};

void sioVoice::sio_write()
{
    // act like a printer for POC
    uint8_t n = 40;
    uint8_t ck;

    memset(sioBuffer, 0, n); // clear buffer

    ck = bus_to_peripheral(sioBuffer, n);

    if (ck == sio_checksum(sioBuffer, n))
    {
        // append sioBuffer onto lineBuffer until EOL is reached
        // move this logic to append \0 into sio_write
        uint8_t i = 0;
        while (i < 40)
        {
            if (sioBuffer[i] != EOL && buffer_idx < 121)
            {
                lineBuffer[buffer_idx] = sioBuffer[i];
                buffer_idx++;
            }
            else
            {
                lineBuffer[buffer_idx] = '\0';
                buffer_idx = 0;
                sio_sam();
                // clear lineBuffer
                memset(lineBuffer, 0, sizeof(lineBuffer));
                break;
            }
            i++;
        }
        sio_complete();
    }
    else
    {
        sio_error();
    }
}

// Status
void sioVoice::sio_status()
{
    // act like a printer for POC
    uint8_t status[4];

    status[0] = 0;
    status[1] = lastAux1;
    status[2] = 15; // set timeout > 10 seconds (SAM audio buffer)
    status[3] = 0;

    bus_to_computer(status, sizeof(status), false);
}

void sioVoice::sio_process(uint32_t commanddata, uint8_t checksum)
{
    cmdFrame.commanddata = commanddata;
    cmdFrame.checksum = checksum;

    // act like a printer for POC
    switch (cmdFrame.comnd)
    {
    case 'P': // 0x50
    case 'W': // 0x57
        sio_late_ack();
        sio_write();
        lastAux1 = cmdFrame.aux1;
        break;
    case 'S': // 0x53
        sio_ack();
        sio_status();
        break;
    default:
        sio_nak();
    }
}

#endif /* BUILD_ATARI */
