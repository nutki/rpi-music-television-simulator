#!/bin/bash
#
# DBUS setup copied from the OMXPlayer launcher script.
#

DBUS_CMD="dbus-daemon --fork --print-address 5 --print-pid 6 --session"
OMXPLAYER_DBUS_ADDR="/tmp/omxplayerdbus.${USER:-root}"
OMXPLAYER_DBUS_PID="/tmp/omxplayerdbus.${USER:-root}.pid"

if [ ! -s "$OMXPLAYER_DBUS_PID" ] || ! pgrep -f "$DBUS_CMD" -F "$OMXPLAYER_DBUS_PID" >/dev/null; then
	#echo "starting dbus for the first time" >&2
	exec 5> "$OMXPLAYER_DBUS_ADDR"
	exec 6> "$OMXPLAYER_DBUS_PID"
	$DBUS_CMD
	until [ -s "$OMXPLAYER_DBUS_ADDR" ]; do
		echo "waiting for dbus address to appear" >&2
		sleep .2
	done
fi

DBUS_SESSION_BUS_ADDRESS=`cat $OMXPLAYER_DBUS_ADDR`
DBUS_SESSION_BUS_PID=`cat $OMXPLAYER_DBUS_PID`

export DBUS_SESSION_BUS_ADDRESS
export DBUS_SESSION_BUS_PID

PLAYER_DIR=`dirname $0`

cd "$PLAYER_DIR"

player/mpvplayer "$@"
