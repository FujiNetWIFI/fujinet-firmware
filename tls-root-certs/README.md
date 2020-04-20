tls-root-certs
==============

First, run `mk-ca-bundle.pl` to get the current Mozilla CA Root Certicates into a single PEM file (ca-bundle.pem). Next, run `gen_crt_bundle.py --input ca-bundle.pem` to create an ESP32 compatible x509 certificate bundle.

To be done:
-----------
* Implement code which verifies fingerprints against store.

