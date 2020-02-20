#include "network.h"

/**
 * Parse deviceSpecs of the format
 * Nx:PROTO:PATH:PORT or
 * Nx:PROTO:PORT
 */
bool sioNetwork::parse_deviceSpec()
{
  char* p;
  char i=0;
  char d=0;

  strcpy(tmp,deviceSpec.rawData);

  p=strtok(tmp,":"); // Get Device spec

  if (p[0]!='N')
    return false;
  else
    strcpy(deviceSpec.device,p);

  while (p!=NULL)
    {
      i++;
      p=strtok(NULL,":");
      switch(i)
        {
        case 1:
          strcpy(deviceSpec.protocol,p);
          break;
        case 2:
          for (d=0;d<strlen(p);d++)
            if (!isdigit(p[d]))
              {
                strcpy(deviceSpec.path,p);
                break;
              }
          deviceSpec.port=atoi(p);
          return true;
        case 3:
          deviceSpec.port=atoi(p);
          return true;
          break;
        default:
          return false; // Too many parameters.
        }
    }
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
