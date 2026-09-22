#pragma once

#include <stdbool.h>

// Called from the link task whenever a full line arrives from the peer.
typedef void (*link_rx_cb_t)(const char *line);

// Brings up WiFi (AP or station depending on the configured role) and starts
// the background task that maintains the TCP connection to the peer.
void link_start(link_rx_cb_t rx_cb);

bool link_connected(void);

// Sends one line to the peer (a '\n' is appended). Returns false if not connected.
bool link_send_line(const char *line);
