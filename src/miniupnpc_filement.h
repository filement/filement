
struct vector *filement_get_upnp_forwardings(void);
void filement_get_upnp_forwarding(char **ip,int ports[], int *count); // ip must be freed
void filement_set_upnp_forwarding(int filement_port);
