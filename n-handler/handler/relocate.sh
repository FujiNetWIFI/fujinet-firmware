#!/bin/bash

make RUNADDRESS=12288 OUTFILE=ndev0.com
make RUNADDRESS=12304 OUTFILE=ndev1.com

mv ndev0.com ndev1.com relwork-dest/

dir2atr -b Dos20 -S relwork.atr relwork-dist 
