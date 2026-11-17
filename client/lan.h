/* LAN discovery: listen for BFDISC beacons from Host / dedicated servers. */
#ifndef CLIENT_LAN_H
#define CLIENT_LAN_H

#define MAX_LAN_SERVERS 16

typedef struct {
    char host[64];
    int port;
    long long seen_ms;
} LanServer;

void lan_start(void);
void lan_stop(void);
void lan_pump(void); /* recv beacons and expire stale entries */

int lan_count(void);
const LanServer *lan_at(int i);

#endif
