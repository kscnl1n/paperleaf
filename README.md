# paperleaf

small c program for a raspberry pi e-reader vibe. not a real os from scratch: it runs on normal linux (raspberry pi os is fine) and uses the text console + ncurses so you get menus without needing a full desktop.

## what it does

main screen: library (lists pdfs) or power off.

library: shows `.pdf` files from a folder, sorted by name. pick one and hit enter to open it in whatever viewer you configured (defaults to `mupdf`). theres a back row at the bottom to return to the main menu.

power off asks y/n then tries `systemctl poweroff` / `shutdown` / `poweroff` — same permission story as always on linux (often need root or polkit or whatever your distro expects).

q from the main menu quits the app. q from library jumps back to main.

## build

on the pi (or any debian-ish box youre targeting):

```bash
sudo apt install build-essential libncurses-dev
make
```

install a pdf viewer separately, e.g. `sudo apt install mupdf`.

## running

```bash
./paperleaf
```

by default it looks for pdfs in `./library` (relative to where you run it). you can point it somewhere else:

```bash
export PAPERLEAF_LIBRARY=/home/pi/books
./paperleaf
```

different viewer:

```bash
export PAPERLEAF_PDF_VIEWER=zathura
./paperleaf
```

## autostart / kiosk

if you want this to be basically the whole experience after boot: autologin to a tty, then start `paperleaf` from `.profile` or `.bash_profile` with the full path to the binary. systemd user units work too if thats your thing. i didnt ship a service file because everyones setup is slightly different.

## cross compile

makefile comment mentions something like `make CC=arm-linux-gnueabihf-gcc` if youre building from another machine — you still need the arm ncurses dev bits on the toolchain side which is its own rabbit hole.

## limits

max 256 pdfs in one folder, names truncated internally to something reasonable. if something breaks the viewer fork the app should come back when the child exits.

## to-do:
- add true OS functionality sans linux.
