#!/bin/sh

echo "Starting a disposable mariadb container for testing..."

if [ -z "${MYSQLPOOL_DBPASSW}" ] ; then
  MYSQLPOOL_DBPASSW=`dd if=/dev/random bs=48 count=1 | base64`
  echo "MYSQLPOOL_DBPASSW is: ${MYSQLPOOL_DBPASSW}"
else
  echo "MYSQLPOOL_DBPASSW is preset to: ${MYSQLPOOL_DBPASSW}"
fi

docker run --rm --detach --name mysqlpool-mariadb -p 127.0.0.1:33060:3306 --env MARIADB_ROOT_PASSWORD=${MYSQLPOOL_DBPASSW}  mariadb:latest

