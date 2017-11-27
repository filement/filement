/* $Id: minissdp.c,v 1.56 2014/02/01 16:35:37 nanard Exp $ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2013 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */

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
#include <Winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include "mingw.h"
#define in_addr_t uint32_t
#include <stdint.h>
#endif

#include <pthread.h>				// libpthread
 
#include "types.h"
#include "log.h"

#if !defined(OS_WINDOWS)
#include "protocol.h"
# include "cache.h"
# include "storage.h"
# include "remote.h"
#else
#include "../protocol.h"
#include "../cache.h"
#include "../storage.h"
#include "../remote.h"
#define syslog(...)
#endif
 
#ifndef   NI_MAXHOST
#define   NI_MAXHOST 1025
#endif

#include "ssdp.h"
#include "upnpdescgen.h"

/* SSDP ip/port */
#define SSDP_PORT (1900)
#define SSDP_MCAST_ADDR ("239.255.255.250")
#define LL_SSDP_MCAST_ADDR "FF02::C"
#define SL_SSDP_MCAST_ADDR "FF05::C"


extern char uuidvalue[];
  
#ifdef OS_WINDOWS
extern struct string UUID_WINDOWS;
#undef UUID
#define UUID UUID_WINDOWS
#else
extern struct string UUID;
#endif

#if !defined(IFF_SLAVE)
# define IFF_SLAVE 0
#endif

int DLNApipe[2]={0,0};
extern volatile int DLNA;
static pthread_mutex_t DLNAmutex = PTHREAD_MUTEX_INITIALIZER;

#if defined(OS_WINDOWS)
int sudp=0;
#endif

void *pthread_upnp_dlna(void *arg)
{
	int pipe = *(int *)arg;
	SSDPDThread(pipe);
}

int DLNAisEnabled(void)
{
	pthread_mutex_lock (&DLNAmutex);
	int test_enabled = DLNA;
	pthread_mutex_unlock(&DLNAmutex);
	return test_enabled;
}

void DLNAEnable(void *storage)
{
	pthread_mutex_lock(&DLNAmutex);

#if !defined(OS_WINDOWS)
	if (!DLNApipe[0]) pipe(DLNApipe);
#endif

	dlna_init();
	DLNA = 1;

	pthread_t dlna_thread;
	pthread_create(&dlna_thread, NULL, pthread_upnp_dlna, &DLNApipe[1]);
	
	struct string key = string("DLNA"), value = string("1");
	if (!storage_local_settings_set_value(storage, &key, &value))
		warning(logs("DB DLNA value write error"));

	pthread_mutex_unlock(&DLNAmutex);
}

void DLNADisable(void *storage)
{
	pthread_mutex_lock(&DLNAmutex);

#if !defined(OS_WINDOWS)
	write(DLNApipe[0], "q", 1);
#else
	if (sudp)
	{
		CLOSE(sudp);
		sudp = 0;
	}
	else
	{
		pthread_mutex_unlock(&DLNAmutex);
		return;
	}
#endif

	struct string key = string("DLNA"), value = string("0");
	if (!storage_local_settings_set_value(storage, &key, &value))
		warning(logs("DB DLNA value write error"));

	dlna_term();
	DLNA = 0;

	pthread_mutex_unlock(&DLNAmutex);
}




static struct lan_addr *
get_lan_for_peer(const struct sockaddr * peer, struct lan_addrs *lanaddrsptr)
{
    struct lan_addr * lan_addr = NULL;
	int i;
#ifdef ENABLE_IPV6
    if(peer->sa_family == AF_INET6)
    {
        struct sockaddr_in6 * peer6 = (struct sockaddr_in6 *)peer;
        if(IN6_IS_ADDR_V4MAPPED(&peer6->sin6_addr))
        {
            struct in_addr peer_addr;
            memcpy(&peer_addr, &peer6->sin6_addr.s6_addr[12], 4);
            for(lan_addr = lan_addrs.lh_first;
                lan_addr != NULL;
                lan_addr = lan_addr->list.le_next)
            {
                if( (peer_addr.s_addr & lan_addr->mask.s_addr)
                   == (lan_addr->addr.s_addr & lan_addr->mask.s_addr))
                    break;
            }
        }
        else
        {
            int index = -1;
            if(peer6->sin6_scope_id > 0)
                index = (int)peer6->sin6_scope_id;
            else
            {
                if(get_src_for_route_to(peer, NULL, NULL, &index) < 0)
                    return NULL;
            }
          //  syslog(LOG_DEBUG, "%s looking for LAN interface index=%d",
           //        "get_lan_for_peer()", index);
            for(lan_addr = lan_addrs.lh_first;
                lan_addr != NULL;
                lan_addr = lan_addr->list.le_next)
            {
                //syslog(LOG_DEBUG,
                //       "ifname=%s index=%u str=%s addr=%08x mask=%08x",
                //       lan_addr->ifname, lan_addr->index,
                 //      lan_addr->str,
                //       ntohl(lan_addr->addr.s_addr),
                //       ntohl(lan_addr->mask.s_addr));
                if(index == (int)lan_addr->index)
                    break;
            }
        }
    }
    else if(peer->sa_family == AF_INET)
    {
#endif
        for(i = 0 ; i < lanaddrsptr->count; i++)
		{
            if( (((const struct sockaddr_in *)peer)->sin_addr.s_addr & lanaddrsptr->lan_addr[i].mask.s_addr)
               == (lanaddrsptr->lan_addr[i].addr.s_addr & lanaddrsptr->lan_addr[i].mask.s_addr))
                goto found;
        }
		i=0;
#ifdef ENABLE_IPV6
    }
#endif

    found:
        
    return &lanaddrsptr->lan_addr[i];
}


static inline int set_non_blocking(int fd)
{
#if !defined(OS_WINDOWS)
    int flags = fcntl(fd, F_GETFL);
    if(flags < 0)
        return 0;
    if(fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
        return 0;
#endif
    return 1;
}

static int
sockaddr_to_string(const struct sockaddr * addr, char * str, size_t size)
{
    char buffer[64];
    unsigned short port = 0;
    int n = -1;

    switch(addr->sa_family)
    {
    case AF_INET6:
        inet_ntop(addr->sa_family,
                  &((struct sockaddr_in6 *)addr)->sin6_addr,
                  buffer, sizeof(buffer));
        port = ntohs(((struct sockaddr_in6 *)addr)->sin6_port);
        n = snprintf(str, size, "[%s]:%hu", buffer, port);
        break;
    case AF_INET:
        inet_ntop(addr->sa_family,
                  &((struct sockaddr_in *)addr)->sin_addr,
                  buffer, sizeof(buffer));
        port = ntohs(((struct sockaddr_in *)addr)->sin_port);
        n = snprintf(str, size, "%s:%hu", buffer, port);
        break;
#ifdef AF_LINK
#if defined(__sun)
        /* solaris does not seem to have link_ntoa */
        /* #define link_ntoa _link_ntoa */
#define link_ntoa(x) "dummy-link_ntoa"
#endif
    /*case AF_LINK:
        {
            struct sockaddr_dl * sdl = (struct sockaddr_dl *)addr;
            n = snprintf(str, size, "index=%hu type=%d %s",
                         sdl->sdl_index, sdl->sdl_type,
                         link_ntoa(sdl));
        }
        break;*/
#endif
    default:
        n = snprintf(str, size, "unknown address family %d", addr->sa_family);
#if 0
        n = snprintf(str, size, "unknown address family %d "
                     "%02x %02x %02x %02x %02x %02x %02x %02x",
                     addr->sa_family,
                     addr->sa_data[0], addr->sa_data[1], (unsigned)addr->sa_data[2], addr->sa_data[3],
                     addr->sa_data[4], addr->sa_data[5], (unsigned)addr->sa_data[6], addr->sa_data[7]);
#endif
    }
    return n;
}

void SSDPDGetIFs_free(struct lan_addrs *lanaddrsptr)
{
int i=0;
for(;i<lanaddrsptr->count;i++)
{
	free(lanaddrsptr->lan_addr[i].host);
}
free(lanaddrsptr);
}

#if !defined(OS_WINDOWS)

struct lan_addrs *SSDPDGetIFs(void)
{
struct ifaddrs *ifaddr=0, *ifa;
int family, s;
int i=0;
struct lan_addrs *lanaddrsptr=0;
char host[NI_MAXHOST];

if (getifaddrs(&ifaddr) == -1) {
               return 0;
           }
		   
 for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
			      if (ifa->ifa_addr == NULL)
                   continue;
				   
			   if (ifa->ifa_flags & (IFF_LOOPBACK | IFF_SLAVE))
				   continue;
				   
			   if (ifa->ifa_addr->sa_family != AF_INET)
			       continue;
				   
				   i++;
                   
}
if(!i)return 0;

lanaddrsptr=malloc(sizeof(struct lan_addrs)+sizeof(struct lan_addr)*i);
lanaddrsptr->count=i;

 for (i=0,ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
               if (ifa->ifa_addr == NULL)
                   continue;
				   
			   if (ifa->ifa_flags & (IFF_LOOPBACK | IFF_SLAVE))
				   continue;
				   
			   if (ifa->ifa_addr->sa_family != AF_INET)
			       continue;


               //if (family == AF_INET || family == AF_INET6) {
			   
                   s = getnameinfo(ifa->ifa_addr,
                           (ifa->ifa_addr->sa_family == AF_INET) ? sizeof(struct sockaddr_in) :
                                                 sizeof(struct sockaddr_in6),
                           host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
                   if (s != 0) {
                       {freeifaddrs(ifaddr);return 0;}
                   }
				   
				   lanaddrsptr->lan_addr[i].addr = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
				   lanaddrsptr->lan_addr[i].mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr;
				   lanaddrsptr->lan_addr[i].host = strdup(host); 
				  
				   
				   i++; 
               
           }

if(ifaddr)freeifaddrs(ifaddr);
return lanaddrsptr;
}
#else 

struct lan_addrs *SSDPDGetIFs(void)
{
int i,u=0;
PMIB_IPADDRTABLE pIPAddrTable;
DWORD dwSize = 0;
DWORD dwRetVal = 0;
IN_ADDR IPAddr;
LPVOID lpMsgBuf;
struct lan_addrs *lanaddrsptr=0;


 pIPAddrTable = (MIB_IPADDRTABLE *) malloc(sizeof (MIB_IPADDRTABLE));

    if (pIPAddrTable) {
        // Make an initial call to GetIpAddrTable to get the
        // necessary size into the dwSize variable
        if (GetIpAddrTable(pIPAddrTable, &dwSize, 0) ==
            ERROR_INSUFFICIENT_BUFFER) {
            free(pIPAddrTable);
            pIPAddrTable = (MIB_IPADDRTABLE *) malloc(dwSize);

        }
        if (pIPAddrTable == NULL) {
            printf("Memory allocation failed for GetIpAddrTable\n");
            return 0;
        }
    }
    // Make a second call to GetIpAddrTable to get the
    // actual data we want
    if ( (dwRetVal = GetIpAddrTable( pIPAddrTable, &dwSize, 0 )) != NO_ERROR ) { 
        printf("GetIpAddrTable failed with error %d\n", dwRetVal);
        if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwRetVal, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),       // Default language
                          (LPTSTR) & lpMsgBuf, 0, NULL)) {
            printf("\tError: %s", lpMsgBuf);
            LocalFree(lpMsgBuf);
        }
		return 0;
    }
u=(int) pIPAddrTable->dwNumEntries;
for (i=0; i < (int) pIPAddrTable->dwNumEntries; i++) {
	if((u_long)pIPAddrTable->table[i].dwAddr == 16777343)u--;
}
	
lanaddrsptr=malloc(sizeof(struct lan_addrs)+sizeof(struct lan_addr)*u);
lanaddrsptr->count=u;

	for (i=0,u=0; i < (int) pIPAddrTable->dwNumEntries; i++) {
	if((u_long)pIPAddrTable->table[i].dwAddr == 16777343)continue;

	IPAddr.S_un.S_addr = (u_long) pIPAddrTable->table[i].dwAddr;
	lanaddrsptr->lan_addr[u].host = strdup(inet_ntoa(IPAddr)); 
	lanaddrsptr->lan_addr[u].addr.s_addr = IPAddr.S_un.S_addr;

	IPAddr.S_un.S_addr = (u_long) pIPAddrTable->table[i].dwMask;
	lanaddrsptr->lan_addr[u].mask.s_addr = IPAddr.S_un.S_addr;

	u++;
	}

if (pIPAddrTable)free(pIPAddrTable);
	
return lanaddrsptr;
}

#if defined(WINDOWS_IPV6)
void inet_get_ip_mask(int af, const void *src, PIP_ADAPTER_PREFIX adapter_prefix, struct in_addr *addr, struct in_addr *maskptr)
{
	if (af == AF_INET)
	{
		struct sockaddr_in srcaddr;

		memset(&srcaddr, 0, sizeof(struct sockaddr_in));
		memcpy(addr, &((struct sockaddr_in *)src)->sin_addr, sizeof(srcaddr.sin_addr));
		
		PIP_ADAPTER_PREFIX adapter_prefix_tmp=0;
		unsigned int mask =0 ;
		while (adapter_prefix) 
		{
			//if(adapter_prefix->Address.lpSockaddr.sa_family == AF_INET )
			{
				if(adapter_prefix->PrefixLength==32 || !adapter_prefix->PrefixLength)
				{
				adapter_prefix=adapter_prefix->Next;
				continue;
				}
				
				
				mask = UINT32_MAX - ((1<<(32-adapter_prefix->PrefixLength))-1);
				
				if( ((struct sockaddr_in *)&adapter_prefix->Address.lpSockaddr)->sin_addr.s_addr && ( (((struct sockaddr_in *)&adapter_prefix->Address.lpSockaddr)->sin_addr.s_addr & mask) == ((struct sockaddr_in *)&adapter_prefix->Address.lpSockaddr)->sin_addr.s_addr ))
				{
					if(!adapter_prefix_tmp || (adapter_prefix_tmp->PrefixLength > adapter_prefix->PrefixLength ))
					{
						adapter_prefix_tmp=adapter_prefix;
						((struct sockaddr_in *)&adapter_prefix_tmp->Address.lpSockaddr)->sin_addr.s_addr=mask;
					}
				}
				
			}
		
		adapter_prefix=adapter_prefix->Next;
		}
		
		if(adapter_prefix_tmp)
		{
		memcpy(maskptr, &((struct sockaddr_in *)adapter_prefix_tmp->Address.lpSockaddr)->sin_addr, sizeof(srcaddr.sin_addr));
		}
		
		/*
		char buf[16];
		uint32_t p = ~((1 << (32 - prefix)) - 1);
		sprintf(buf,"%u.%u.%u.%u", p >> 24, (p >> 16) & 0xff, (p >> 8) & 0xff, p & 0xff);
		inet_aton(buf, maskptr);
		*/

	}
	//TODO IPV6
	/*
	else if (af == AF_INET6)
	{
			struct sockaddr_in6 in;
			memset(&in, 0, sizeof(in));
			in.sin6_family = AF_INET6;
			memcpy(&in.sin6_addr, &((struct sockaddr_in6 *)src)->sin6_addr, sizeof(struct in_addr6));
			
	}
	*/
}

struct lan_addrs *SSDPDGetIFs(void)
{
/*
struct ifaddrs *ifaddr=0, *ifa;
int family, s;
int i=0;
struct lan_addrs *lanaddrsptr=0;
char host[NI_MAXHOST];

if (getifaddrs(&ifaddr) == -1) {
               return 0;
           }
*/

struct lan_addrs *lanaddrsptr=0;

char str_buffer[128];
    /* Declare and initialize variables */
	  WSADATA wsaData = {0};
    int iResult = 0;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        return 0;
    }
	
    DWORD dwSize = 0;
    DWORD dwRetVal = 0;

    unsigned int i = 0;


    // default to unspecified address family (both)
	#ifdef ENABLE_IPV6
    ULONG family = AF_UNSPEC;
	#else
	ULONG family = AF_INET;
	#endif

    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    ULONG outBufLen = 0;
    ULONG Iterations = 0;

    PIP_ADAPTER_ADDRESSES pCurrAddresses = NULL;
    PIP_ADAPTER_UNICAST_ADDRESS pUnicast = NULL;
  

    // Allocate a 15 KB buffer to start with.
    outBufLen = 15000;
	
	   do {

        pAddresses = (IP_ADAPTER_ADDRESSES *) malloc(outBufLen);
        if (pAddresses == NULL) {
           return 0;
        }

        dwRetVal =
            GetAdaptersAddresses(family, GAA_FLAG_SKIP_ANYCAST | 
        GAA_FLAG_SKIP_MULTICAST | 
        GAA_FLAG_SKIP_DNS_SERVER |
		GAA_FLAG_INCLUDE_PREFIX |
        GAA_FLAG_SKIP_FRIENDLY_NAME, NULL, pAddresses, &outBufLen);

        if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
            free(pAddresses);
            pAddresses = NULL;
        } else {
            break;
        }

        Iterations++;

    } while ((dwRetVal == ERROR_BUFFER_OVERFLOW) && (Iterations < 4));
	
	
	
		    if (dwRetVal == NO_ERROR) {
        // If successful, output some information from the data we received
		
		Iterations=0;
		pCurrAddresses = pAddresses;
		while (pCurrAddresses) 
		{
			 pUnicast = pCurrAddresses->FirstUnicastAddress;
				if (pUnicast != NULL) {
					while ( pUnicast != NULL )
					{
						if(pUnicast->Address.lpSockaddr->sa_family == 2)Iterations++;
						pUnicast = pUnicast->Next;
					}
				}
				pCurrAddresses = pCurrAddresses->Next;
		}
		
		lanaddrsptr=malloc(sizeof(struct lan_addrs)+sizeof(struct lan_addr)*Iterations);
		lanaddrsptr->count=Iterations;
		i=0;
        pCurrAddresses = pAddresses;
        while (pCurrAddresses) {

            pUnicast = pCurrAddresses->FirstUnicastAddress;
			
            if (pUnicast != NULL) {
                while ( pUnicast != NULL )
				{
					family = pUnicast->Address.lpSockaddr->sa_family;
					if(family == 2)//TODO IPv6 for now only IPv4
					{
						memset(str_buffer,0,128);
						inet_ntop(family, &((struct sockaddr_in *)pUnicast->Address.lpSockaddr)->sin_addr, str_buffer, 128);
						
					   inet_get_ip_mask(family, pUnicast->Address.lpSockaddr, pCurrAddresses->FirstPrefix, &(lanaddrsptr->lan_addr[i].addr), &(lanaddrsptr->lan_addr[i].mask));
					   lanaddrsptr->lan_addr[i].host = strdup(str_buffer); 
					   i++;
					}

                    pUnicast = pUnicast->Next;
				}
            }

				

            pCurrAddresses = pCurrAddresses->Next;
        }
    } else {
		if (pAddresses) {
					free(pAddresses);
					}
		return 0;
                 
    }
	


if (pAddresses) {
free(pAddresses);
}
		   
return lanaddrsptr;
}
#endif

#endif

void SSDPDThread(int ctrlfd)
{

#if defined(OS_WINDOWS)
	#define PFDCOUNT 1
#else
	#ifndef ENABLE_IPV6
	#define PFDCOUNT 2
	#else
	#define PFDCOUNT 3
	#endif
#endif

struct pollfd pfd[PFDCOUNT];
char qbuf;
int i;
int * snotify = NULL;
#if !defined(OS_WINDOWS)
int sudp=0;
#endif
struct lan_addrs *lanaddrsptr=0;
int quitting=0;
time_t lasttime=0;
time_t curtime=0;
	syslog(LOG_NOTICE, "SSDP test syslog.");

int pfdc=0;	
	
	//adding controlling fd
	//int ctrlfd = open("/dev/null", O_RDONLY);
	//int ctrlfd=65000;
	#if !defined(OS_WINDOWS)
	pfd[pfdc].events = POLLIN;
	pfd[pfdc].revents = 0;
	pfd[pfdc].fd=ctrlfd;
	pfdc++;
	#endif
	
	/* sets the UUID */
	memcpy(uuidvalue+5,UUID.data,8);
	memcpy(uuidvalue+5+9,UUID.data+8,4);
	memcpy(uuidvalue+5+14,UUID.data+12,4);
	memcpy(uuidvalue+5+19,UUID.data+16,4);
	memcpy(uuidvalue+5+24,UUID.data+20,12);
	

	lanaddrsptr=SSDPDGetIFs();
    if(!lanaddrsptr || !lanaddrsptr->count)
	{
	syslog(LOG_NOTICE, "Can't get the network interfaces.");
		
				return ;
	}
	
#ifndef ENABLE_IPV6
		snotify = calloc(lanaddrsptr->count, sizeof(int));
#else
		/* one for IPv4, one for IPv6 */
		snotify = calloc(lanaddrsptr->count * 2, sizeof(int));
#endif
	/* open socket for SSDP connections */
    //TODO to check this one !!!! In error tries to SubmitServicesToMiniSSDPD, I suppose that I have to call SUbmitServicesToMiniSSDD when the socket is opened on
		sudp = OpenAndConfSSDPReceiveSocket(0,lanaddrsptr);
		if(sudp < 0)
		{
			syslog(LOG_NOTICE, "Failed to open socket for receiving SSDP. Trying to use MiniSSDPd");
		
				return ;
			
		}
		pfd[pfdc].events = POLLIN;
		pfd[pfdc].revents = 0;
		pfd[pfdc].fd=sudp;
		pfdc++;
		
#ifdef ENABLE_IPV6
		sudpv6 = OpenAndConfSSDPReceiveSocket(1);
		if(sudpv6 < 0)
		{
			syslog(LOG_WARNING, "Failed to open socket for receiving SSDP (IP v6).");
		}
		pfd[pfdc].events = POLLIN;
		pfd[pfdc].revents = 0;
		pfd[pfdc].fd=sudpv6;
		pfdc++
#endif

		/* open socket for sending notifications */
		if(OpenAndConfSSDPNotifySockets(snotify,lanaddrsptr) < 0)
		{
			syslog(LOG_ERR, "Failed to open sockets for sending SSDP notify "
		                "messages. EXITING");
			return ;
		}
		for(i=0;i<2;i++)
		{
	#ifndef ENABLE_IPV6
			if(SendSSDPGoodbye(snotify, lanaddrsptr->count) < 0)
	#else
			if(SendSSDPGoodbye(snotify, lanaddrsptr->count * 2) < 0)
	#endif
			{
				syslog(LOG_ERR, "Failed to broadcast good-bye notifications");
			}
		}
	
		while(!quitting)
		{
			/* Check if we need to send SSDP NOTIFY messages and do it if
			 * needed */
			time(&curtime);
			
				/* the comparaison is not very precise but who cares ? */
				if( curtime > lasttime + SSDPnotify_interval )
				{
						for(i=0;i<2;i++)
						{
						SendSSDPNotifies2(snotify,
									  SSDPport,
									  SSDPnotify_interval*2, UUID.data, lanaddrsptr);
						
						}
						time(&lasttime);
						
				}
				
			
			
			
			if (poll(pfd, pfdc, -1) < 0) continue;
			for(i = 0; i < pfdc; i++)
			{
			#if defined(OS_WINDOWS)
			if(sudp==0)
			{
				quitting=1;
				break;
			}
			#endif
				if (pfd[i].revents & POLLIN)
				{
				/* process SSDP packets */
				if(pfd[i].fd==sudp)
						{
							syslog(LOG_INFO, "Received UDP Packet");
							ProcessSSDPRequest(sudp, (unsigned short)SSDPport, lanaddrsptr);
						}
				#ifdef ENABLE_IPV6
				if(pfd[i].fd==sudpv6)
						{
							syslog(LOG_INFO, "Received UDP Packet (IPv6)");
							ProcessSSDPRequest(sudpv6, (unsigned short)SSDPport, lanaddrsptr);
						}
				#endif
				
				#if !defined(OS_WINDOWS)
				if(pfd[i].fd==ctrlfd)
						{
							read(ctrlfd,&qbuf,1);
							quitting=1; // TODO to check do I have to quit here
						}
				#endif
				
				}
				#if defined(OS_WINDOWS)
				if(pfd[i].revents & POLLHUP)
				{
					quitting=1; // TODO to check do I have to quit here
				}
				#endif
				
				pfd[i].revents = 0;
			}
			
		}
		
//TODO to check what to close
for(i=0;i<2;i++)
		{
#ifndef ENABLE_IPV6
		if(SendSSDPGoodbye(snotify, lanaddrsptr->count) < 0)
#else
		if(SendSSDPGoodbye(snotify, lanaddrsptr->count * 2) < 0)
#endif
		{
			syslog(LOG_ERR, "Failed to broadcast good-bye notifications");
		}
		}

SSDPDGetIFs_free(lanaddrsptr);
free(snotify);

#if !defined(OS_WINDOWS)
	close(sudp);
#else
	if(sudp)CLOSE(sudp);
#endif
#ifdef ENABLE_IPV6
		close(sudpv6);
#endif
}


/* AddMulticastMembership()
 * param s		socket
 * param ifaddr	ip v4 address
 */
static int
AddMulticastMembership(int s, in_addr_t ifaddr)
{
	struct ip_mreq imr;	/* Ip multicast membership */

    /* setting up imr structure */
    imr.imr_multiaddr.s_addr = inet_addr(SSDP_MCAST_ADDR);
    /*imr.imr_interface.s_addr = htonl(INADDR_ANY);*/
    imr.imr_interface.s_addr = ifaddr;	/*inet_addr(ifaddr);*/

	if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, (void *)&imr, sizeof(struct ip_mreq)) < 0)
	{
        syslog(LOG_ERR, "setsockopt(udp, IP_ADD_MEMBERSHIP): %m");
		return -1;
    }

	return 0;
}

/* AddMulticastMembershipIPv6()
 * param s	socket (IPv6)
 * To be improved to target specific network interfaces */
#ifdef ENABLE_IPV6
static int
AddMulticastMembershipIPv6(int s)
{
	struct ipv6_mreq mr;
	/*unsigned int ifindex;*/

	memset(&mr, 0, sizeof(mr));
	inet_pton(AF_INET6, LL_SSDP_MCAST_ADDR, &mr.ipv6mr_multiaddr);
	/*mr.ipv6mr_interface = ifindex;*/
	mr.ipv6mr_interface = 0; /* 0 : all interfaces */
#ifndef IPV6_ADD_MEMBERSHIP
#define IPV6_ADD_MEMBERSHIP IPV6_JOIN_GROUP
#endif
	if(setsockopt(s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mr, sizeof(struct ipv6_mreq)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp, IPV6_ADD_MEMBERSHIP): %m");
		return -1;
	}
	inet_pton(AF_INET6, SL_SSDP_MCAST_ADDR, &mr.ipv6mr_multiaddr);
	if(setsockopt(s, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &mr, sizeof(struct ipv6_mreq)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp, IPV6_ADD_MEMBERSHIP): %m");
		return -1;
	}
	return 0;
}
#endif

/* Open and configure the socket listening for
 * SSDP udp packets sent on 239.255.255.250 port 1900
 * SSDP v6 udp packets sent on FF02::C, or FF05::C, port 1900 */
int
OpenAndConfSSDPReceiveSocket(int ipv6, struct lan_addrs *lanaddrsptr)
{
	int s;
	struct sockaddr_storage sockname;
	socklen_t sockname_len;
	int j = 1;
	int i = 0;

#if !defined(OS_WINDOWS)
	if( (s = socket(ipv6 ? PF_INET6 : PF_INET, SOCK_DGRAM, 0)) < 0)
#else
	if( (s = socket(ipv6 ? PF_INET6 : PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
#endif
	{
		syslog(LOG_ERR, "%s: socket(udp): %m",
		       "OpenAndConfSSDPReceiveSocket");
		return -1;
	}

	memset(&sockname, 0, sizeof(struct sockaddr_storage));
	if(ipv6) {
		struct sockaddr_in6 * saddr = (struct sockaddr_in6 *)&sockname;
		saddr->sin6_family = AF_INET6;
		saddr->sin6_port = htons(SSDP_PORT);
		saddr->sin6_addr = in6addr_any;
		sockname_len = sizeof(struct sockaddr_in6);
	} else {
		struct sockaddr_in * saddr = (struct sockaddr_in *)&sockname;
		saddr->sin_family = AF_INET;
		saddr->sin_port = htons(SSDP_PORT);
		/* NOTE : it seems it doesnt work when binding on the specific address */
		/*saddr->sin_addr.s_addr = inet_addr(UPNP_MCAST_ADDR);*/
		saddr->sin_addr.s_addr = htonl(INADDR_ANY);
		/*saddr->sin_addr.s_addr = inet_addr(ifaddr);*/
		sockname_len = sizeof(struct sockaddr_in);
	}

#if !defined(OS_WINDOWS)
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &j, sizeof(j)) < 0)
	{
		syslog(LOG_WARNING, "setsockopt(udp, SO_REUSEADDR): %m");
	}
#else
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char *)&j, sizeof(j)) < 0)
	{
		syslog(LOG_WARNING, "setsockopt(udp, SO_REUSEADDR): %m");
	}
#endif

	if(!set_non_blocking(s))
	{
		syslog(LOG_WARNING, "%s: set_non_blocking(): %m",
		       "OpenAndConfSSDPReceiveSocket");
	}

	if(bind(s, (struct sockaddr *)&sockname, sockname_len) < 0)
	{
		syslog(LOG_ERR, "%s: bind(udp%s): %m",
		       "OpenAndConfSSDPReceiveSocket", ipv6 ? "6" : "");
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}

#ifdef ENABLE_IPV6
	if(ipv6)
	{
		if(AddMulticastMembershipIPv6(s) < 0)
		{
			syslog(LOG_WARNING,
			        "Failed to add IPv6 multicast membership");
		}
	}
	else
#endif
	{
		for(i = 0 ; i < lanaddrsptr->count; i++)
		{
			if(AddMulticastMembership(s, lanaddrsptr->lan_addr[i].addr.s_addr) < 0)
			{
				syslog(LOG_WARNING,
				       "Failed to add multicast membership for interface %s",
				       lanaddrsptr->lan_addr[i].host ? lanaddrsptr->lan_addr[i].host : "NULL");
			}
		}
	}

	return s;
}

/*

*/

/* open the UDP socket used to send SSDP notifications to
 * the multicast group reserved for them */
static int
OpenAndConfSSDPNotifySocket(in_addr_t addr)
{
	int s;
	unsigned char loopchar = 0;
	int bcast = 1;
	unsigned char ttl = 4; /* UDA v1.1 says :
		The TTL for the IP packet SHOULD default to 4 and
		SHOULD be configurable. */
	/* TODO: Make TTL be configurable */
	struct in_addr mc_if;
	struct sockaddr_in sockname;

#if !defined(OS_WINDOWS)
	if( (s = socket(PF_INET, SOCK_DGRAM, 0)) < 0)
#else
	if( (s = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
#endif
	{
		syslog(LOG_ERR, "socket(udp_notify): %m");
		return -1;
	}

	mc_if.s_addr = addr;	/*inet_addr(addr);*/

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_LOOP, (char *)&loopchar, sizeof(loopchar)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, IP_MULTICAST_LOOP): %m");
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}

	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_IF, (char *)&mc_if, sizeof(mc_if)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, IP_MULTICAST_IF): %m");
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}

#if !defined(OS_WINDOWS)
	if(setsockopt(s, IPPROTO_IP, IP_MULTICAST_TTL, &ttl, sizeof(ttl)) < 0)
	{
		syslog(LOG_WARNING, "setsockopt(udp_notify, IP_MULTICAST_TTL,): %m");
	}
#endif

	if(setsockopt(s, SOL_SOCKET, SO_BROADCAST, (char *)&bcast, sizeof(bcast)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, SO_BROADCAST): %m");
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}

	memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_addr.s_addr = addr;	/*inet_addr(addr);*/

    if (bind(s, (struct sockaddr *)&sockname, sizeof(struct sockaddr_in)) < 0)
	{
	/*
		int error = WSAGetLastError();
		char str_buffer[128];
		memset(str_buffer,0,128);
						inet_ntop(2, &addr, str_buffer, 128);
		syslog(LOG_ERR, "bind(udp_notify): %m");
	*/
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
    }

	return s;
}

#ifdef ENABLE_IPV6
/* open the UDP socket used to send SSDP notifications to
 * the multicast group reserved for them. IPv6 */
static int
OpenAndConfSSDPNotifySocketIPv6(unsigned int if_index)
{
	int s;
	unsigned int loop = 0;

	s = socket(PF_INET6, SOCK_DGRAM, 0);
	if(s < 0)
	{
		syslog(LOG_ERR, "socket(udp_notify IPv6): %m");
		return -1;
	}
	if(setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &if_index, sizeof(if_index)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify IPv6, IPV6_MULTICAST_IF, %u): %m", if_index);
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}
	if(setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &loop, sizeof(loop)) < 0)
	{
		syslog(LOG_ERR, "setsockopt(udp_notify, IPV6_MULTICAST_LOOP): %m");
		#if !defined(OS_WINDOWS)
			close(s);
		#else
			CLOSE(s);
		#endif
		return -1;
	}
	return s;
}
#endif

int OpenAndConfSSDPNotifySockets(int * sockets,struct lan_addrs *lanaddrsptr)
/*OpenAndConfSSDPNotifySockets(int * sockets,
                             struct lan_addr_s * lan_addr, int n_lan_addr)*/
{
	int i;
	int temp=0;
	for(i = 0 ; i < lanaddrsptr->count;)
	{
		sockets[i] = OpenAndConfSSDPNotifySocket(lanaddrsptr->lan_addr[i].addr.s_addr);
		i++;
#ifdef ENABLE_IPV6
		sockets[i] = OpenAndConfSSDPNotifySocketIPv6(lanaddrsptr->lan_addr[i].index);
		i++;
#endif
	}
	temp=i;
	while(--i >= 0)
	{
		if(sockets[i] >= 0)return 0;
	}
	
	while(--temp >= 0)
	{
		if(sockets[i]<0)
		{
		#if !defined(OS_WINDOWS)
		close(sockets[i]);
		#else
		CLOSE(sockets[i]);
		#endif
		}
		sockets[i] = -1;
	}

	return -1;
}

/*
 * response from a LiveBox (Wanadoo)
HTTP/1.1 200 OK
CACHE-CONTROL: max-age=1800
DATE: Thu, 01 Jan 1970 04:03:23 GMT
EXT:
LOCATION: http://192.168.0.1:49152/gatedesc.xml
SERVER: Linux/2.4.17, UPnP/1.0, Intel SDK for UPnP devices /1.2
ST: upnp:rootdevice
USN: uuid:75802409-bccb-40e7-8e6c-fa095ecce13e::upnp:rootdevice

 * response from a Linksys 802.11b :
HTTP/1.1 200 OK
Cache-Control:max-age=120
Location:http://192.168.5.1:5678/rootDesc.xml
Server:NT/5.0 UPnP/1.0
ST:upnp:rootdevice
USN:uuid:upnp-InternetGatewayDevice-1_0-0090a2777777::upnp:rootdevice
EXT:
 */

/* Responds to a SSDP "M-SEARCH"
 * s :          socket to use
 * addr :       peer
 * st, st_len : ST: header
 * suffix :     suffix for USN: header
 * host, port : our HTTP host, port
 */
static void
SendSSDPResponse(int s, const struct sockaddr * addr,
                 const char * st, int st_len, const char * suffix,
                 const char * host, unsigned short port, const char * uuidvalue, char *UUID)
{
	int l, n;
	char buf[512];
	char addr_str[64];
	socklen_t addrlen;
	int st_is_uuid;
#ifdef ENABLE_HTTP_DATE
	char http_date[64];
	time_t t;
	struct tm tm;

	time(&t);
	gmtime_r(&t, &tm);
	strftime(http_date, sizeof(http_date),
		    "%a, %d %b %Y %H:%M:%S GMT", &tm);
#endif

	st_is_uuid = (st_len == (int)strlen(uuidvalue)) &&
	              (memcmp(uuidvalue, st, st_len) == 0);
	/*
	 * follow guideline from document "UPnP Device Architecture 1.0"
	 * uppercase is recommended.
	 * DATE: is recommended
	 * SERVER: OS/ver UPnP/1.0 miniupnpd/1.0
	 * - check what to put in the 'Cache-Control' header
	 *
	 * have a look at the document "UPnP Device Architecture v1.1 */
	l = snprintf(buf, sizeof(buf), "HTTP/1.1 200 OK\r\n"
		"CACHE-CONTROL: max-age=1800\r\n"
#ifdef ENABLE_HTTP_DATE
		"DATE: %s\r\n"
#endif
		"ST:%.*s%s\r\n"
		"USN:%s%s%.*s%s\r\n"
		"EXT:\r\n"
		"SERVER:" DLNA_SERVER_STRING "\r\n"
		"LOCATION:http://%s:%u/%s/dlna/rootDesc.xml\r\n"
		"\r\n",
#ifdef ENABLE_HTTP_DATE
		http_date,
#endif
		st_len, st, suffix,
		uuidvalue, st_is_uuid ? "" : "::",
		st_is_uuid ? 0 : st_len, st, suffix,
		host, (unsigned int)port, UUID);
//		upnp_bootid, upnp_bootid, upnp_configid);
	if(l<0)
	{
		syslog(LOG_ERR, "%s: snprintf failed %m",
		       "SendSSDPResponse()");
		return;
	}
	else if((unsigned)l>=sizeof(buf))
	{
		syslog(LOG_WARNING, "%s: truncated output",
		       "SendSSDPResponse()");
		l = sizeof(buf) - 1;
	}
	addrlen = (addr->sa_family == AF_INET6)
	          ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);
	n = sendto(s, buf, l, 0,
	           addr, addrlen);
	sockaddr_to_string(addr, addr_str, sizeof(addr_str));
	syslog(LOG_INFO, "SSDP Announce %d bytes to %s ST: %.*s",n,
       		addr_str,
		l, buf);
	if(n < 0)
	{
		/* XXX handle EINTR, EAGAIN, EWOULDBLOCK */
		syslog(LOG_ERR, "sendto(udp): %m");
	}
}

#ifndef IGD_V2
#define IGD_VER 1
#define WANIPC_VER 1
#else
#define IGD_VER 2
#define WANIPC_VER 2
#endif

static struct {
	const char * s;
	const int version;
	const char * uuid;
} const known_service_types[] =
{
	{uuidvalue, 0, uuidvalue},
	{"upnp:rootdevice", 0, uuidvalue},
	{"urn:schemas-upnp-org:device:MediaServer:", 1, uuidvalue},
	{"urn:schemas-upnp-org:service:ContentDirectory:", 1, uuidvalue},
	{"urn:schemas-upnp-org:service:ConnectionManager:", 1, uuidvalue},
	{"urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:", 1, uuidvalue},
	{0, 0, 0}
};

/*
static const char * const known_service_types[] =
{
	uuidvalue,
	"upnp:rootdevice",
	"urn:schemas-upnp-org:device:MediaServer:",
	"urn:schemas-upnp-org:service:ContentDirectory:",
	"urn:schemas-upnp-org:service:ConnectionManager:",
	"urn:microsoft.com:service:X_MS_MediaReceiverRegistrar:",
	0
};


static struct {
	const char * s;
	const int version;
	const char * uuid;
} const known_service_types[] =
{
	{"upnp:rootdevice", 0, uuidvalue_igd},
	{"urn:schemas-upnp-org:device:InternetGatewayDevice:", IGD_VER, uuidvalue_igd},
	{"urn:schemas-upnp-org:device:WANConnectionDevice:", 1, uuidvalue_wcd},
	{"urn:schemas-upnp-org:device:WANDevice:", 1, uuidvalue_wan},
	{"urn:schemas-upnp-org:service:WANCommonInterfaceConfig:", 1, uuidvalue_wan},
	{"urn:schemas-upnp-org:service:WANIPConnection:", WANIPC_VER, uuidvalue_wcd},
#ifndef UPNP_STRICT
	{"urn:schemas-upnp-org:service:WANPPPConnection:", 1, uuidvalue_wcd},
#endif
#ifdef ENABLE_L3F_SERVICE
	{"urn:schemas-upnp-org:service:Layer3Forwarding:", 1, uuidvalue_igd},
#endif
#ifdef ENABLE_6FC_SERVICE
	{"url:schemas-upnp-org:service:WANIPv6FirewallControl:", 1, uuidvalue_wcd},
#endif
	{0, 0, 0}
};
*/

static void
SendDiscovery(int s, const struct sockaddr * dest,
               const char * host, unsigned short port,
              int ipv6, char *UUID)
{
	char bufr[512];
	int n, l;

	l = snprintf(bufr, sizeof(bufr),
		"M-SEARCH * HTTP/1.1\r\n"
		"Host: 239.255.255.250:1900\r\n"
		"Man: \"ssdp:discover\"\r\n"
		"MX: 2\r\n"
		"ST: uuid:%s\r\n"
		"\r\n", UUID
		);
	if(l<0)
	{
		syslog(LOG_ERR, "SendDiscovery() snprintf error");
		return;
	}
	else if((unsigned int)l >= sizeof(bufr))
	{
		syslog(LOG_WARNING, "SendDiscovery(): truncated output");
		l = sizeof(bufr) - 1;
	} 
	n = sendto(s, bufr, l, 0, dest,
#ifdef ENABLE_IPV6
		ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)
#else
		sizeof(struct sockaddr_in)
#endif
		);
	if(n < 0)
	{
		/* XXX handle EINTR, EAGAIN, EWOULDBLOCK */
		syslog(LOG_ERR, "sendto(udp_notify=%d, %s): %m", s,
		       host ? host : "NULL");
	}
	else if(n != l)
	{
		syslog(LOG_NOTICE, "sendto() sent %d out of %d bytes", n, l);
	}
}

static void
SendSSDPNotify(int s, const struct sockaddr * dest,
               const char * host, unsigned short port,
               const char * nt, const char * suffix,
               const char * usn1, const char * usn2, const char * usn3,
               unsigned int lifetime, int ipv6, char *UUID)
{
	char bufr[512];
	int n, l;

	l = snprintf(bufr, sizeof(bufr),
		"NOTIFY * HTTP/1.1\r\n"
		"HOST:%s:%d\r\n"
		"CACHE-CONTROL:max-age=%u\r\n"
		"LOCATION:http://%s:%u/%s/dlna/rootDesc.xml\r\n"
		"SERVER: " DLNA_SERVER_STRING "\r\n"
		"NT:%s%s\r\n"
		"USN:%s%s%s%s\r\n"
		"NTS:ssdp:alive\r\n"
		"\r\n",
		ipv6 ? "[" LL_SSDP_MCAST_ADDR "]" : SSDP_MCAST_ADDR,
		SSDP_PORT,
		1800,
		host, port, UUID,
		nt, suffix, /* NT: */
		usn1, usn2, usn3, suffix /* USN: */
		);
	if(l<0)
	{
		syslog(LOG_ERR, "SendSSDPNotify() snprintf error");
		return;
	}
	else if((unsigned int)l >= sizeof(bufr))
	{
		syslog(LOG_WARNING, "SendSSDPNotify(): truncated output");
		l = sizeof(bufr) - 1;
	} 
	n = sendto(s, bufr, l, 0, dest,
#ifdef ENABLE_IPV6
		ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)
#else
		sizeof(struct sockaddr_in)
#endif
		);
	if(n < 0)
	{
		/* XXX handle EINTR, EAGAIN, EWOULDBLOCK */
		syslog(LOG_ERR, "sendto(udp_notify=%d, %s): %m", s,
		       host ? host : "NULL");
	}
	else if(n != l)
	{
		syslog(LOG_NOTICE, "sendto() sent %d out of %d bytes", n, l);
	}
}

static void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime, int ipv6, char *UUID)
{
#ifdef ENABLE_IPV6
	struct sockaddr_storage sockname;
#else
	struct sockaddr_in sockname;
#endif
	int i=0;
	char ver_str[4];

	memset(&sockname, 0, sizeof(sockname));
#ifdef ENABLE_IPV6
	if(ipv6)
	{
		struct sockaddr_in6 * p = (struct sockaddr_in6 *)&sockname;
		p->sin6_family = AF_INET6;
		p->sin6_port = htons(SSDP_PORT);
		inet_pton(AF_INET6, LL_SSDP_MCAST_ADDR, &(p->sin6_addr));
	}
	else
#endif
	{
		struct sockaddr_in *p = (struct sockaddr_in *)&sockname;
		p->sin_family = AF_INET;
		p->sin_port = htons(SSDP_PORT);
		p->sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);
	}
	
	SendSSDPNotify(s, (struct sockaddr *)&sockname, host, port,
			               known_service_types[i].uuid, "",	/* NT: */
			               known_service_types[i].uuid, "", "", /* ver_str,	USN: */
			               lifetime, ipv6, UUID);
	i++;
	while(known_service_types[i].s)
	{
		if(i==1)
			ver_str[0] = '\0';
		else
			snprintf(ver_str, sizeof(ver_str), "%d", known_service_types[i].version);
		SendSSDPNotify(s, (struct sockaddr *)&sockname, host, port,
		               known_service_types[i].s, ver_str,	/* NT: */
		               known_service_types[i].uuid, "::",
		               known_service_types[i].s, /* ver_str,	USN: */
		               lifetime, ipv6, UUID);
		
		i++;
	}
	

//SendDiscovery(s, (struct sockaddr *)&sockname,
//               host, port,
//              ipv6, UUID);
}

void
SendSSDPNotifies2(int * sockets,
                  unsigned short port,
                  unsigned int lifetime,char *UUID, struct lan_addrs *lanaddrsptr)
{
	int i;
	for(i = 0 ; i < lanaddrsptr->count; i++)
	{
		if(sockets[i]<0)continue;
	
		SendSSDPNotifies(sockets[i], lanaddrsptr->lan_addr[i].host, port,
		                 lifetime, 0, UUID);
	
#ifdef ENABLE_IPV6
		i++;
		SendSSDPNotifies(sockets[i], ipv6_addr_for_http_with_brackets, port,
		                 lifetime, 1, UUID);
		
#endif
		
	}
}

/* ProcessSSDPRequest()
 * process SSDP M-SEARCH requests and responds to them */
void
ProcessSSDPRequest(int s, unsigned short port, struct lan_addrs *lanaddrsptr)
{
	int n;
	char bufr[1500];
	socklen_t len_r;
#ifdef ENABLE_IPV6
	struct sockaddr_storage sendername;
	len_r = sizeof(struct sockaddr_storage);
#else
	struct sockaddr_in sendername;
	len_r = sizeof(struct sockaddr_in);
#endif

	n = recvfrom(s, bufr, sizeof(bufr), 0,
	             (struct sockaddr *)&sendername, &len_r);
	if(n < 0)
	{
		/* EAGAIN, EWOULDBLOCK, EINTR : silently ignore (try again next time)
		 * other errors : log to LOG_ERR */
		if(errno != EAGAIN &&
		   errno != EWOULDBLOCK &&
		   errno != EINTR)
		{
			syslog(LOG_ERR, "recvfrom(udp): %m");
		}
		return;
	}
	ProcessSSDPData(s, bufr, n, (struct sockaddr *)&sendername, port, lanaddrsptr);

}

void
ProcessSSDPData(int s, const char *bufr, int n,
                const struct sockaddr * sender, unsigned short port, struct lan_addrs *lanaddrsptr) {
	int i, l;
	struct lan_addr * lan_addr = NULL;
	const char * st = NULL;
	int st_len = 0;
	int st_ver = 0;
	char sender_str[64];
	char ver_str[4];
	const char * announced_host = NULL;
#ifdef UPNP_STRICT
#ifdef ENABLE_IPV6
	char announced_host_buf[64];
#endif
	int mx_value = -1;
#endif

	/* get the string representation of the sender address */
	sockaddr_to_string(sender, sender_str, sizeof(sender_str));
	lan_addr = get_lan_for_peer(sender,lanaddrsptr);
	if(lan_addr == NULL)
	{
		syslog(LOG_WARNING, "SSDP packet sender %s not from a LAN, ignoring",
		       sender_str);
		return;
	}

	if(memcmp(bufr, "NOTIFY", 6) == 0)
	{
		/* ignore NOTIFY packets. We could log the sender and device type */
		return;
	}
	else if(memcmp(bufr, "M-SEARCH", 8) == 0)
	{
		i = 0;
		while(i < n)
		{
			while((i < n - 1) && (bufr[i] != '\r' || bufr[i+1] != '\n'))
				i++;
			i += 2;
			if((i < n - 3) && ((bufr[i]=='s' || bufr[i]=='S') && (bufr[i+1]=='t' || bufr[i+1]=='T') && bufr[i+2]==':'))
			{
				st = bufr+i+3;
				st_len = 0;
				while((*st == ' ' || *st == '\t') && (st < bufr + n))
					st++;
				while(st[st_len]!='\r' && st[st_len]!='\n'
				     && (st + st_len < bufr + n))
					st_len++;
				l = st_len;
				while(l > 0 && st[l-1] != ':')
					l--;
				st_ver = atoi(st+l);
				syslog(LOG_DEBUG, "ST: %.*s (ver=%d)", st_len, st, st_ver);
				/*j = 0;*/
				/*while(bufr[i+j]!='\r') j++;*/
				/*syslog(LOG_INFO, "%.*s", j, bufr+i);*/
			}
#ifdef UPNP_STRICT
			else if((i < n - 3) && ((bufr[i]=='m' || bufr[i]=='M') && (bufr[i+1]=='x' || bufr[i+1]=='X') && bufr[i+2]==':'))
			{
				const char * mx;
				int mx_len;
				mx = bufr+i+3;
				mx_len = 0;
				while((*mx == ' ' || *mx == '\t') && (mx < bufr + n))
					mx++;
				while(mx[mx_len]!='\r' && mx[mx_len]!='\n'
				     && (mx + mx_len < bufr + n))
					mx_len++;
				mx_value = atoi(mx);
				syslog(LOG_DEBUG, "MX: %.*s (value=%d)", mx_len, mx, mx_value);
			}
#endif
		}
#ifdef UPNP_STRICT
		if(mx_value < 0) {
			syslog(LOG_INFO, "ignoring SSDP packet missing MX: header");
			return;
		}
#endif
		/*syslog(LOG_INFO, "SSDP M-SEARCH packet received from %s",
	           sender_str );*/
		if(st && (st_len > 0))
		{
			/* TODO : doesnt answer at once but wait for a random time */
			syslog(LOG_INFO, "SSDP M-SEARCH from %s ST: %.*s",
			       sender_str, st_len, st);
			/* find in which sub network the client is */
			if(sender->sa_family == AF_INET)
			{
				if (lan_addr == NULL)
				{
					syslog(LOG_ERR, "Can't find in which sub network the client is");
					return;
				}
				announced_host = lan_addr->host;
			}
#ifdef ENABLE_IPV6
			else
			{
				/* IPv6 address with brackets */
#ifdef UPNP_STRICT
				int index;
				struct in6_addr addr6;
				size_t addr6_len = sizeof(addr6);
				/* retrieve the IPv6 address which
				 * will be used locally to reach sender */
				memset(&addr6, 0, sizeof(addr6));
				if(get_src_for_route_to (sender, &addr6, &addr6_len, &index) < 0) {
					syslog(LOG_WARNING, "get_src_for_route_to() failed, using %s", ipv6_addr_for_http_with_brackets);
					announced_host = ipv6_addr_for_http_with_brackets;
				} else {
					if(inet_ntop(AF_INET6, &addr6,
					             announced_host_buf+1,
					             sizeof(announced_host_buf) - 2)) {
						announced_host_buf[0] = '[';
						i = strlen(announced_host_buf);
						if(i < (int)sizeof(announced_host_buf) - 1) {
							announced_host_buf[i] = ']';
							announced_host_buf[i+1] = '\0';
						} else {
							syslog(LOG_NOTICE, "cannot suffix %s with ']'",
							       announced_host_buf);
						}
						announced_host = announced_host_buf;
					} else {
						syslog(LOG_NOTICE, "inet_ntop() failed %m");
						announced_host = ipv6_addr_for_http_with_brackets;
					}
				}
#else
				announced_host = ipv6_addr_for_http_with_brackets;
#endif
			}
#endif
			/* Responds to request with a device as ST header */
			for(i = 0; known_service_types[i].s; i++)
			{
				l = (int)strlen(known_service_types[i].s);
				if(l<=st_len && (0 == memcmp(st, known_service_types[i].s, l))
#ifdef UPNP_STRICT
				   && (st_ver <= known_service_types[i].version)
		/* only answer for service version lower or equal of supported one */
#endif
				   )
				{
					syslog(LOG_INFO, "Single search found");
					SendSSDPResponse(s, sender,
					                 st, st_len, "",
					                 announced_host, port,
					                 known_service_types[i].uuid,UUID.data);
					break;
				}
			}
			/* Responds to request with ST: ssdp:all */
			/* strlen("ssdp:all") == 8 */
			if(st_len==8 && (0 == memcmp(st, "ssdp:all", 8)))
			{
				syslog(LOG_INFO, "ssdp:all found");
				for(i=0; known_service_types[i].s; i++)
				{
					if(i==0)
						ver_str[0] = '\0';
					else
						snprintf(ver_str, sizeof(ver_str), "%d", known_service_types[i].version);
					l = (int)strlen(known_service_types[i].s);
					SendSSDPResponse(s, sender,
					                 known_service_types[i].s, l, ver_str,
					                 announced_host, port,
					                 known_service_types[i].uuid, UUID.data);
				}
				
				/* also answer for uuid */
				/*
				SendSSDPResponse(s, sender, uuidvalue, strlen(uuidvalue_igd), "",
				                 announced_host, port, uuidvalue_igd, UUID.data);
				SendSSDPResponse(s, sender, uuidvalue_wan, strlen(uuidvalue_wan), "",
				                 announced_host, port, uuidvalue_wan, UUID.data);
				SendSSDPResponse(s, sender, uuidvalue_wcd, strlen(uuidvalue_wcd), "",
				                 announced_host, port, uuidvalue_wcd, UUID.data);
				*/
			}
			/* responds to request by UUID value */
			/*
			l = (int)strlen(uuidvalue_igd);
			if(l==st_len)
			{
				if(0 == memcmp(st, uuidvalue_igd, l))
				{
					syslog(LOG_INFO, "ssdp:uuid (IGD) found");
					SendSSDPResponse(s, sender, st, st_len, "",
					                 announced_host, port, uuidvalue_igd, UUID);
				}
				else if(0 == memcmp(st, uuidvalue_wan, l))
				{
					syslog(LOG_INFO, "ssdp:uuid (WAN) found");
					SendSSDPResponse(s, sender, st, st_len, "",
					                 announced_host, port, uuidvalue_wan, UUID);
				}
				else if(0 == memcmp(st, uuidvalue_wcd, l))
				{
					syslog(LOG_INFO, "ssdp:uuid (WCD) found");
					SendSSDPResponse(s, sender, st, st_len, "",
					                 announced_host, port, uuidvalue_wcd, UUID);
				}
			}
			*/
		}
		else
		{
			syslog(LOG_INFO, "Invalid SSDP M-SEARCH from %s", sender_str);
		}
	}
	else
	{
		syslog(LOG_NOTICE, "Unknown udp packet received from %s", sender_str);
	}
}



static int
SendSSDPbyebye(int s, const struct sockaddr * dest,
               const char * nt, const char * suffix,
               const char * usn1, const char * usn2, const char * usn3,
               int ipv6)
{
	int n, l;
	char bufr[512];
/*
	l = snprintf(bufr, sizeof(bufr),
	             "NOTIFY * HTTP/1.1\r\n"
	             "HOST: %s:%d\r\n"
	             "NT: %s%s\r\n"
	             "USN: %s%s%s%s\r\n"
	             "NTS: ssdp:byebye\r\n"
	             "OPT: \"http://schemas.upnp.org/upnp/1/0/\"; ns=01\r\n" // UDA v1.1 
	             "01-NLS: %u\r\n" // same as BOOTID field. UDA v1.1 
	             "BOOTID.UPNP.ORG: %u\r\n" // UDA v1.1 
	             "CONFIGID.UPNP.ORG: %u\r\n" // UDA v1.1 
	             "\r\n",
	             ipv6 ? "[" LL_SSDP_MCAST_ADDR "]" : SSDP_MCAST_ADDR,
	             SSDP_PORT,
	             nt, suffix,	//NT: 
	             usn1, usn2, usn3, suffix,	// USN:
	             upnp_bootid, upnp_bootid, upnp_configid);
*/
	l = snprintf(bufr, sizeof(bufr),
	             "NOTIFY * HTTP/1.1\r\n"
	             "HOST:%s:%d\r\n"
	             "NT:%s%s\r\n"
	             "USN:%s%s%s%s\r\n"
	             "NTS:ssdp:byebye\r\n"
	             "\r\n",
	             ipv6 ? "[" LL_SSDP_MCAST_ADDR "]" : SSDP_MCAST_ADDR,
	             SSDP_PORT,
	             nt, suffix,	//NT: 
	             usn1, usn2, usn3, suffix	// USN:
	             );
	if(l<0)
	{
		syslog(LOG_ERR, "SendSSDPbyebye() snprintf error");
		return -1;
	}
	else if((unsigned int)l >= sizeof(bufr))
	{
		syslog(LOG_WARNING, "SendSSDPbyebye(): truncated output");
		l = sizeof(bufr) - 1;
	}
	n = sendto(s, bufr, l, 0, dest,
#ifdef ENABLE_IPV6
	           ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in)
#else
	           sizeof(struct sockaddr_in)
#endif
	          );
	if(n < 0)
	{
		syslog(LOG_ERR, "sendto(udp_shutdown=%d): %m", s);
		return -1;
	}
	else if(n != l)
	{
		syslog(LOG_NOTICE, "sendto() sent %d out of %d bytes", n, l);
		return -1;
	}
	return 0;
}

/* This will broadcast ssdp:byebye notifications to inform
 * the network that UPnP is going down. */
int
SendSSDPGoodbye(int * sockets, int n_sockets)
{
	struct sockaddr_in sockname;
#ifdef ENABLE_IPV6
	struct sockaddr_in6 sockname6;
#endif
	int i=0, j=0;
	char ver_str[4];
	int ret = 0;
	int ipv6 = 0;

    memset(&sockname, 0, sizeof(struct sockaddr_in));
    sockname.sin_family = AF_INET;
    sockname.sin_port = htons(SSDP_PORT);
    sockname.sin_addr.s_addr = inet_addr(SSDP_MCAST_ADDR);
#ifdef ENABLE_IPV6
	memset(&sockname6, 0, sizeof(struct sockaddr_in6));
	sockname6.sin6_family = AF_INET6;
	sockname6.sin6_port = htons(SSDP_PORT);
	inet_pton(AF_INET6, LL_SSDP_MCAST_ADDR, &(sockname6.sin6_addr));
#endif

	for(j=0; j<n_sockets; j++) 
	{
	i=0;
	if(sockets[j]<0)continue;
#ifdef ENABLE_IPV6
		ipv6 = j & 1;
#endif

	if(known_service_types[i].uuid==NULL)
	{
	return 0;
	}

		ret += SendSSDPbyebye(sockets[j],
		#ifdef ENABLE_IPV6
				                      ipv6 ? (struct sockaddr *)&sockname6 : (struct sockaddr *)&sockname,
		#else
				                      (struct sockaddr *)&sockname,
		#endif
				                      known_service_types[i].uuid, "",	/* NT: */
				                      known_service_types[i].uuid, "", "", /* ver_str, USN: */
				                      ipv6);

	    for(i=1; known_service_types[i].s; i++)
	    {
			if(i==1)
				ver_str[0] = '\0';
			else
				snprintf(ver_str, sizeof(ver_str), "%d", known_service_types[i].version);
			ret += SendSSDPbyebye(sockets[j],
#ifdef ENABLE_IPV6
			                      ipv6 ? (struct sockaddr *)&sockname6 : (struct sockaddr *)&sockname,
#else
			                      (struct sockaddr *)&sockname,
#endif
			                      known_service_types[i].s, ver_str,	/* NT: */
			                      known_service_types[i].uuid, "::",
			                      known_service_types[i].s, /* ver_str, USN: */
			                      ipv6);
			
    	}
	}
	return ret;
}

/* SubmitServicesToMiniSSDPD() :
 * register services offered by MiniUPnPd to a running instance of
 * MiniSSDPd */
 /*
int
SSDPDServicesSubmit(const char * host, unsigned short port, char *UUID) {
	struct sockaddr_un addr;
	int s;
	unsigned char buffer[2048];
	char strbuf[256];
	unsigned char * p;
	int i, l, n;
	char ver_str[4];

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if(s < 0) {
		syslog(LOG_ERR, "socket(unix): %m");
		return -1;
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, minissdpdsocketpath, sizeof(addr.sun_path));
	if(connect(s, (struct sockaddr *)&addr, sizeof(struct sockaddr_un)) < 0) {
		syslog(LOG_ERR, "connect(\"%s\"): %m", minissdpdsocketpath);
		close(s);
		return -1;
	}
	for(i = 0; known_service_types[i].s; i++) {
		buffer[0] = 4;	//request type 4 : submit service
		// 4 strings following : ST (service type), USN, Server, Location 
		p = buffer + 1;
		l = (int)strlen(known_service_types[i].s);
		if(i > 0)
			l++;
		CODELENGTH(l, p);
		memcpy(p, known_service_types[i].s, l);
		if(i > 0)
			p[l-1] = '1';
		p += l;
		if(i==0)
			ver_str[0] = '\0';
		else
			snprintf(ver_str, sizeof(ver_str), "%d", known_service_types[i].version);
		l = snprintf(strbuf, sizeof(strbuf), "%s::%s%s",
		             known_service_types[i].uuid, known_service_types[i].s, ver_str);
		if(l<0) {
			syslog(LOG_WARNING, "SSDPDServicesSubmit: snprintf %m");
			continue;
		} else if((unsigned)l>=sizeof(strbuf)) {
			l = sizeof(strbuf) - 1;
		}
		CODELENGTH(l, p);
		memcpy(p, strbuf, l);
		p += l;
		l = (int)strlen(MINIUPNPD_SERVER_STRING);
		CODELENGTH(l, p);
		memcpy(p, MINIUPNPD_SERVER_STRING, l);
		p += l;
		l = snprintf(strbuf, sizeof(strbuf), "http://%s:%u/%s/upnp/rootDesc.xml", UUID,
		             host, (unsigned int)port);
		if(l<0) {
			syslog(LOG_WARNING, "SSDPDServicesSubmit: snprintf %m");
			continue;
		} else if((unsigned)l>=sizeof(strbuf)) {
			l = sizeof(strbuf) - 1;
		}
		CODELENGTH(l, p);
		memcpy(p, strbuf, l);
		p += l;
		// now write the encoded data 
		n = p - buffer;	// bytes to send 
		p = buffer;	// start 
		while(n > 0) {
			l = write(s, p, n);
			if (l < 0) {
				syslog(LOG_ERR, "write(): %m");
				close(s);
				return -1;
			} else if (l == 0) {
				syslog(LOG_ERR, "write() returned 0");
				close(s);
				return -1;
			}
			p += l;
			n -= l;
		}
	}
 	close(s);
	return 0;
}
*/
