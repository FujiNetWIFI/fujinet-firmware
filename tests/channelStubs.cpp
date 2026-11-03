// Link-time stand-ins for the serial channels a bus header pulls in, for tests
// that never open one.

#include "TTYChannel.h"
#include "COMChannel.h"

#ifdef ITS_A_UNIX_SYSTEM_I_KNOW_THIS
void TTYChannel::updateFIFO() {}
size_t TTYChannel::dataOut(const void *, size_t) { return 0; }
void TTYChannel::end() {}
void TTYChannel::flushOutput() {}
void TTYChannel::setBaudrate(uint32_t) {}
bool TTYChannel::getDTR() { return false; }
void TTYChannel::setDSR(bool) {}
bool TTYChannel::getRTS() { return false; }
void TTYChannel::setCTS(bool) {}
bool TTYChannel::getDCD() { return false; }
bool TTYChannel::getRI() { return false; }
#endif /* ITS_A_UNIX_SYSTEM_I_KNOW_THIS */

#ifdef HELLO_IM_A_PC
void COMChannel::updateFIFO() {}
size_t COMChannel::dataOut(const void *, size_t) { return 0; }
void COMChannel::end() {}
void COMChannel::flushOutput() {}
void COMChannel::setBaudrate(uint32_t) {}
bool COMChannel::getDTR() { return false; }
void COMChannel::setDSR(bool) {}
bool COMChannel::getRTS() { return false; }
void COMChannel::setCTS(bool) {}
bool COMChannel::getDCD() { return false; }
bool COMChannel::getRI() { return false; }
#endif /* HELLO_IM_A_PC */
