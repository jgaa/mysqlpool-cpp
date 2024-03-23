FROM mariadb:latest

# Copy certificates

COPY server-cert.pem /etc/mysql/server-cert.pem
COPY server-key.pem  /etc/mysql/server-key.pem
COPY ca.pem /etc/mysql/cacert.pem

RUN chmod 0444 /etc/mysql/cacert.pem \
    /etc/mysql/server-key.pem \
    /etc/mysql/server-cert.pem

#Enable TLS in the MariaDB configuration
RUN echo '\n\
[mariadb]\n\
ssl-ca = /etc/mysql/cacert.pem\n\
ssl-cert = /etc/mysql/server-cert.pem\n\
ssl-key = /etc/mysql/server-key.pem\n\
require-secure-transport = on\n\
' >> /etc/mysql/my.cnf
