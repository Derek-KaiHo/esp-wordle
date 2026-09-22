#pragma once

// Handles one protocol line from the peer (called from the link task).
void game_on_peer_line(const char *line);

// Runs the game loop forever. Call after link_start().
void game_run(void);
