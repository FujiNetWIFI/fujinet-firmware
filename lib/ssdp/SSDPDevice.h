// SSDPDevice.h
// based on https://github.com/esp8266/Arduino/issues/2283 
// https://github.com/esp8266/Arduino/files/980894/SSDPDevice.zip
// by Pawel Dino
//
// Ported to ESP-IDF by Jaime Idolpx (https://github.com/idolpx)
//
// http://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.0.pdf
//

// // Simple Service Discovery Protocol setup
// server.on("/device.xml", HTTP_GET, [](AsyncWebServerRequest *request){
//     request->send(200, "text/xml", SSDPDevice.schema());
// });
// SSDPDevice.setName( "FujiNet (C) 2022" );
// SSDPDevice.setDeviceType("urn:schemas-upnp-org:device:ManageableDevice:1");
// SSDPDevice.setSchemaURL("device.xml");
// SSDPDevice.setSerialNumber(ESP.getEfuseMac());
// SSDPDevice.setURL("http://" + fujinet.hostname + ".local");
// SSDPDevice.setModelName( "fujinet" );
// SSDPDevice.setModelNumber("1.6");
// SSDPDevice.setManufacturer("FujiNet");
// SSDPDevice.setManufacturerURL("https://fujinet.online/");
//


#ifndef _SSDPDEVICE_h
#define _SSDPDEVICE_h

#include <string>

#include "IPAddress.h"

#include "fnWiFi.h"
#include "fnUDP.h"


#define pip41(ipaddr) ((u16_t)(((u8_t*)(ipaddr))[0]))
#define pip42(ipaddr) ((u16_t)(((u8_t*)(ipaddr))[1]))
#define pip43(ipaddr) ((u16_t)(((u8_t*)(ipaddr))[2]))
#define pip44(ipaddr) ((u16_t)(((u8_t*)(ipaddr))[3]))

#define LIP2STR(ipaddr) pip41(ipaddr), \
    pip42(ipaddr), \
    pip43(ipaddr), \
    pip44(ipaddr)


#define SSDP_INTERVAL     1200
#define SSDP_PORT         1900
//#define SSDP_METHOD_SIZE  10
//#define SSDP_URI_SIZE     2
//#define SSDP_BUFFER_SIZE  64
#define SSDP_MULTICAST_TTL 2

#define SSDP_QUEUE_SIZE 21

static const IPAddress SSDP_MULTICAST_ADDR(239, 255, 255, 250);

#define SSDP_UUID_SIZE              37
#define SSDP_SCHEMA_URL_SIZE        64
#define SSDP_DEVICE_TYPE_SIZE       64
#define SSDP_FRIENDLY_NAME_SIZE     64
#define SSDP_SERIAL_NUMBER_SIZE     32
#define SSDP_PRESENTATION_URL_SIZE  128
#define SSDP_MODEL_NAME_SIZE        64
#define SSDP_MODEL_URL_SIZE         128
#define SSDP_MODEL_VERSION_SIZE     32
#define SSDP_MANUFACTURER_SIZE      64
#define SSDP_MANUFACTURER_URL_SIZE  128

static const char* SSDP_RESPONSE_TEMPLATE =
	"HTTP/1.1 200 OK\r\n"
	"EXT:\r\n";

static const char* SSDP_NOTIFY_ALIVE_TEMPLATE =
	"NOTIFY * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"NTS: ssdp:alive\r\n";

static const char* SSDP_NOTIFY_UPDATE_TEMPLATE =
	"NOTIFY * HTTP/1.1\r\n"
	"HOST: 239.255.255.250:1900\r\n"
	"NTS: ssdp:update\r\n";

static const char* SSDP_PACKET_TEMPLATE =
	"%s" // _ssdp_response_template / _ssdp_notify_template
	"CACHE-CONTROL: max-age=%u\r\n" // SSDP_INTERVAL
	"SERVER: UPNP/1.1 %s/%s\r\n" // m_modelName, m_modelNumber
	"USN: %s%s%s\r\n" // m_uuid
	"%s: %s\r\n"  // "NT" or "ST", m_deviceType
	"LOCATION: http://%u.%u.%u.%u:%u/%s\r\n" // WiFi.localIP(), m_port, m_schemaURL
	"\r\n";

// static const char* SSDP_SCHEMA_TEMPLATE =
// 	"HTTP/1.1 200 OK\r\n"
// 	"Content-Type: text/xml\r\n"
// 	"Connection: close\r\n"
// 	"Access-Control-Allow-Origin: *\r\n"
// 	"\r\n"
// 	"<?xml version=\"1.0\"?>"
// 	"<root xmlns=\"urn:schemas-upnp-org:device-1-0\">"
// 		"<specVersion>"
// 			"<major>1</major>"
// 			"<minor>0</minor>"
// 		"</specVersion>"
// 		"<URLBase>http://%u.%u.%u.%u:%u/%s</URLBase>" // WiFi.localIP(), _port
// 		"<device>"
// 			"<deviceType>%s</deviceType>"
// 			"<friendlyName>%s</friendlyName>"
// 			"<presentationURL>%s</presentationURL>"
// 			"<serialNumber>%s</serialNumber>"
// 			"<modelName>%s</modelName>"
// 			"<modelNumber>%s</modelNumber>"
// 			"<modelURL>%s</modelURL>"
// 			"<manufacturer>%s</manufacturer>"
// 			"<manufacturerURL>%s</manufacturerURL>"
// 			"<UDN>uuid:%s</UDN>"
// 		"</device>"
// //    "<iconList>"
// //      "<icon>"
// //        "<mimetype>image/png</mimetype>"
// //        "<height>48</height>"
// //        "<width>48</width>"
// //        "<depth>24</depth>"
// //        "<url>icon48.png</url>"
// //      "</icon>"
// //      "<icon>"
// //       "<mimetype>image/png</mimetype>"
// //       "<height>120</height>"
// //       "<width>120</width>"
// //       "<depth>24</depth>"
// //       "<url>icon120.png</url>"
// //      "</icon>"
// //    "</iconList>"
// 	"</root>\r\n"
// 	"\r\n";

typedef enum {
	NOTIFY_ALIVE_INIT,
	NOTIFY_ALIVE,
	NOTIFY_UPDATE,
	RESPONSE
} ssdp_message_t;

typedef enum {
	ROOT_FOR_ALL,
	ROOT_BY_UUID,
	ROOT_BY_TYPE
} ssdp_udn_t;

typedef struct {
	unsigned long time;
	
	ssdp_message_t type;
	ssdp_udn_t udn;
	uint32_t address;
	uint16_t port;
} ssdp_send_parameters_t;

class SSDPDeviceClass {
private:
	fnUDP *m_server;

	IPAddress m_last;

	char m_schemaURL[SSDP_SCHEMA_URL_SIZE];
	char m_uuid[SSDP_UUID_SIZE];
	char m_deviceType[SSDP_DEVICE_TYPE_SIZE];
	char m_friendlyName[SSDP_FRIENDLY_NAME_SIZE];
	char m_serialNumber[SSDP_SERIAL_NUMBER_SIZE];
	char m_presentationURL[SSDP_PRESENTATION_URL_SIZE];
	char m_manufacturer[SSDP_MANUFACTURER_SIZE];
	char m_manufacturerURL[SSDP_MANUFACTURER_URL_SIZE];
	char m_modelName[SSDP_MODEL_NAME_SIZE];
	char m_modelURL[SSDP_MODEL_URL_SIZE];
	char m_modelNumber[SSDP_MODEL_VERSION_SIZE];
	
	uint16_t m_port;
	uint8_t m_ttl;

	ssdp_send_parameters_t m_queue[SSDP_QUEUE_SIZE];

protected:
	bool readLine(std::string &value);
	bool readKeyValue(std::string &key, std::string &value);

	void postNotifyALive();
	void postNotifyUpdate();
	void postResponse(long mx);
	void postResponse(ssdp_udn_t udn, long mx);
	void post(ssdp_message_t type, ssdp_udn_t udn, IPAddress address, uint16_t port, unsigned long time);
	
	void send(ssdp_send_parameters_t *parameters);

public:
	SSDPDeviceClass();

	void update();

	// void schema(WiFiClient client);
	// std::string schema();

	void handleClient();

	const char* getUUID() { return m_uuid; };

	void setDeviceType(const std::string& deviceType) { setDeviceType(deviceType.c_str()); }
	void setDeviceType(const char *deviceType);
	void setName(const std::string& name) { setName(name.c_str()); }
	void setName(const char *name);
	void setURL(const std::string& url) { setURL(url.c_str()); }
	void setURL(const char *url);
	void setSchemaURL(const std::string& url) { setSchemaURL(url.c_str()); }
	void setSchemaURL(const char *url);
	void setSerialNumber(const std::string& serialNumber) { setSerialNumber(serialNumber.c_str()); }
	void setSerialNumber(const char *serialNumber);
	void setSerialNumber(const uint32_t serialNumber);
	void setModelName(const std::string& name) { setModelName(name.c_str()); }
	void setModelName(const char *name);
	void setModelNumber(const std::string& num) { setModelNumber(num.c_str()); }
	void setModelNumber(const char *num);
	void setModelURL(const std::string& url) { setModelURL(url.c_str()); }
	void setModelURL(const char *url);
	void setManufacturer(const std::string& name) { setManufacturer(name.c_str()); }
	void setManufacturer(const char *name);
	void setManufacturerURL(const std::string& url) { setManufacturerURL(url.c_str()); }
	void setManufacturerURL(const char *url);
	void setHTTPPort(uint16_t port);
	void setTTL(uint8_t ttl);
};

extern SSDPDeviceClass SSDPDevice;

#endif
