
#ifndef _NETUTILS_IFC_H_
#define _NETUTILS_IFC_H_

#include <sys/cdefs.h>
#include <arpa/inet.h>


extern int ifc_init(void);
extern void ifc_close(void);

extern int ifc_get_ifindex(const char *name, int *if_indexp);
extern int ifc_get_hwaddr(const char *name, void *ptr);

extern int ifc_up(const char *name);
extern int ifc_down(const char *name);

extern int ifc_enable(const char *ifname);
extern int ifc_disable(const char *ifname);

extern int ifc_reset_connections(const char *ifname);

extern int ifc_set_addr(const char *name, in_addr_t addr);
extern int ifc_set_mask(const char *name, in_addr_t mask);
extern int ifc_set_hwaddr(const char *name, const void *ptr);

/* This function is deprecated. Use ifc_add_route instead. */
extern int ifc_add_host_route(const char *name, in_addr_t addr);
extern int ifc_remove_host_routes(const char *name);
extern int ifc_get_default_route(const char *ifname);
/* This function is deprecated. Use ifc_add_route instead */
extern int ifc_set_default_route(const char *ifname, in_addr_t gateway);
/* This function is deprecated. Use ifc_add_route instead */
extern int ifc_create_default_route(const char *name, in_addr_t addr);
extern int ifc_remove_default_route(const char *ifname);
extern int ifc_add_route(const char *name, const char *addr, int prefix_length,
                         const char *gw);

extern int ifc_get_info(const char *name, in_addr_t *addr, in_addr_t *mask,
                        in_addr_t *flags);

extern int ifc_configure(const char *ifname, in_addr_t address,
                         in_addr_t netmask, in_addr_t gateway,
                         in_addr_t dns1, in_addr_t dns2);


#endif /* _NETUTILS_IFC_H_ */
