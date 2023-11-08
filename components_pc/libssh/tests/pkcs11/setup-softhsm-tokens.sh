#!/bin/bash

# The client keys are stored in a SoftHSM device.

TESTDIR=$1
PRIVKEY=$2
OBJNAME=$3
LOADPUBLIC=$4
LIBSOFTHSM_PATH=$5
shift 5

PUBKEY="$PRIVKEY.pub"

echo "TESTDIR: $TESTDIR"
echo "PRIVKEY: $PRIVKEY"
echo "PUBKEY: $PUBKEY"
echo "OBJNAME: $OBJNAME"
echo "LOADPUBLIC: $LOADPUBLIC"

# Create temporary directory for tokens
install -d -m 0755 "$TESTDIR/db"

# Create SoftHSM configuration file
cat >"$TESTDIR/softhsm.conf" <<EOF
directories.tokendir = $TESTDIR/db
objectstore.backend = file
log.level = DEBUG
EOF

export SOFTHSM2_CONF=$TESTDIR/softhsm.conf

cat "$TESTDIR/softhsm.conf"

#init
cmd="softhsm2-util --init-token --label $OBJNAME --free --pin 1234 --so-pin 1234"
eval echo "$cmd"
out=$(eval "$cmd")
ret=$?
if [ $ret -ne 0 ]; then
    echo "Init token failed"
    echo "$out"
    exit 1
fi

#load private key
cmd="p11tool --provider $LIBSOFTHSM_PATH --write --load-privkey $PRIVKEY --label $OBJNAME --login --set-pin=1234 \"pkcs11:token=$OBJNAME\""
eval echo "$cmd"
out=$(eval "$cmd")
ret=$?
if [ $ret -ne 0 ]; then
   echo "Loading privkey failed"
   echo "$out"
   exit 1
fi

cat "$PUBKEY"

ls -l "$TESTDIR"

if [ "$LOADPUBLIC" -ne 0 ]; then
#load public key
    cmd="p11tool --provider $LIBSOFTHSM_PATH --write --load-pubkey $PUBKEY --label $OBJNAME --login --set-pin=1234 \"pkcs11:token=$OBJNAME\""
    eval echo "$cmd"
    out=$(eval "$cmd")
    ret=$?
    if [ $ret -ne 0 ]; then
        echo "Loading pubkey failed"
        echo "$out"
        exit 1
    fi
fi

cmd="p11tool --list-all --login \"pkcs11:token=$OBJNAME\" --set-pin=1234"
eval echo "$cmd"
out=$(eval "$cmd")
ret=$?
if [ $ret -ne 0 ]; then
    echo "Logging in failed"
    echo "$out"
    exit 1
fi
echo "$out"

exit 0
