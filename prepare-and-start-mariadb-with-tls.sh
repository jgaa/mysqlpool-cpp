#!/bin/bash

port=33061

pushd docker

bash ./create_certs.sh

docker build -t mysqlpool-mariadb-tls .

echo "Starting a disposable mariadb container with TLS on port ${port} enabled for testing..."

if [ -z "${MYSQLPOOL_DBPASSW}" ] ; then
  MYSQLPOOL_DBPASSW=`dd if=/dev/random bs=48 count=1 | base64`
  echo "MYSQLPOOL_DBPASSW is: ${MYSQLPOOL_DBPASSW}"
else
  echo "MYSQLPOOL_DBPASSW is preset to: ${MYSQLPOOL_DBPASSW}"
fi

docker run --rm --detach --name mysqlpool-mariadb-tls -p 127.0.0.1:${port}:3306 --env MARIADB_ROOT_PASSWORD=${MYSQLPOOL_DBPASSW}  mysqlpool-mariadb-tls

popd
