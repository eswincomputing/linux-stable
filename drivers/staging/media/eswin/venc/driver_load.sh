#!/bin/sh
module="es_venc"
enc_dev_path="/dev"
device="${enc_dev_path}/${module}"
mode="666"


if [ ! -e ${enc_dev_path} ]
then
    mkdir ${enc_dev_path}/
fi

#insert module
modprobe $module.ko vcmd_supported=0 || exit 1
#insmod $module.ko vcmd_supported=1 || exit 1

echo "module $module inserted"

#remove old nod
rm -f $device

#read the major asigned at loading time
major=`cat /proc/devices | grep $module | cut -c1-3`

echo "$module major = $major"

#create dev node
mknod $device c $major 0

echo "node $device created"

#give all 'rw' access
chmod $mode $device

echo "set node access to $mode"

#the end
echo
