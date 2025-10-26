#!/bin/bash
sudo rm -rf /root/rpmbuild/
sudo rm -rf /tmp/gxde-lsg-source
mkdir /tmp/gxde-lsg-source -pv
cp * /tmp/gxde-lsg-source -rv
cp rpm/top.gxde.gxde-lsg.spec /tmp/top.gxde.gxde-lsg.spec
sudo rpmbuild -bb /tmp/top.gxde.gxde-lsg.spec 
sudo bash -c 'cp /root/rpmbuild/RPMS/*/top.gxde.gxde-lsg*.rpm .'
sudo rm -rf /root/rpmbuild/
sudo rm -rf /tmp/gxde-lsg-source
