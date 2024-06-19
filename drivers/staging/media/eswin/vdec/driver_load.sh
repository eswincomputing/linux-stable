#  Copyright 2013 Google Inc. All Rights Reserved.

module="es_vdec"
dev_path="/dev"
device="${dev_path}/${module}"
mode="666"

echo
echo "Usage:"
echo "    ./driver_load.sh vcmd=1"
echo "desc:"
echo "    vcmd=1 - vcmd mode"
echo "    vcmd=0 - normal mode"

if [ ! -e ${dev_path} ]
then
    mkdir -p ${dev_path}/
fi

#insert module
rm_module=`lsmod |grep $module`
if [ ! -z "$rm_module" ]
then
   rmmod $module || exit 1
fi
modprobe $module.ko $* || exit 1

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
