#include <string>
#include "voice.h"
#include "utils.h"

using namespace std;

#define EOL 0x9B

void sioVoice::sio_sam_parameters()
{
    string s = string((char *)sioBuffer);
    vector<string> tokens = util_tokenize(s, ' ');

    for (vector<string>::iterator it = tokens.begin(); it != tokens.end(); ++it)
    {
        string t = *it;

        switch (t[0])
        {
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
            pitch.clear();
            mouth.clear();
            phonetic = false;
            speed.clear();
            throat.clear();
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
    int i = 0;

    memset(samBuffer,0,sizeof(samBuffer));
    memset(a,0,sizeof(a));

    while (sioBuffer[i] != EOL && i < 39)
    {
        i++;
    }

    sioBuffer[i] = '\0';

    // Construct parameter buffer.
    a[n++] = (char *)("sam");
    a[n++] = (char *)("-debug");

    sio_sam_parameters();

    if (sing == true)
        a[n++] = (char *)("-sing");

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

    if (!pitch.empty())
    {
        a[n++] = (char *)("-pitch");
        a[n++] = (char *)(pitch.c_str());
    }

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

    ck = sio_to_peripheral(sioBuffer, n);

    if (ck == sio_checksum(sioBuffer, n))
    {
        sio_sam();
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
    status[2] = 5;
    status[3] = 0;

    sio_to_computer(status, sizeof(status), false);
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
        sio_ack();
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
