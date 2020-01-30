#!/bin/bash

FILESIZE=$(stat --printf="%s" plato.com)
PADDINGSIZE=$(expr 92160 - $FILESIZE)

dd if=/dev/zero of=padding bs=1 count=$PADDINGSIZE

cat header.atr plato.com padding >../data/plato.atr
