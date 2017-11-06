/* $Id: minissdp.h,v 1.11 2012/09/27 16:00:44 nanard Exp $ */
/* MiniUPnP project
 * http://miniupnp.free.fr/ or http://miniupnp.tuxfamily.org/
 * (c) 2006-2007 Thomas Bernard
 * This software is subject to the conditions detailed
 * in the LICENCE file provided within the distribution */
#ifndef SSDP_H_INCLUDED
#define SSDP_H_INCLUDED

#if !defined(OS_WINDOWS)

#include <netinet/in.h>
#include <net/if.h>
#include <sys/queue.h>

#endif

/* structure and list for storing lan addresses
 * with ascii representation and mask */
struct lan_addr {
#ifdef ENABLE_IPV6
	unsigned int index;		/* use if_nametoindex() */
#endif
	char *host;	/* example: 192.168.0.1 */
	struct in_addr addr, mask;	/* ip/mask */
};

struct lan_addrs {
	int count; 
	struct lan_addr lan_addr[];
};

int DLNAisEnabled(void);
void DLNAEnable(void *);
void DLNADisable(void *);
void SSDPDThread(int ctrlfd);

int
OpenAndConfSSDPReceiveSocket(int ipv6, struct lan_addrs *lanaddrsptr);

int OpenAndConfSSDPNotifySockets(int * sockets,struct lan_addrs *lanaddrsptr);

/*void
SendSSDPNotifies(int s, const char * host, unsigned short port,
                 unsigned int lifetime);*/
void
SendSSDPNotifies2(int * sockets,
                  unsigned short port,
                  unsigned int lifetime,char *UUID, struct lan_addrs *lanaddrsptr);
				  
/*SendSSDPNotifies2(int * sockets, struct lan_addr_s * lan_addr, int n_lan_addr,
                  unsigned short port,
                  unsigned int lifetime);*/
				  

void
ProcessSSDPRequest(int s, unsigned short port, struct lan_addrs *lanaddrsptr);
/*ProcessSSDPRequest(int s, struct lan_addr_s * lan_addr, int n_lan_addr,
                   unsigned short port);*/

void
ProcessSSDPData(int s, const char *bufr, int n,
                const struct sockaddr * sender, unsigned short port, struct lan_addrs *lanaddrsptr);

int
SendSSDPGoodbye(int * sockets, int n);

int
SubmitServicesToMiniSSDPD(const char * host, unsigned short port, char *UUID);


static void
SendDiscovery(int s, const struct sockaddr * dest,
               const char * host, unsigned short port,
              int ipv6, char *UUID);

#define ROOTDESC_PATH 		"/rootDesc.xml"

//#include "../protocol.h"
    #define SSDPport PORT_HTTP_MIN
	#define SSDPnotify_interval 60
	#define DLNA_SERVER_STRING "Filement/1.0 DLNADOC/1.50 UPnP/1.0"

	#define upnp_bootid 1 //this should be increased everytime we invoke update, but for now is static
    #define upnp_configid 1233

#endif

