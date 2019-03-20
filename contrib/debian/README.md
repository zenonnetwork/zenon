
Debian
====================
This directory contains files used to package Zenond/Zenon-qt
for Debian-based Linux systems. If you compile Zenond/Zenon-qt yourself, there are some useful files here.

## Zenon: URI support ##


Zenon-qt.desktop  (Gnome / Open Desktop)
To install:

	sudo desktop-file-install Zenon-qt.desktop
	sudo update-desktop-database

If you build yourself, you will either need to modify the paths in
the .desktop file or copy or symlink your Zenonqt binary to `/usr/bin`
and the `../../share/pixmaps/Zenon128.png` to `/usr/share/pixmaps`

Zenon-qt.protocol (KDE)

