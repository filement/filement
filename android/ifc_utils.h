#ifndef _IFC_UTILS_H_
#define _IFC_UTILS_H_

int ifc_init(void);

int ifc_get_ifindex(const char *name, int *if_indexp);
int ifc_get_hwaddr(const char *name, void *ptr);

int ifc_up(const char *name);
int ifc_down(const char *name);

int ifc_set_addr(const char *name, unsigned addr);
int ifc_set_mask(const char *name, unsigned mask);

int ifc_create_default_route(const char *name, unsigned addr);


int ifc_get_info(const char *name, in_addr_t *addr, int *prefixLength, unsigned *flags);

in_addr_t prefixLengthToIpv4Netmask(int prefix_length);

void ifc_close(void);

int ifc_get_addr(const char *name, in_addr_t *addr);
#endif
