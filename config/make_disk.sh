#!/bin/bash

FILESIZE=$(stat --printf="%s" config.com)
PADDINGSIZE=$(expr 92160 - $FILESIZE)

dd if=/dev/zero of=padding bs=1 count=$PADDINGSIZE

cat header.atr config.com padding >autorun.atr
