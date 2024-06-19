#!/bin/bash
BASEDIR=$(dirname $(readlink -f "$0"))
PARENT_DIR=$(readlink -f "$BASEDIR"/..)
env_file=$PARENT_DIR/tools/build.profile
[ ! -f $env_file ] && echo "haven't found env file[$env_file]" >&2 && exit 0

show_help() {
    RET=$?
    cat<<EOF
`basename $0` Build 2D kernel script V1.0
Usage: `basename $0` <kernel_dir> [toolchain_dir] [compile options]
    kernel_dir,      it means the kernel directory
    toolchain_dir,   it means toolchain directory. default it try to find in the env.
                     you also can specify the toolchain directory.
    compile options, the extra compile if need to specify. [ `cat $BASEDIR/Kbuild |egrep "ifeq|ifneq"|grep -oP '[A-Z_]+'|sort|uniq|tr "\n" " "`]
for example,
    `basename $0` ~/win2030/linux-5.17 ~/toolschain DEBUG=1
    `basename $0` ~/win2030/linux-5.17 ~/toolschain DEBUG=1 install
    `basename $0` ~/win2030/linux-5.17 DEBUG=1
    `basename $0` ~/win2030/linux-5.17
    `basename $0` ~/win2030/linux-5.17 clean
    `basename $0` ~/win2030/linux-5.17 install
EOF
    exit $RET
}

if [ $# -eq 0 ] || [[ "$1" = "-h*" ]];then
    show_help
fi

check_kernel_dir_valid() {
    kernel_dir=$1
    if [ ! -d $kernel_dir ];then
        echo "No such kernel directory[$kernel_dir]" >&2
        exit 1
    fi
    if [ ! -d $kernel_dir/kernel ];then
        echo "You given kernel directory[$kernel_dir] was not correct, please double check!" >&2
        exit 1
    fi
}
#parse the kernel directory from command line
check_kernel_dir_valid $1
kernel_dir=$1
echo "kernel directory   :  $kernel_dir"
shift

source $env_file

#configure the toolchain
toolchain_given=""
is_dir_or_path_pattern "$1"
[ $? -eq 0 ] && toolchain_given="$1" && shift

guess_toolchain "$toolchain_given" "/opt" "$kernel_dir" "$BASEDIR"

parse_compile_flags $@

export AQROOT=$BASEDIR
export ARCH_TYPE=riscv
export KERNEL_DIR=$kernel_dir
export SDK_DIR=$PARENT_DIR/sdk
export ARCH=riscv
export SOC_PLATFORM=eswin-win2030 #SOC_PLATFORM string format should be <vendor>-<board>
export VIVANTE_ENABLE_DRM=0
export VIVANTE_ENABLE_3D=0
export VIVANTE_ENABLE_VG=0
[ ! -d $SDK_DIR ] && mkdir -p $SDK_DIR

echo "compile extra flags: ${EXTRA_FLAGS[@]}"
echo "make ${EXTRA_FLAGS[@]}"
make -C $BASEDIR ESW_DEBUG_PERF=0 ${EXTRA_FLAGS[@]}
