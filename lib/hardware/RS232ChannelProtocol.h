#ifndef RS232CHANNELPROTOCOL_H
#define RS232CHANNELPROTOCOL_H

class RS232ChannelProtocol
{
public:
    virtual uint32_t getBaudrate() = 0;
    virtual void setBaudrate(uint32_t baud) = 0;

    virtual bool getDTR() = 0;
    virtual void setDSR(bool state) = 0;
    virtual bool getRTS() = 0;
    virtual void setCTS(bool state) = 0;
    virtual void setDCD(bool state) = 0;
    virtual bool getDCD() = 0;
    virtual void setRI(bool state) = 0;
    virtual bool getRI() = 0;
};

#endif /* RS232CHANNELPROTOCOL_H */
