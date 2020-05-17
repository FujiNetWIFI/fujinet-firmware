#!/bin/bash

make RUNADDRESS=12288 OUTFILE=ndev0.com
make RUNADDRESS=12544 OUTFILE=ndev1.com

mv ndev0.com ndev1.com relwork-dist/

dir2atr -b Dos20 -S relwork.atr relwork-dist 
