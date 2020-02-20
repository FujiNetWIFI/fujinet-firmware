#include "network.h"

/**
 * Parse deviceSpecs of the format
 * Nx:PROTO:PATH:PORT or
 * Nx:PROTO:PORT
 */
bool sioNetwork::parse_deviceSpec()
{

}

void sioNetwork::open()
{
    if (parse_deviceSpec()==false)
    {
        
    }
}

void sioNetwork::close()
{

}

void sioNetwork::read()
{

}

void sioNetwork::write()
{

}

void sioNetwork::status()
{

}
