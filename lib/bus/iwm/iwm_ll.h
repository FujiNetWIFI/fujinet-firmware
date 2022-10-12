#ifdef BUILD_APPLE
#ifndef IWM_LL_H
#define IWM_LL_H

#define SPI_BUFFER_LEN      6000 // should be long enough for 20.1 ms (for SoftSP) + some margin - call it 22 ms. 2051282*.022 =  45128.204 bits / 8 = 5641.0255 bytes

#endif // IWM_LL_H
#endif // BUILD_APPLE