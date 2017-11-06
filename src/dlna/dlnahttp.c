#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#if !defined(OS_WINDOWS)
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <poll.h>
#include <netdb.h>
#include <ifaddrs.h>
#else
#include <sys/stat.h>
#define WINVER 0x0501
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "mingw.h"
#endif

#include <errno.h>
#include <sys/types.h>
#include <time.h>
 
#include "types.h"
#if !defined(OS_WINDOWS)
#include "protocol.h"
#include "actions.h"
#else
#include "../protocol.h"
#include "../actions.h"
#endif

#include "ssdp.h"
#include "upnpdescgen.h"
#include "dlnahttp.h"
#include "soapactions.h"
#include "icons.h"
  
#ifdef OS_WINDOWS
extern struct string UUID_WINDOWS;
#undef UUID
#define UUID UUID_WINDOWS
#else
extern struct string UUID;
#endif

extern struct string DEVICE_NAME;

extern char uuidvalue[];
int processDLNArequest(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
int status = ERROR_MISSING;

if(request->method==METHOD_GET)
{
	status=processDLNAGET(request,response,resources);
}
else if(request->method==METHOD_POST)
{
	status=processDLNAPOST(request,response,resources);
}
else if(request->method==METHOD_SUBSCRIBE)
{
	status=processDLNASUBSCRIBE(request,response,resources);
}
else if(request->method==METHOD_NOTIFY)
{
	status=processDLNANOTIFY(request,response,resources);
}
return (status ? status : ERROR_CANCEL);
}

int processDLNAGET(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
	char body[8192];
int len=0;
int status = 0;
struct string model_url;
struct string manufacturer_url;

	if(!strcmp(request->URI.data+33+6,"rootDesc.xml"))
	{
		struct string tmpkey = string("User-Agent");
		struct string *user_agent = dict_get(&request->headers, &tmpkey);
		if(user_agent && (!strcmp(user_agent->data,"Samsung") || !strcmp(user_agent->data,"SEC_HHP")))
		{
		model_url=string("<sec:X_ProductCap>smi,DCM10,getMediaInfo.sec,getCaptionInfo.sec</sec:X_ProductCap>");
		manufacturer_url=string("<sec:ProductCap>smi,DCM10,getMediaInfo.sec,getCaptionInfo.sec</sec:ProductCap>");
		}
		else
		{
		model_url=string("<modelURL>http://www.filement.com</modelURL>");
		manufacturer_url=string("<manufacturerURL>http://www.filement.com/</manufacturerURL>");
		}
		char *tmpname=strdup(DEVICE_NAME.data);
	    for(len=DEVICE_NAME.length-1;len>=0;len--)
		{
			if(tmpname[len]=='"' || tmpname[len]==':' || tmpname[len]=='<' || tmpname[len]=='>' || tmpname[len]=='&')tmpname[len]='_';
		}
		
		len=snprintf(body,8192,"<?xml version=\"1.0\"?>\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Filement %s:1</friendlyName><manufacturer>Filement DLNA</manufacturer>%s<modelDescription>Filement Device</modelDescription><modelName>Windows Media Connect compatible Filement</modelName><modelNumber>0.0.0</modelNumber>%s<serialNumber>00000000</serialNumber><UDN>%s</UDN><dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMS-1.50</dlna:X_DLNADOC><presentationURL>http://%s:%d/%s/dlna/Presentation</presentationURL><iconList><icon><mimetype>image/png</mimetype><width>48</width><height>48</height><depth>24</depth><url>/%s/dlna/icon/48.png</url></icon><icon><mimetype>image/png</mimetype><width>120</width><height>120</height><depth>24</depth><url>/%s/dlna/icon/120.png</url></icon><icon><mimetype>image/jpeg</mimetype><width>48</width><height>48</height><depth>24</depth><url>/%s/dlna/icon/48.jpg</url></icon><icon><mimetype>image/jpeg</mimetype><width>120</width><height>120</height><depth>24</depth><url>/%s/dlna/icon/120.jpg</url></icon></iconList><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><controlURL>/%s/dlna/ctl/ContentDir</controlURL><eventSubURL>/%s/dlna/evt/ContentDir</eventSubURL><SCPDURL>/%s/dlna/ContentDir.xml</SCPDURL></service><service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId><controlURL>/%s/dlna/ctl/ConnectionMgr</controlURL><eventSubURL>/%s/dlna/evt/ConnectionMgr</eventSubURL><SCPDURL>/%s/dlna/ConnectionMgr.xml</SCPDURL></service><service><serviceType>urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1</serviceType><serviceId>urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar</serviceId><controlURL>/%s/dlna/ctl/X_MS_MediaReceiverRegistrar</controlURL><eventSubURL></eventSubURL><SCPDURL>/%s/dlna/X_MS_MediaReceiverRegistrar.xml</SCPDURL></service></serviceList></device></root>",tmpname,manufacturer_url.data,model_url.data,uuidvalue,"192.168.2.29",4080,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data,UUID.data);
		
		free(tmpname);
		//if(len<0)return ERROR_MEMORY;
		//TODO icons
		 
		//len=sprintf(body,"<?xml version=\"1.0\"?>\n<root xmlns=\"urn:schemas-upnp-org:device-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><device><deviceType>urn:schemas-upnp-org:device:MediaServer:1</deviceType><friendlyName>Filement Device 2: 1</friendlyName><manufacturer>Justin Maggard</manufacturer><manufacturerURL>http://www.netgear.com/</manufacturerURL><modelDescription>MiniDLNA on Linux</modelDescription><modelName>Windows Media Connect compatible (MiniDLNA)</modelName><modelNumber>1.1.1</modelNumber><modelURL>http://www.netgear.com</modelURL><serialNumber>00000000</serialNumber><UDN>%s</UDN><dlna:X_DLNADOC xmlns:dlna=\"urn:schemas-dlna-org:device-1-0\">DMS-1.50</dlna:X_DLNADOC><presentationURL>http://192.168.2.29:4080/</presentationURL><iconList><icon><mimetype>image/png</mimetype><width>48</width><height>48</height><depth>24</depth><url>/icons/sm.png</url></icon><icon><mimetype>image/png</mimetype><width>120</width><height>120</height><depth>24</depth><url>/icons/lrg.png</url></icon><icon><mimetype>image/jpeg</mimetype><width>48</width><height>48</height><depth>24</depth><url>/icons/sm.jpg</url></icon><icon><mimetype>image/jpeg</mimetype><width>120</width><height>120</height><depth>24</depth><url>/icons/lrg.jpg</url></icon></iconList><serviceList><service><serviceType>urn:schemas-upnp-org:service:ContentDirectory:1</serviceType><serviceId>urn:upnp-org:serviceId:ContentDirectory</serviceId><controlURL>/ctl/ContentDir</controlURL><eventSubURL>/evt/ContentDir</eventSubURL><SCPDURL>/ContentDir.xml</SCPDURL></service><service><serviceType>urn:schemas-upnp-org:service:ConnectionManager:1</serviceType><serviceId>urn:upnp-org:serviceId:ConnectionManager</serviceId><controlURL>/ctl/ConnectionMgr</controlURL><eventSubURL>/evt/ConnectionMgr</eventSubURL><SCPDURL>/ConnectionMgr.xml</SCPDURL></service><service><serviceType>urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1</serviceType><serviceId>urn:microsoft.com:serviceId:X_MS_MediaReceiverRegistrar</serviceId><controlURL>/%s/dlna/ctl/X_MS_MediaReceiverRegistrar</controlURL><eventSubURL></eventSubURL><SCPDURL>/X_MS_MediaReceiverRegistrar.xml</SCPDURL></service></serviceList></device></root>\n",uuidvalue,UUID.data);
		
	
		 
		if(!body)
		{
			return Forbidden;
		}
	//	write(1,"\n",1);
	//	write(1,body,len);
	//	write(1,"\n",1);

	struct string key, value;

		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("EXT");
		value = string("");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, len);
		if (!status) status = response_entity_send(&resources->stream, response, body, len);
	}
	else if(!strcmp(request->URI.data+33+6,"X_MS_MediaReceiverRegistrar.xml"))
	{
	
		len=snprintf(body,8192,"<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><actionList><action><name>IsAuthorized</name><argumentList><argument><name>DeviceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_DeviceID</relatedStateVariable></argument><argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument></argumentList></action><action><name>IsValidated</name><argumentList><argument><name>DeviceID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_DeviceID</relatedStateVariable></argument><argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument></argumentList></action></actionList><serviceStateTable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_DeviceID</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RegistrationReqMsg</name><dataType>bin.base64</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RegistrationRespMsg</name><dataType>bin.base64</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>int</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>AuthorizationDeniedUpdateID</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>AuthorizationGrantedUpdateID</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>ValidationRevokedUpdateID</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>ValidationSucceededUpdateID</name><dataType>ui4</dataType></stateVariable></serviceStateTable></scpd>");
		
		if(len<0)return ERROR_MEMORY;
	
			if(!body)
		{
			return Forbidden; 
		}
		//write(1,"\n",1);
		//write(1,body,len);
		//write(1,"\n",1);

		struct string key, value;

		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, len);
		if (!status) status = response_entity_send(&resources->stream, response, body, len);
	
	}
	else if(!strcmp(request->URI.data+33+6,"ConnectionMgr.xml"))
	{
	
		len=snprintf(body,8192,"<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><actionList><action><name>GetProtocolInfo</name><argumentList><argument><name>Source</name><direction>out</direction><relatedStateVariable>SourceProtocolInfo</relatedStateVariable></argument><argument><name>Sink</name><direction>out</direction><relatedStateVariable>SinkProtocolInfo</relatedStateVariable></argument></argumentList></action><action><name>GetCurrentConnectionIDs</name><argumentList><argument><name>ConnectionIDs</name><direction>out</direction><relatedStateVariable>CurrentConnectionIDs</relatedStateVariable></argument></argumentList></action><action><name>GetCurrentConnectionInfo</name><argumentList><argument><name>ConnectionID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument><argument><name>RcsID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_RcsID</relatedStateVariable></argument><argument><name>AVTransportID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_AVTransportID</relatedStateVariable></argument><argument><name>ProtocolInfo</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ProtocolInfo</relatedStateVariable></argument><argument><name>PeerConnectionManager</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionManager</relatedStateVariable></argument><argument><name>PeerConnectionID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionID</relatedStateVariable></argument><argument><name>Direction</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Direction</relatedStateVariable></argument><argument><name>Status</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_ConnectionStatus</relatedStateVariable></argument></argumentList></action></actionList><serviceStateTable><stateVariable sendEvents=\"yes\"><name>SourceProtocolInfo</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"yes\"><name>SinkProtocolInfo</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"yes\"><name>CurrentConnectionIDs</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionStatus</name><dataType>string</dataType><allowedValueList><allowedValue>OK</allowedValue><allowedValue>ContentFormatMismatch</allowedValue><allowedValue>InsufficientBandwidth</allowedValue><allowedValue>UnreliableChannel</allowedValue><allowedValue>Unknown</allowedValue></allowedValueList></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionManager</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Direction</name><dataType>string</dataType><allowedValueList><allowedValue>Input</allowedValue><allowedValue>Output</allowedValue></allowedValueList></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ProtocolInfo</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ConnectionID</name><dataType>i4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_AVTransportID</name><dataType>i4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_RcsID</name><dataType>i4</dataType></stateVariable></serviceStateTable></scpd>");
	
		if(len<0)return ERROR_MEMORY;
	
			if(!body)
		{
			return Forbidden;
		}
		//write(1,"\n",1);
		//write(1,body,len);
		//write(1,"\n",1);

		struct string key, value;

		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, len);
		if (!status) status = response_entity_send(&resources->stream, response, body, len);
	
	}
	else if(!strcmp(request->URI.data+33+6,"ContentDir.xml"))
	{
	
			len=snprintf(body,8192,"<?xml version=\"1.0\"?>\n<scpd xmlns=\"urn:schemas-upnp-org:service-1-0\"><specVersion><major>1</major><minor>0</minor></specVersion><actionList><action><name>GetSearchCapabilities</name><argumentList><argument><name>SearchCaps</name><direction>out</direction><relatedStateVariable>SearchCapabilities</relatedStateVariable></argument></argumentList></action><action><name>GetSortCapabilities</name><argumentList><argument><name>SortCaps</name><direction>out</direction><relatedStateVariable>SortCapabilities</relatedStateVariable></argument></argumentList></action><action><name>GetSystemUpdateID</name><argumentList><argument><name>Id</name><direction>out</direction><relatedStateVariable>SystemUpdateID</relatedStateVariable></argument></argumentList></action><action><name>Browse</name><argumentList><argument><name>ObjectID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument><argument><name>BrowseFlag</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_BrowseFlag</relatedStateVariable></argument><argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument><argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument><argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument><argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument><argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument></argumentList></action><action><name>Search</name><argumentList><argument><name>ContainerID</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_ObjectID</relatedStateVariable></argument><argument><name>SearchCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SearchCriteria</relatedStateVariable></argument><argument><name>Filter</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Filter</relatedStateVariable></argument><argument><name>StartingIndex</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Index</relatedStateVariable></argument><argument><name>RequestedCount</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>SortCriteria</name><direction>in</direction><relatedStateVariable>A_ARG_TYPE_SortCriteria</relatedStateVariable></argument><argument><name>Result</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Result</relatedStateVariable></argument><argument><name>NumberReturned</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>TotalMatches</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_Count</relatedStateVariable></argument><argument><name>UpdateID</name><direction>out</direction><relatedStateVariable>A_ARG_TYPE_UpdateID</relatedStateVariable></argument></argumentList></action></actionList><serviceStateTable><stateVariable sendEvents=\"yes\"><name>TransferIDs</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_ObjectID</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Result</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SearchCriteria</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_BrowseFlag</name><dataType>string</dataType><allowedValueList><allowedValue>BrowseMetadata</allowedValue><allowedValue>BrowseDirectChildren</allowedValue></allowedValueList></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Filter</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_SortCriteria</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Index</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_Count</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>A_ARG_TYPE_UpdateID</name><dataType>ui4</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>SearchCapabilities</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"no\"><name>SortCapabilities</name><dataType>string</dataType></stateVariable><stateVariable sendEvents=\"yes\"><name>SystemUpdateID</name><dataType>ui4</dataType></stateVariable></serviceStateTable></scpd>");
			
			if(len<0)return ERROR_MEMORY;
			
			if(!body)
		{
			return Forbidden;
		}
		//write(1,"\n",1);
		//write(1,body,len);
		//write(1,"\n",1);

		struct string key, value;

		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, len);
		if (!status) status = response_entity_send(&resources->stream, response, body, len);
	
	}
	else if(!strcmp(request->URI.data+33+6,"Presentation"))
	{
	struct string key, value;
	int status = 0;
		response->code = MovedPermanently;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Location");
		value = string("http://www.filement.com/");
		response_header_add(response, &key, &value);
	
		status = !response_headers_send(&resources->stream, request, response, 0);
		if(!status) return 0;
		else return ERROR_MISSING;
	}
	else if(!strcmp(request->URI.data+33+6,"icon/48.jpg"))
	{
	struct string key, value;
	int status = 0;
		response->code = OK;
		key = string("Content-Type");
		value = string("image/jpeg");
		response_header_add(response, &key, &value);
		status = !response_headers_send(&resources->stream, request, response, icon_jpg_48_size);
		if (!status) status = response_entity_send(&resources->stream, response, icon_jpg_48, icon_jpg_48_size);
	}
	else if(!strcmp(request->URI.data+33+6,"icon/120.jpg"))
	{
	struct string key, value;
	int status = 0;
		response->code = OK;
		key = string("Content-Type");
		value = string("image/jpeg");
		response_header_add(response, &key, &value);
		status = !response_headers_send(&resources->stream, request, response, icon_jpg_120_size);
		if (!status) status = response_entity_send(&resources->stream, response, icon_jpg_120, icon_jpg_120_size);
	}
	else if(!strcmp(request->URI.data+33+6,"icon/48.png"))
	{
	struct string key, value;
	int status = 0;
		response->code = OK;
		key = string("Content-Type");
		value = string("image/png");
		response_header_add(response, &key, &value);
		status = !response_headers_send(&resources->stream, request, response, icon_png_48_size);
		if (!status) status = response_entity_send(&resources->stream, response, icon_png_48, icon_png_48_size);
	}
	else if(!strcmp(request->URI.data+33+6,"icon/120.png"))
	{
	struct string key, value;
	int status = 0;
		response->code = OK;
		key = string("Content-Type");
		value = string("image/png");
		response_header_add(response, &key, &value);
		status = !response_headers_send(&resources->stream, request, response, icon_png_120_size);
		if (!status) status = response_entity_send(&resources->stream, response, icon_png_120, icon_png_120_size);
	}
	
	return status;
}

static const struct 
{
	const char * action; 
	const int actionlen; 
	int (*actionfun)(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action);
}
SOAPActions[] =
{
	{ "IsAuthorized", sizeof("IsAuthorized")-1, SAIsAuthorized},
	{ "IsValidated", sizeof("IsValidated")-1, SAIsAuthorized},
	{ "Browse", sizeof("Browse")-1, SABrowse},
	{ "Search", sizeof("Search")-1, SASearch},
	{ "GetCurrentConnectionInfo", sizeof("GetCurrentConnectionInfo")-1, SAGetCurrentConnectionInfo},
	{ "GetCurrentConnectionIDs", sizeof("GetCurrentConnectionIDs")-1, SAGetCurrentConnectionIDs},
	{ "GetProtocolInfo", sizeof("GetProtocolInfo")-1, SAGetProtocolInfo},
	{ "GetSearchCapabilities", sizeof("GetSearchCapabilities")-1, SAGetSearchCapabilities},
	{ "GetSortCapabilities", sizeof("GetSortCapabilities")-1, SAGetSortCapabilities},
	{ "QueryStateVariable", sizeof("QueryStateVariable")-1,SAQueryStateVariable},
	{ "GetSystemUpdateID", sizeof("GetSystemUpdateID")-1,SAGetSystemUpdateID},
	{ "X_GetFeatureList", sizeof("X_GetFeatureList")-1,SASamsungGetFeatureList},
	{ "X_SetBookmark", sizeof("X_SetBookmark")-1,SASamsungSetBookmark},
	{ 0, 0 }
};

int processDLNAPOST(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
char * p;
	struct string key = string("soapaction");
	struct string *action = dict_get(&request->headers, &key);
	if (!action) return ERROR_MISSING;

	//printf("SOAP Action:%s\n",action->data);
	//"urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1#IsAuthorized"
	p = strchr(action->data, '#');
	if(p)
		{
		int i = 0;
		int methodlen; 
		char * p2;
		p++;
		p2 = strchr(p, '"');
		if(p2)
			methodlen = p2 - p;
		else
			methodlen = action->length - ( p - action->data );
			
			while(SOAPActions[i].action)
			{
				if(!strncmp(p, SOAPActions[i].action, SOAPActions[i].actionlen)) 
				{ 
				 	return SOAPActions[i].actionfun(request,response,resources,SOAPActions[i].action);
				}
				i++;
			}
		
		}
	
	
	return ERROR_MISSING;
}

int processDLNASUBSCRIBE(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
/*
	struct string key = string("soapaction");
	struct string *action = dict_get(&request->headers, &key);
printf("POST\n");fflush(stdout);
	if (!action) return ERROR_MISSING;
	else 
	{
	printf("SOAP Action:%s\n",action->data);
	}
*/
	struct string key, value;
	int status = 0;
		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, 0);
		if(!status) return 0;
		else return ERROR_MISSING;
	
}

int processDLNANOTIFY(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources)
{
	struct string key, value;
	int status = 0;
	
		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, 0);
		if(!status) return 0;
		else return ERROR_MISSING;
}
