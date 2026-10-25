#!/bin/sh
# snapd: expose snap binaries and desktop entries

case ":$PATH:" in
  *:/var/lib/snapd/snap/bin:*) ;;
  *) export PATH="$PATH:/var/lib/snapd/snap/bin" ;;
esac

XDG_DATA_DIRS="${XDG_DATA_DIRS:-/usr/local/share:/usr/share}"
case ":$XDG_DATA_DIRS:" in
  *:/var/lib/snapd/desktop:*) ;;
  *) XDG_DATA_DIRS="$XDG_DATA_DIRS:/var/lib/snapd/desktop" ;;
esac
export XDG_DATA_DIRS
