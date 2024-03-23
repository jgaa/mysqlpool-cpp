#!/bin/sh

echo "Creating self sighed certs with 10 years validity..."

root_subj="/C=EU/O=The Last Viking LTD/OU=Root CA/CN=rootca.localhost"
server_subj="/C=EU/O=The Last Viking LTD/CN=localhost"


openssl genrsa 2048 > ca-key.pem

openssl req -new -x509 -nodes -days 365000 -key ca-key.pem -out ca.pem -subj "$root_subj"

#openssl req -newkey rsa:2048 -days 365000 -nodes -keyout server-key.pem -out server-req.pem -subj "$server_subj"
openssl req -newkey rsa:2048 -nodes -keyout server-key.pem -out server-req.pem -subj "$server_subj"
#openssl req -newkey rsa:2048 -x509  -days 365000 -nodes -keyout server-key.pem -out server-req.pem -subj "$server_subj"

openssl rsa -in server-key.pem -out server-key.pem

openssl x509 -req -in server-req.pem -days 365000 -CA ca.pem -CAkey ca-key.pem -set_serial 01 -out server-cert.pem

openssl verify -CAfile ca.pem server-cert.pem

echo "Done";
