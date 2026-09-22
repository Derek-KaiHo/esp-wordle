# esp-wordle

Two-player Wordle over WiFi between two ESP32 boards. Each board is plugged
into a computer over USB; players type guesses in the serial monitor. Board A
("host") runs a WiFi access point and TCP server; board B ("joiner") connects
to it — no router involved.

## How it plays

At boot each board shows a menu: **single player** or **two-player duel**.

Single player: the board picks a random word from the curated ~2,315-word
Wordle answers list (`main/answerlist.c`) and you have 6 guesses. No second
board needed.

Duel rules: each player secretly picks a 5-letter word for their opponent,
then both race to guess the word they were given (6 tries, classic
green/yellow/gray feedback rendered with ANSI colors in the terminal).
Solving beats not solving; fewer guesses wins; otherwise it's a draw.
After each round both boards return to the menu; picking duel on both
starts a rematch.

Words (secrets and guesses) are validated against the canonical ~14,855-word
Wordle guess list baked into flash (`main/wordlist.c`). Each board scores
guesses against its own secret, so secret words never cross the WiFi link.

Code layout:

- `main/link.c` — WiFi (AP or station) + TCP line channel with reconnect
- `main/wordle.c` — pure scoring + word validation (unit-testable on the host)
- `main/game.c` — duel state machine, wire protocol, terminal UI
- `main/main.c` — boot, console setup, wiring

## Building

Enter the ESP-IDF environment in every new terminal first:

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
```

The two roles build into separate directories from the same source:

```sh
# Host board (AP + TCP server)
idf.py -B build/host -D SDKCONFIG="$PWD/build/host/sdkconfig" \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.host" build

# Joiner board (station + TCP client)
idf.py -B build/join -D SDKCONFIG="$PWD/build/join/sdkconfig" \
       -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.join" build
```

## Flashing and playing

Flash each board from its build directory, pointing at that board's port
(find ports with `ls /dev/cu.usbserial*`):

```sh
idf.py -B build/host -p /dev/cu.usbserial-210 flash monitor
idf.py -B build/join -p /dev/cu.usbserial-XXX flash monitor
```

`flash monitor` flashes and then opens the serial monitor (quit with Ctrl+]).
Boot order doesn't matter — the joiner retries until the host is up. Once
`link: peer connected` appears, anything typed at the `you>` prompt shows up
on the other board as `peer> ...`.

The WiFi credentials and TCP port are in `main/link.c` (`ESP-WORDLE` /
`wordle123`, port 3333).
