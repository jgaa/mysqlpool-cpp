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

# # Define the subject details for the certificate
# root_subj="/C=EU/O=The Last Viking LTD/OU=Root CA/CN=rootca.localhost"
# server_subj="/C=EU/O=The Last Viking LTD/CN=localhost"
#
# # Generate a new private key for the root certificate
# openssl genrsa -out root-key.pem 2048
#
# # Generate a self-signed root certificate
# openssl req -x509 -new -nodes -key root-key.pem -sha256 -days 1024 -out root-cert.pem -subj "$root_subj"
#
# # Generate a new private key for the server certificate
# openssl genrsa -out server-key.pem 2048
#
# # Generate a Certificate Signing Request (CSR) for the server certificate
# openssl req -new -key server-key.pem -out server.csr -subj "$server_subj"
#
# # Generate the server certificate signed by the root certificate
# openssl x509 -req -in server.csr -CA root-cert.pem -CAkey root-key.pem -CAcreateserial -out server-cert.pem -days 365 -sha256
#
# # Export the public key of the root certificate to ca.pem
# openssl x509 -pubkey -noout -in root-cert.pem > ca.pem

echo "Done";
