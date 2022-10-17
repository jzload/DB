#!/bin/bash

cd ${HOME}/db_core
rm -rf kwreports
mkdir -m 777 kwreports

rm -rf my_build
cp -r BUILD/ my_build
cd my_build
sed -i "s/make -j4 /make -j16 /g" make_mysql.sh

/home/zxin10/kwuser/bin/kwinject -o /root/db_core/kw.out sh make_mysql.sh

cd ${HOME}/db_core
