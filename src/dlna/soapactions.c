#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <time.h>

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
 
#include "types.h"
#include "format.h"

#if !defined(OS_WINDOWS)
#include "protocol.h"
#include "actions.h"
#include "remote.h"
#else
#include "../protocol.h"
#include "../actions.h"
#include "../remote.h"
#endif
#include "upnpreplyparse.h"
#include "soapactions.h"

  
#ifdef OS_WINDOWS
extern struct string UUID_WINDOWS;
#undef UUID
#define UUID UUID_WINDOWS
#else
extern struct string UUID;
#endif

int UpdateId = 0;
/*
//operations are atomic
static pthread_mutex_t UpdateIdMutex = PTHREAD_MUTEX_INITIALIZER;
static void UpdateIdSet(UpdateId)
{
pthread_mutex_lock(UpdateIdMutex);
UpdateId=UpdateId;
pthread_mutex_unlock(UpdateIdMutex);
}

static int UpdateIdGet()
{
int updid;
pthread_mutex_lock(UpdateIdMutex);
updid=UpdateId;
pthread_mutex_unlock(UpdateIdMutex);
return updid;
}
*/

int SoapErr(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, int errCode, const char * errDesc)
{
	int status;
	static const char resp[] = 
		"<s:Envelope "
		"xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>"
		"<s:Fault>"
		"<faultcode>s:Client</faultcode>"
		"<faultstring>UPnPError</faultstring>"
		"<detail>"
		"<UPnPError xmlns=\"urn:schemas-upnp-org:control-1-0\">"
		"<errorCode>%d</errorCode>"
		"<errorDescription>%s</errorDescription>" 
		"</UPnPError>"
		"</detail>" 
		"</s:Fault>"
		"</s:Body>"
		"</s:Envelope>";

	char body[2048];
	int bodylen;

	
	bodylen = snprintf(body, sizeof(body), resp, errCode, errDesc);
	
	struct string key, value;
	 
		response->code = InternalServerError;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, bodylen);
		if (!status) status = response_entity_send(&resources->stream, response, body, bodylen);
	
	return ERROR;
}

int SoapSend(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * body, int bodylen)
{
	int status;
	static const char beforebody[] =
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
		"<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
		"s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
		"<s:Body>";

	static const char afterbody[] =
		"</s:Body>"
		"</s:Envelope>\r\n";
	
	struct string key, value;

	off_t len = sizeof(beforebody) - 1 + sizeof(afterbody) - 1 + bodylen;

	if (!body || bodylen < 0)
	{
		return Forbidden;
	}

		response->code = OK;
		key = string("Connection");
		value = string("close");
		response_header_add(response, &key, &value);
		key = string("Content-Type");
		value = string("text/xml; charset=\"utf-8\"");
		response_header_add(response, &key, &value);
		
		status = !response_headers_send(&resources->stream, request, response, len);
		if (!status) status = response_entity_send(&resources->stream, response, beforebody, sizeof(beforebody) - 1);
		else return status;
		
		if (!status) status = response_entity_send(&resources->stream, response, body, bodylen);
		else return status;
		
		if (!status) status = response_entity_send(&resources->stream, response, afterbody, sizeof(afterbody) - 1);
		else return status;
	
	return status;
}

int SAIsAuthorized(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	struct string buffer;
	int status;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
		if (status = stream_read(&resources->stream, &buffer, length)) return status;
	//	if (!writeall(output, buffer.data, buffer.length)) return ERROR_WRITE;
	
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Result>%d</Result>"
		"</u:%sResponse>";

	char body[512];
	struct NameValueParserData data;
	const char * id;

	ParseNameValue(buffer.data, buffer.length, &data);
	id = GetValueFromNameValueList(&data, "DeviceID");
	
	ClearNameValueList(&data);	
	stream_read_flush(&resources->stream, length);
	
	if(id)
	{
		int bodylen;
		bodylen = snprintf(body, sizeof(body), resp,
			action, "urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:1",
			1, action);	
		return SoapSend(request, response, resources, body, bodylen);
	}
	else
		return SoapErr(request, response, resources, 402, "Invalid Args");

		
		
return 0;
}

int SAGetSearchCapabilities(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	struct string buffer;
	int status;
		off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
		if (status = stream_read(&resources->stream, &buffer, length)) return status;
		stream_read_flush(&resources->stream, length);
	//	if (!writeall(output, buffer.data, buffer.length)) return ERROR_WRITE;
	
	static const char resp[] =
		"<u:%sResponse xmlns:u=\"%s\">"
		"<SearchCaps>"
		  "dc:creator,"
		  "dc:date,"
		  "dc:title,"
		  "upnp:album,"
		  "upnp:actor,"
		  "upnp:artist,"
		  "upnp:class,"
		  "upnp:genre,"
		  "@refID"
		"</SearchCaps>"
		"</u:%sResponse>";

	char body[512];
	struct NameValueParserData data;
	
	int bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		action);
	
		return SoapSend(request, response, resources, body, bodylen);
	
}

int SAGetSortCapabilities(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
		struct string buffer;
	int status;
		off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
		if (status = stream_read(&resources->stream, &buffer, length)) return status;
		stream_read_flush(&resources->stream, length);
	//	if (!writeall(output, buffer.data, buffer.length)) return ERROR_WRITE;
	
	static const char resp[] =
		"<u:%sResponse xmlns:u=\"%s\">"
		"<SearchCaps>"
		  "dc:creator,"
		  "dc:date,"
		  "dc:title,"
		  "upnp:album,"
		  "upnp:actor,"
		  "upnp:artist,"
		  "upnp:class,"
		  "upnp:genre,"
		  "@refID"
		"</SearchCaps>"
		"</u:%sResponse>";

	char body[512];
	struct NameValueParserData data;
	
	int bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		action);
	
	return SoapSend(request, response, resources, body, bodylen);

}


int SAQueryStateVariable(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	struct string buffer;
	int status;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
		if (status = stream_read(&resources->stream, &buffer, length)) return status;
		
	static const char resp[] =
        "<u:%sResponse "
        "xmlns:u=\"%s\">"
		"<return>%s</return>"
        "</u:%sResponse>";

	char body[512];
	struct NameValueParserData data;
	const char * var_name;

	ParseNameValue(buffer.data, buffer.length, &data);
	/*var_name = GetValueFromNameValueList(&data, "QueryStateVariable"); */
	/*var_name = GetValueFromNameValueListIgnoreNS(&data, "varName");*/
	var_name = GetValueFromNameValueList(&data, "varName");
	ClearNameValueList(&data);	
	stream_read_flush(&resources->stream, length);

	if(!var_name)
	{
		return SoapErr(request, response, resources, 402, "Invalid Args");
	}
	else if(strcmp(var_name, "ConnectionStatus") == 0)
	{	
		int bodylen = snprintf(body, sizeof(body), resp,
                           action, "urn:schemas-upnp-org:control-1-0",
		                   "Connected", action);
		return SoapSend(request, response, resources, body, bodylen);
	}
	else
		return SoapErr(request, response, resources, 402, "Invalid Args");

}


int SASamsungGetFeatureList(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status=0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	static const char body[] =
		"<u:X_GetFeatureListResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
		"<FeatureList>"
		"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n"
		"&lt;Features xmlns=\"urn:schemas-upnp-org:av:avs\""
		" xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\""
		" xsi:schemaLocation=\"urn:schemas-upnp-org:av:avs http://www.upnp.org/schemas/av/avs.xsd\"&gt;"
		"&lt;Feature name=\"samsung.com_BASICVIEW\" version=\"1\"&gt;"
		 "&lt;container id=\"1\" type=\"object.item.audioItem\"/&gt;"
		 "&lt;container id=\"2\" type=\"object.item.videoItem\"/&gt;"
		 "&lt;container id=\"3\" type=\"object.item.imageItem\"/&gt;"
		"&lt;/Feature&gt;"
		"</FeatureList></u:X_GetFeatureListResponse>";

	
	if (status = stream_read(&resources->stream, &buffer, length)) return status;
	stream_read_flush(&resources->stream, length);
		
	return SoapSend(request, response, resources, body, sizeof(body)-1);
}

int SASamsungSetBookmark(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
//TODO to make it work, currently we don't support bookmarks
	int status=0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	
	static const char body[] =
	    "<u:X_SetBookmarkResponse"
	    " xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"
	    "</u:X_SetBookmarkResponse>";
/*
	struct NameValueParserData data;
	char *ObjectID, *PosSecond;

	ParseNameValue(h->req_buf + h->req_contentoff, h->req_contentlen, &data, 0);
	ObjectID = GetValueFromNameValueList(&data, "ObjectID");
	PosSecond = GetValueFromNameValueList(&data, "PosSecond");
	if( ObjectID && PosSecond )
	{
		int ret;
		ret = sql_exec(db, "INSERT OR REPLACE into BOOKMARKS"
		                   " VALUES "
		                   "((select DETAIL_ID from OBJECTS where OBJECT_ID = '%q'), %q)", ObjectID, PosSecond);
		if( ret != SQLITE_OK )
			DPRINTF(E_WARN, L_METADATA, "Error setting bookmark %s on ObjectID='%s'\n", PosSecond, ObjectID);
		BuildSendAndCloseSoapResp(h, resp, sizeof(resp)-1);
	}
	else
		SoapError(h, 402, "Invalid Args");

	ClearNameValueList(&data);
*/
	if (status = stream_read(&resources->stream, &buffer, length)) return status;
	stream_read_flush(&resources->stream, length);
	
return SoapSend(request, response, resources, body, sizeof(body)-1);
}


int SAGetSystemUpdateID(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status=0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Id>%d</Id>"
		"</u:%sResponse>";

	if (status = stream_read(&resources->stream, &buffer, length)) return status;
	stream_read_flush(&resources->stream, length);
		
	char body[512];


	int bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		UpdateId, action);
	return SoapSend(request, response, resources, body, bodylen);
}


int SAGetProtocolInfo(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status =0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<Source>"
		DLNA_SUPPORTED_TYPES
		"</Source>"
		"<Sink></Sink>"
		"</u:%sResponse>";

	if (status = stream_read(&resources->stream, &buffer, length)) return status;
	stream_read_flush(&resources->stream, length);	
		
	char body[sizeof(resp)+1024]; //TODO make it more precise

	int bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ContentDirectory:1",
		action);	
	return SoapSend(request, response, resources, body, bodylen);
}


int SAGetCurrentConnectionIDs(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status = 0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	/* TODO: Use real data.*/
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<ConnectionIDs>0</ConnectionIDs>"
		"</u:%sResponse>";
		
if (status = stream_read(&resources->stream, &buffer, length)) return status;
	stream_read_flush(&resources->stream, length);	
		
	char body[512];

	int bodylen = snprintf(body, sizeof(body), resp,
		action, "urn:schemas-upnp-org:service:ConnectionManager:1",
		action);	
	return SoapSend(request, response, resources, body, bodylen);
}


int SAGetCurrentConnectionInfo(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status = 0;
	struct string buffer;
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	/* TODO: Use real data.  */
	static const char resp[] =
		"<u:%sResponse "
		"xmlns:u=\"%s\">"
		"<RcsID>-1</RcsID>"
		"<AVTransportID>-1</AVTransportID>"
		"<ProtocolInfo></ProtocolInfo>"
		"<PeerConnectionManager></PeerConnectionManager>"
		"<PeerConnectionID>-1</PeerConnectionID>"
		"<Direction>Output</Direction>"
		"<Status>Unknown</Status>"
		"</u:%sResponse>";

	char body[sizeof(resp)+128];
	struct NameValueParserData data;
	const char *id_str;
	int id;
	char *endptr = NULL;

	
	if (status = stream_read(&resources->stream, &buffer, length)) return status;
		

	ParseNameValue(buffer.data, buffer.length, &data);
	id_str = GetValueFromNameValueList(&data, "ConnectionID");
	
	ClearNameValueList(&data);	
	stream_read_flush(&resources->stream, length);
	if(id_str)
		id = strtol(id_str, &endptr, 10);
	if (!id_str || endptr == id_str)
	{
		return SoapErr(request, response, resources, 402, "Invalid Args");
	}
	else if(id != 0)
	{
		return SoapErr(request, response, resources, 402, "Object not found");
	}
	else
	{
		int bodylen = snprintf(body, sizeof(body), resp,
			action, "urn:schemas-upnp-org:service:ConnectionManager:1",
			action);	
		return SoapSend(request, response, resources, body, bodylen);
	}
}

/*static bool valid_targets(const union json *root)
{
	if (json_type(root) != OBJECT) return false;

	union json *category;

	struct dict_iterator it;
	const struct dict_item *item;
	for(item = dict_first(&it, root->object); item; item = dict_next(&it, root->object))
		if (json_type(item->value) != ARRAY)
			return false;

	return true;
}*/

int SASearch(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status = 0;
	char key[CACHE_KEY_SIZE];

	union json *root = NULL, *item = NULL, *tmpitem = NULL, *array = NULL;
	struct string *output_items = 0;
	struct string tmpkey,catkey,friendly_name;
	struct string buffer;
	const struct cache *cache=0;
	char tinyurlkey[CACHE_KEY_SIZE];
	
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	struct string hkey = string("host");
	struct string *host = dict_get(&request->headers, &hkey);
	if (!host) return ERROR_MISSING;

	char body[8192];
	char *body_dyn=0;
	int bodylen=0;
	int i=0;
	char *ptr;
	char *ContainerID, *Filter, *SearchCriteria, *SortCriteria;
	struct NameValueParserData data;
	int RequestedCount = 0;
	int StartingIndex = 0;

	if (status = dlna_key(key)) return status;
	
	if (status = stream_read(&resources->stream, &buffer, length)) return status;
		
	ParseNameValue(buffer.data, buffer.length, &data);
	
	stream_read_flush(&resources->stream, length);
	
	ContainerID = GetValueFromNameValueList(&data, "ContainerID");
	Filter = GetValueFromNameValueList(&data, "Filter");
	SearchCriteria = GetValueFromNameValueList(&data, "SearchCriteria");
	SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");

	if( (ptr = GetValueFromNameValueList(&data, "RequestedCount")) )
		RequestedCount = atoi(ptr);
	if( !RequestedCount )
		RequestedCount = -1;
	if( (ptr = GetValueFromNameValueList(&data, "StartingIndex")) )
		StartingIndex = atoi(ptr);
	if( !ContainerID )
	{
		if( !(ContainerID = GetValueFromNameValueList(&data, "ObjectID")) )
		{
			goto error;
		}
	}

	cache = cache_use(key);
	root = (union json *)cache->value;
	if(!root) goto error;
	if(json_type(root)!=OBJECT)goto error;

		struct string parent_category;
		
		if(ContainerID)
		{
			if(*ContainerID=='1')
			{
				switch (*(ContainerID+1))
				{
					case '4':
					*ContainerID='1';
					break;
					case '5':
					*ContainerID='2';
					break;
				}
			}
			else
			{
				switch (*ContainerID)
				{
					case '4':
					case '5':
					case '6':
					case '7':
					case 'F':
					*ContainerID='1';
					break;
					case '8':
					*ContainerID='2';
					break;
					case 'B':
					case 'C':
					*ContainerID='3';
					break;
				}
			}
		}
		int cmpObjId=0;
			 if(strstr(SearchCriteria,"music") || strstr(SearchCriteria,"audio") || *ContainerID=='1') { catkey = string("music"); parent_category = string("1"); cmpObjId=1; }
		else if(strstr(SearchCriteria,"video") || *ContainerID=='2' ) { catkey = string("videos"); parent_category = string("2"); cmpObjId=2; }
		else if(strstr(SearchCriteria,"picture") || *ContainerID=='3') { catkey = string("photos"); parent_category = string("3"); cmpObjId=3; }
		else goto error;
		 
			array=dict_get(root->object, &catkey);
			if(!array) goto error;
			if(json_type(array)!=ARRAY)goto error;
				bodylen = 512;	
		int itemlen = 0;
		union json *title,*fsize,*protocolInfo,*class,*tinyurl;
		struct string *url=0;
		//TODO: allocate memory only for online ones
		output_items=calloc(array->array_node.length+1,sizeof(struct string )); // this way we are able to free part of the output items on error
	
		int show_offline=0;
		struct string offline_buf;
		offline_buf.length=0;
		int items_num=0;

		if(*(ContainerID+1)=='_')show_offline=1;
		
	
		for(i=0;i<array->array_node.length;i++)
		{
		//&lt;item id=\"1$4$1\" parentID=\"1$4\" restricted=\"1\" refID=\"64$1\"&gt;&lt;dc:title&gt;Use Somebody (Armin van Buuren Rework)&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;&lt;dc:description&gt;Excellent&lt;/dc:description&gt;&lt;dc:creator&gt;Laura Jansen&lt;/dc:creator&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;&lt;upnp:artist&gt;Various Artists&lt;/upnp:artist&gt;&lt;upnp:album&gt;Sense Of Trance #32&lt;/upnp:album&gt;&lt;upnp:genre&gt;Trance&lt;/upnp:genre&gt;&lt;upnp:originalTrackNumber&gt;32&lt;/upnp:originalTrackNumber&gt;&lt;res size=\"20464617\" duration=\"0:08:20.848\" bitrate=\"320000\" sampleFrequency=\"44100\" nrAudioChannels=\"2\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000\"&gt;http://192.168.2.29:4080/b9dd96c7cbf3468f08b2f1f1cdfb2242/?%%7B%%22auth_id%%22%%3A%%22aw1YiM04Z98QZQxc%%22%%2C%%22actions%%22%%3A%%7B%%22ffs.download%%22%%3A%%5B%%7B%%22block_id%%22%%3A1%%2C%%22path%%22%%3A%%22%%22%%7D%%5D%%7D%%7D&lt;/res&gt;&lt;/item&gt;
			item=vector_get(&array->array_node,i);

			tmpkey = string("host");
				if(!show_offline && !dict_get(item->object, &tmpkey) )continue;
				else if(show_offline && dict_get(item->object, &tmpkey) )continue;
				
				
			items_num++;
			
			tmpkey = string("name");
			title=dict_get(item->object, &tmpkey);
			if(!title)goto error_free_array;
			if(json_type(title)!=STRING)goto error_free_array;
			
			tmpkey = string("size");
			fsize=dict_get(item->object, &tmpkey);
			if(!fsize)fsize=json_integer(0);
			if(json_type(fsize)!=INTEGER)fsize=json_integer(0);
			//if(!fsize)goto error_free_array;
			//if(json_type(fsize)!=INTEGER)goto error_free_array;
			
			tmpkey = string("class");
			class=dict_get(item->object, &tmpkey);
			if(!class)goto error_free_array;
			if(json_type(class)!=STRING)goto error_free_array;
			
			tmpkey = string("protocolInfo");
			protocolInfo=dict_get(item->object, &tmpkey);
			if(!protocolInfo)goto error_free_array;
			if(json_type(protocolInfo)!=STRING)goto error_free_array;

			#define S(s) s, sizeof(s) - 1
			
			tmpkey = string("tinyurl");
			tinyurl=dict_get(item->object, &tmpkey);
			if(!tinyurl)
			{
				url=string_alloc(0,224+UUID.length+6);
				if(!url) goto error;
#if !defined(OS_IOS)
				url->length=format(url->data,str(S("/?{%22actions%22:{%22proxy.forward%22:{%22category%22:%22")),str(catkey.data,catkey.length),str(S("%22%2C%22id%22:")),uint(i),str(S("}}%2C%22protocol%22:{%22name%22:%22remoteJson%22%2C%22function%22:%22kernel.sockets.receiveJson%22%2C%22request_id%22:%2214%22}}"))) - url->data;
#else
				url->length=format_ios(url->data,str(S("/?{%22actions%22:{%22proxy.forward%22:{%22category%22:%22")),str(catkey.data,catkey.length),str(S("%22%2C%22id%22:")),uint(i),str(S("}}%2C%22protocol%22:{%22name%22:%22remoteJson%22%2C%22function%22:%22kernel.sockets.receiveJson%22%2C%22request_id%22:%2214%22}}"))) - url->data;
#endif
				
				tmpitem = json_string_old(url);
				free(url);
				if (!tmpitem) goto error;
				
				if (!cache_create(tinyurlkey, CACHE_URL, tmpitem, 0))
				{
					json_free(tmpitem);
					goto error;
				}
				
				root=vector_get(&array->array_node,i);
				if(json_type(root)!=OBJECT)goto error;
				
				tmpkey = string(tinyurlkey, CACHE_KEY_SIZE);
				tinyurl = json_string_old(&tmpkey);
				if(!tinyurl) goto error;
				
				tmpkey = string("tinyurl");
				json_object_insert_old(item,&tmpkey,tinyurl);
			}
			if(json_type(tinyurl)!=STRING)goto error_free_array;
			
			//example URL 
			url=string_alloc(0,16+CACHE_KEY_SIZE+host->length+UUID.length + title->string_node.length+1);
#if !defined(OS_IOS)
			url->length=format(url->data,str(S("http://")),str(host->data,host->length),str(S("/")),str(UUID.data,UUID.length),str(S("/~")),str(tinyurl->string_node.data,tinyurl->string_node.length),str(S("/"))) - url->data;
#else
			url->length=format_ios(url->data,str(S("http://")),str(host->data,host->length),str(S("/")),str(UUID.data,UUID.length),str(S("/~")),str(tinyurl->string_node.data,tinyurl->string_node.length),str(S("/"))) - url->data;
#endif
			url->data[url->length]=0;
			
			//calculate approximate length
			itemlen = 320 + title->string_node.length + title->string_node.length + 20 + protocolInfo->string_node.length + class->string_node.length + url->length;
			bodylen += itemlen;
			output_items[i].data = malloc(itemlen*sizeof(char)+1);
			//TODO escape the title
#if !defined(OS_IOS)
			output_items[i].length=format(output_items[i].data,str(S("&lt;item id=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" refID=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\"&gt;&lt;dc:title&gt;")),str(title->string_node.data,title->string_node.length),str(S("&lt;/dc:title&gt;&lt;upnp:class&gt;")),str(class->string_node.data,class->string_node.length),str(S("&lt;/upnp:class&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;")),str(S("&lt;res size=\"")),uint(fsize->integer),str(S("\" protocolInfo=\"")),str(protocolInfo->string_node.data,protocolInfo->string_node.length),str(S("\"&gt;")),str(url->data,url->length),str(S("&lt;/res&gt;&lt;/item&gt;"))) - output_items[i].data;
#else
			output_items[i].length=format_ios(output_items[i].data,str(S("&lt;item id=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" refID=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\"&gt;&lt;dc:title&gt;")),str(title->string_node.data,title->string_node.length),str(S("&lt;/dc:title&gt;&lt;upnp:class&gt;")),str(class->string_node.data,class->string_node.length),str(S("&lt;/upnp:class&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;")),str(S("&lt;res size=\"")),uint(fsize->integer),str(S("\" protocolInfo=\"")),str(protocolInfo->string_node.data,protocolInfo->string_node.length),str(S("\"&gt;")),str(url->data,url->length),str(S("&lt;/res&gt;&lt;/item&gt;"))) - output_items[i].data;
#endif
			output_items[i].data[output_items[i].length]=0;
			
			free(url);
		}
		
		
		
		if(!show_offline)
		{
		offline_buf.data = malloc(512*sizeof(char)+1);
		offline_buf.length=format(offline_buf.data,str(S("&lt;container id=\"")),uint(cmpObjId),str(S("_1\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" searchable=\"1\" childCount=\"55\"&gt;&lt;dc:title&gt;Offline Devices&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;"))) - offline_buf.data;
		}
		
		body_dyn = malloc((bodylen+offline_buf.length)*sizeof(char)+1);
		
		//init
		int body_ptrlen=sizeof("<u:SearchResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;\r\n")-1;
		memcpy(body_dyn,"<u:SearchResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;\r\n",body_ptrlen);
		
		if(!show_offline)
		{
		items_num++;
		memcpy(body_dyn+body_ptrlen,offline_buf.data,offline_buf.length);
		body_ptrlen+=offline_buf.length;
		free(offline_buf.data);
		}
		
		for(i=0;i<array->array_node.length;i++)
		{
			memcpy(body_dyn+body_ptrlen,output_items[i].data,output_items[i].length);
			body_ptrlen+=output_items[i].length;
		}
		
		char body_footer[200];
		bodylen = snprintf(body_footer, sizeof(body_footer),"&lt;/DIDL-Lite&gt;</Result>\r\n<NumberReturned>%d</NumberReturned>\r\n<TotalMatches>%d</TotalMatches>\r\n<UpdateID>%d</UpdateID></u:SearchResponse>",(int)array->array_node.length,(int)array->array_node.length,UpdateId+1);
		memcpy(body_dyn+body_ptrlen,body_footer,bodylen);
		body_ptrlen+=bodylen;
		
		
		//time for free some stuff
		for(i=0;i<array->array_node.length;i++)
		{
			free(output_items[i].data);
		}
		free(output_items);
		
		status = SoapSend(request, response, resources, body_dyn, body_ptrlen);
		
		free(body_dyn);
		if (cache) cache_finish(cache);
		return status;
			
	error_free_array:
	if(array)
	{
		for(i=0;i<array->array_node.length;i++)
		{
			free(output_items[i].data);
		}
			free(output_items);
	}		
	error:
		if (cache) cache_finish(cache);
			bodylen = snprintf(body, sizeof(body), "<u:SearchResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>0</NumberReturned><TotalMatches>0</TotalMatches><UpdateID>1</UpdateID></u:SearchResponse>");	
			return SoapSend(request, response, resources, body, bodylen);
}

int SABrowse(struct http_request *restrict request, struct http_response *restrict response, struct resources *restrict resources, const char * action)
{
	int status = 0;
	char key[CACHE_KEY_SIZE];

	struct string *url=0;

	union json *root = NULL, *item = NULL, *tmpitem = NULL, *array = NULL;
	struct string *output_items = 0;
	struct string tmpkey,catkey,friendly_name;
	struct string buffer;
	const struct cache *cache=0;
	char tinyurlkey[CACHE_KEY_SIZE];
	
	off_t length = content_length(&request->headers);
	if (length < 0) return LengthRequired;
	if (length >= BUFFER_SIZE_MAX) return LengthRequired; //TODO change the error
	
	struct string hkey = string("host");
	struct string *host = dict_get(&request->headers, &hkey);
	if (!host) return ERROR_MISSING;

	char body[8192];
	char *body_dyn=0;
	int bodylen=0;
	int i=0;
	char *ptr;
	char *ObjectID, *Filter, *BrowseFlag, *SortCriteria;
	int cmpObjId=-1;
	struct NameValueParserData data;
	int RequestedCount = 0;
	int StartingIndex = 0;

	if (status = dlna_key(key)) return status;
	
	if (status = stream_read(&resources->stream, &buffer, length)) return status;
		
	ParseNameValue(buffer.data, buffer.length, &data);
	
	stream_read_flush(&resources->stream, length);
	
	ObjectID = GetValueFromNameValueList(&data, "ObjectID");
	Filter = GetValueFromNameValueList(&data, "Filter");
	BrowseFlag = GetValueFromNameValueList(&data, "BrowseFlag");
	SortCriteria = GetValueFromNameValueList(&data, "SortCriteria");
	fprintf(stderr,"\n\n\n\nBrowseFlag %s ObjectID %s\n\n\n\n\n",BrowseFlag,ObjectID);
	fflush(stderr);
	if( (ptr = GetValueFromNameValueList(&data, "RequestedCount")) )
		RequestedCount = atoi(ptr);
	if( RequestedCount < 0 )
	{
		ClearNameValueList(&data);
		return SoapErr(request, response, resources, 402, "Invalid Args");
		
	}
	if( !RequestedCount )
		RequestedCount = -1;
	if( (ptr = GetValueFromNameValueList(&data, "StartingIndex")) )
		StartingIndex = atoi(ptr);
	if( StartingIndex < 0 )
	{
		ClearNameValueList(&data);
		return SoapErr(request, response, resources, 402, "Invalid Args");
		
	}
	if( !BrowseFlag || (strcmp(BrowseFlag, "BrowseDirectChildren") && strcmp(BrowseFlag, "BrowseMetadata")) )
	{
		ClearNameValueList(&data);
		return SoapErr(request, response, resources, 402, "Invalid Args");
		
	}
	if( !ObjectID && !(ObjectID = GetValueFromNameValueList(&data, "ContainerID")) )
	{
		ClearNameValueList(&data);
		return SoapErr(request, response, resources, 402, "Invalid Args");
	
	}

	cache = cache_use(key);
	root = (union json *)cache->value;
	if(!root) goto error;
	if(json_type(root)!=OBJECT)goto error;
		
		struct string parent_category;
		parent_category.data = ObjectID;
		parent_category.length = strlen(ObjectID);
		
		int curitem=-1;
		
		for(i=0;i<parent_category.length;i++)
		{
			if(ObjectID[i]=='$')
			{
				ObjectID[i]=0;
				curitem=strtol(ObjectID+i+1,NULL,10);
				parent_category.length = strlen(ObjectID);
				break;
			}
		}
		
		if(ObjectID)
		{
			if(*ObjectID=='1')
			{
				switch (*(ObjectID+1)) // ObjectID+1 will be 0 if we have 2 chars :) ,
				{
					case '4':
					cmpObjId=1;
					break;
					case '5':
					cmpObjId=2;
					break;
				}
			}
			else
			{
				switch (*ObjectID)
				{
					case '4':
					case '5':
					case '6':
					case '7':
					case 'F':
					cmpObjId=1;
					break;
					case '8':
					cmpObjId=2;
					break;
					case 'B':
					case 'C':
					cmpObjId=3;
					break;
				}
			}
			
			if(*(ObjectID+1)=='_')
			{
				switch (*ObjectID)
				{
					case '1':
					case '4':
					case '5':
					case '6':
					case '7':
					case 'F':
					cmpObjId=1;
					break;
					case '8':
					case '2':
					cmpObjId=2;
					break;
					case '3':
					case 'B':
					case 'C':
					cmpObjId=3;
					break;
				}
			}
			
			if(!strcmp(ObjectID, "1"))cmpObjId=1;
			if(!strcmp(ObjectID, "2"))cmpObjId=2;
			if(!strcmp(ObjectID, "3"))cmpObjId=3;
			if(!strcmp(ObjectID, "0") || !strcmp(ObjectID, "-1"))cmpObjId=0;
		}
		
		

			 if(cmpObjId == 1) { catkey = string("music"); friendly_name = string("Music"); }//parent_category = string("1") }
		else if(cmpObjId == 2) { catkey = string("videos"); friendly_name = string("Video"); }//parent_category = string("2") }
		else if(cmpObjId == 3) { catkey = string("photos"); friendly_name = string("Pictures"); }//parent_category = string("3") }
		else if(cmpObjId != 0) goto error;

	if(!strcmp(BrowseFlag+6, "Metadata") && !cmpObjId)
	{
		bodylen = snprintf(body, sizeof(body), "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;&lt;container id=\"0\" parentID=\"-1\" restricted=\"1\" childCount=\"4\"&gt;&lt;dc:title&gt;root&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>1</NumberReturned><TotalMatches>1</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse>");	
		
		if (cache) cache_finish(cache);
		return SoapSend(request, response, resources, body, bodylen);
	}
	else if( !cmpObjId )
	{	
		catkey = string("music");
		array=dict_get(root->object, &catkey);
		int music_len = 0;
		if(!array) music_len = 0;
		else  music_len = array->array_node.length;
		
		catkey = string("videos");
		array=dict_get(root->object, &catkey);
		int videos_len = 0;
		if(!array) videos_len = 0;
		else  videos_len = array->array_node.length;
		
		catkey = string("photos");
		array=dict_get(root->object, &catkey);
		int photos_len = 0;
		if(!array) photos_len = 0;
		else  photos_len = array->array_node.length;

		bodylen = snprintf(body, sizeof(body), "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;\r\n&lt;container id=\"1\" parentID=\"0\" restricted=\"1\" searchable=\"1\" childCount=\"%d\"&gt;&lt;dc:title&gt;Music&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;&lt;container id=\"3\" parentID=\"0\" restricted=\"1\" searchable=\"1\" childCount=\"%d\"&gt;&lt;dc:title&gt;Pictures&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;&lt;container id=\"2\" parentID=\"0\" restricted=\"1\" searchable=\"1\" childCount=\"%d\"&gt;&lt;dc:title&gt;Video&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;&lt;/DIDL-Lite&gt;</Result>\r\n<NumberReturned>3</NumberReturned>\r\n<TotalMatches>3</TotalMatches>\r\n<UpdateID>1</UpdateID></u:BrowseResponse>",music_len,photos_len,videos_len);	 

		if (cache) cache_finish(cache);
		return SoapSend(request, response, resources, body, bodylen);
	}
	else
	{
		array=dict_get(root->object, &catkey);
		if(!array) goto error;
		if(json_type(array)!=ARRAY)goto error;
		
		if(!strcmp(BrowseFlag+6, "Metadata") && !cmpObjId) //TODO make it better
		{
			bodylen = snprintf(body, sizeof(body), "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;&lt;container id=\"%s\" parentID=\"0\" restricted=\"1\" childCount=\"%d\"&gt;&lt;dc:title&gt;%s&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>1</NumberReturned><TotalMatches>1</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse>",ObjectID,(int)array->array_node.length,friendly_name.data);
			if (cache) cache_finish(cache);
			return SoapSend(request, response, resources, body, bodylen);
		}
	
		bodylen = 512;	
		int itemlen = 0;
		union json *title,*fsize,*protocolInfo,*class,*tinyurl;
		//TODO: allocate memory only for the online ones
		output_items=calloc(array->array_node.length+1,sizeof(struct string )); // this way we are able to free part of the output items on error
		int show_offline=0;
		struct string offline_buf;
		offline_buf.length=0;
		int items_num=0;

		if(*(ObjectID+1)=='_' || curitem>-1)show_offline=1;
		
		for(i=0;i<array->array_node.length;i++)
		{
		//&lt;item id=\"1$4$1\" parentID=\"1$4\" restricted=\"1\" refID=\"64$1\"&gt;&lt;dc:title&gt;Use Somebody (Armin van Buuren Rework)&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.audioItem.musicTrack&lt;/upnp:class&gt;&lt;dc:description&gt;Excellent&lt;/dc:description&gt;&lt;dc:creator&gt;Laura Jansen&lt;/dc:creator&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;&lt;upnp:artist&gt;Various Artists&lt;/upnp:artist&gt;&lt;upnp:album&gt;Sense Of Trance #32&lt;/upnp:album&gt;&lt;upnp:genre&gt;Trance&lt;/upnp:genre&gt;&lt;upnp:originalTrackNumber&gt;32&lt;/upnp:originalTrackNumber&gt;&lt;res size=\"20464617\" duration=\"0:08:20.848\" bitrate=\"320000\" sampleFrequency=\"44100\" nrAudioChannels=\"2\" protocolInfo=\"http-get:*:audio/mpeg:DLNA.ORG_PN=MP3;DLNA.ORG_OP=01;DLNA.ORG_CI=0;DLNA.ORG_FLAGS=01700000000000000000000000000000\"&gt;http://192.168.2.29:4080/b9dd96c7cbf3468f08b2f1f1cdfb2242/?%%7B%%22auth_id%%22%%3A%%22aw1YiM04Z98QZQxc%%22%%2C%%22actions%%22%%3A%%7B%%22ffs.download%%22%%3A%%5B%%7B%%22block_id%%22%%3A1%%2C%%22path%%22%%3A%%22%%22%%7D%%5D%%7D%%7D&lt;/res&gt;&lt;/item&gt;
			

			if(curitem>-1)
			{
			i=curitem;
			item=vector_get(&array->array_node,i);
			}
			else
			{
			item=vector_get(&array->array_node,i);
			tmpkey = string("host");
				if(!show_offline && !dict_get(item->object, &tmpkey) )continue;
				else if(show_offline && dict_get(item->object, &tmpkey) )continue;
			}
			
				
			items_num++;
			
			tmpkey = string("name");
			title=dict_get(item->object, &tmpkey);
			if(!title)goto error_free_array;
			if(json_type(title)!=STRING)goto error_free_array;
			
			tmpkey = string("size");
			fsize=dict_get(item->object, &tmpkey);
			if(!fsize)fsize=json_integer(0);
			if(json_type(fsize)!=INTEGER)fsize=json_integer(0);
			//if(!fsize)goto error_free_array;
			//if(json_type(fsize)!=INTEGER)goto error_free_array;
			
			tmpkey = string("class");
			class=dict_get(item->object, &tmpkey);
			if(!class)goto error_free_array;
			if(json_type(class)!=STRING)goto error_free_array;

			tmpkey = string("protocolInfo");
			protocolInfo=dict_get(item->object, &tmpkey);
			if(!protocolInfo)goto error_free_array;
			if(json_type(protocolInfo)!=STRING)goto error_free_array;
			
			tmpkey = string("tinyurl");
			tinyurl=dict_get(item->object, &tmpkey);
			if(!tinyurl)
			{
				url=string_alloc(0,224+UUID.length+6);
				if(!url) goto error;
#if !defined(OS_IOS)
				url->length=format(url->data,str(S("/?{%22actions%22:{%22proxy.forward%22:{%22category%22:%22")),str(catkey.data,catkey.length),str(S("%22%2C%22id%22:")),uint(i),str(S("}}%2C%22protocol%22:{%22name%22:%22remoteJson%22%2C%22function%22:%22kernel.sockets.receiveJson%22%2C%22request_id%22:%2214%22}}"))) - url->data;
#else
				url->length=format_ios(url->data,str(S("/?{%22actions%22:{%22proxy.forward%22:{%22category%22:%22")),str(catkey.data,catkey.length),str(S("%22%2C%22id%22:")),uint(i),str(S("}}%2C%22protocol%22:{%22name%22:%22remoteJson%22%2C%22function%22:%22kernel.sockets.receiveJson%22%2C%22request_id%22:%2214%22}}"))) - url->data;
#endif
				
				tmpitem = json_string_old(url);
				free(url);
				if (!tmpitem) goto error;
				
				if (!cache_create(tinyurlkey, CACHE_URL, tmpitem, 0))
				{
					json_free(tmpitem);
					goto error;
				}
				
				root=vector_get(&array->array_node,i);
				if(json_type(root)!=OBJECT)goto error;
				
				tmpkey = string(tinyurlkey, CACHE_KEY_SIZE);
				tinyurl = json_string_old(&tmpkey);
				if(!tinyurl) goto error;
				
				tmpkey = string("tinyurl");
				json_object_insert_old(item,&tmpkey,tinyurl);
			}
			if(json_type(tinyurl)!=STRING)goto error_free_array;

#define S(s) s, sizeof(s) - 1
			
			//example URL 
			url=string_alloc(0,16+CACHE_KEY_SIZE+host->length+UUID.length + title->string_node.length+1);
#if !defined(OS_IOS)
			url->length=format(url->data,str(S("http://")),str(host->data,host->length),str(S("/")),str(UUID.data,UUID.length),str(S("/~")),str(tinyurl->string_node.data,tinyurl->string_node.length),str(S("/"))) - url->data;
#else
			url->length=format_ios(url->data,str(S("http://")),str(host->data,host->length),str(S("/")),str(UUID.data,UUID.length),str(S("/~")),str(tinyurl->string_node.data,tinyurl->string_node.length),str(S("/"))) - url->data;
#endif
			url->data[url->length]=0;
			
			//calculate approximate length
			itemlen = 320 + title->string_node.length + title->string_node.length + 20 + protocolInfo->string_node.length + class->string_node.length + url->length + 30 ;
			bodylen += itemlen;
			output_items[i].data = malloc(itemlen*sizeof(char)+1);
			//TODO escape the title
#if !defined(OS_IOS)
			output_items[i].length=format(output_items[i].data,str(S("&lt;item id=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" refID=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\"&gt;&lt;dc:title&gt;")),str(title->string_node.data,title->string_node.length),str(S("&lt;/dc:title&gt;&lt;upnp:class&gt;")),str(class->string_node.data,class->string_node.length),str(S("&lt;/upnp:class&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;")),str(S("&lt;res size=\"")),uint(fsize->integer),str(S("\" protocolInfo=\"")),str(protocolInfo->string_node.data,protocolInfo->string_node.length),str(S("\"&gt;")),str(url->data,url->length),str(S("&lt;/res&gt;&lt;/item&gt;"))) - output_items[i].data;
#else
			output_items[i].length=format_ios(output_items[i].data,str(S("&lt;item id=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" refID=\"")),str(parent_category.data,parent_category.length),str(S("$")),uint(i),str(S("\"&gt;&lt;dc:title&gt;")),str(title->string_node.data,title->string_node.length),str(S("&lt;/dc:title&gt;&lt;upnp:class&gt;")),str(class->string_node.data,class->string_node.length),str(S("&lt;/upnp:class&gt;&lt;dc:date&gt;2012-01-01&lt;/dc:date&gt;")),str(S("&lt;res size=\"")),uint(fsize->integer),str(S("\" protocolInfo=\"")),str(protocolInfo->string_node.data,protocolInfo->string_node.length),str(S("\"&gt;")),str(url->data,url->length),str(S("&lt;/res&gt;&lt;/item&gt;"))) - output_items[i].data;
#endif
			//output_items[i].length=format(output_items[i].data,str(S(";&lt;item id=\"2$15$5\" parentID=\"2$15\" restricted=\"1\"&gt;&lt;dc:title&gt;test1080p&lt;/dc:title&gt;&lt;upnp:class&gt;object.item.videoItem&lt;/upnp:class&gt;&lt;res duration=\"0:01:36.429\" bitrate=\"46864\" protocolInfo=\"http-get:*:video/avi:*\"&gt;")),str(S("http://192.168.2.29:4070/62a98398/1f6739748c6a99a672096297/?as%3Adasd{asdasd{pro2s}}&lt;/res&gt;&lt;/item&gt;"))) - output_items[i].data; 

			output_items[i].data[output_items[i].length]=0;
			 
			free(url);
			
			if(curitem>-1)break;
		} 

		
		//init
		
		if(!show_offline)
		{
		offline_buf.data = malloc(512*sizeof(char)+1);
		offline_buf.length=format(offline_buf.data,str(S("&lt;container id=\"")),uint(cmpObjId),str(S("_1\" parentID=\"")),str(parent_category.data,parent_category.length),str(S("\" restricted=\"1\" searchable=\"1\" childCount=\"55\"&gt;&lt;dc:title&gt;Offline Devices&lt;/dc:title&gt;&lt;upnp:class&gt;object.container.storageFolder&lt;/upnp:class&gt;&lt;upnp:storageUsed&gt;-1&lt;/upnp:storageUsed&gt;&lt;/container&gt;"))) - offline_buf.data;
		}
		
		body_dyn = malloc((bodylen+offline_buf.length)*sizeof(char)+1);
		
		int body_ptrlen=sizeof("<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;\r\n")-1;
		memcpy(body_dyn,"<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\" xmlns:dlna=\"urn:schemas-dlna-org:metadata-1-0/\"&gt;\r\n",body_ptrlen);
		
		if(!show_offline)
		{
		items_num++;
		memcpy(body_dyn+body_ptrlen,offline_buf.data,offline_buf.length);
		body_ptrlen+=offline_buf.length;
		free(offline_buf.data);
		}
		
		for(i=0;i<array->array_node.length;i++)
		{	
			memcpy(body_dyn+body_ptrlen,output_items[i].data,output_items[i].length);
			body_ptrlen+=output_items[i].length;
		}
		
		char body_footer[200];
		bodylen = snprintf(body_footer, sizeof(body_footer),"&lt;/DIDL-Lite&gt;</Result>\r\n<NumberReturned>%d</NumberReturned>\r\n<TotalMatches>%d</TotalMatches>\r\n<UpdateID>%d</UpdateID></u:BrowseResponse>",(int)items_num,(int)items_num,UpdateId+1);
		memcpy(body_dyn+body_ptrlen,body_footer,bodylen);
		body_ptrlen+=bodylen;

		//time for free some stuff
		for(i=0;i<array->array_node.length;i++)
		{
			free(output_items[i].data);
		}
		free(output_items);
		
		status = SoapSend(request, response, resources, body_dyn, body_ptrlen);
		
		free(body_dyn);
		
		if (cache) cache_finish(cache);
		return status;
	}

error_free_array:
	if(array)
	{
		for(i=0;i<array->array_node.length;i++)
		{
			free(output_items[i].data);
		}
			free(output_items);
	}
error:
	if (cache) cache_finish(cache);
	bodylen = snprintf(body, sizeof(body), "<u:BrowseResponse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\"><Result>&lt;DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"&gt;&lt;/DIDL-Lite&gt;</Result><NumberReturned>0</NumberReturned><TotalMatches>0</TotalMatches><UpdateID>1</UpdateID></u:BrowseResponse>");	
	return SoapSend(request, response, resources, body, bodylen);
}
