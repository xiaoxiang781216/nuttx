#!/usr/bin/env bash
# tools/build.sh
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

function cleanup()
{
  # keep the mapping but change to the link since:
  # 1.root can't access mount point created by normal user
  # 2.debugger could find the source code without manual setting
  fusermount -u ${MOUNTDIR}
  rmdir ${MOUNTDIR}
  ln -s ${ROOTDIR} ${MOUNTDIR}
}

function mount_unionfs()
{
  echo -e "Mount command line:"
  echo -e "  unionfs-fuse -o cow ${OUTDIR}=RW:${ROOTDIR}=RO ${MOUNTDIR}"

  rm -f ${MOUNTDIR}
  mkdir -p ${MOUNTDIR}
  unionfs-fuse -o cow ${OUTDIR}=RW:${ROOTDIR}=RO ${MOUNTDIR}
}

function setup_environment()
{
  PACKAGES=( \
      "autoconf" \
      "automake" \
      "bison" \
      "build-essential" \
      "dfu-util" \
      "genromfs" \
      "flex" \
      "git" \
      "gperf" \
      "libncurses5" \
      "lib32ncurses5-dev" \
      "libc6-dev-i386" \
      "libx11-dev" \
      "libx11-dev:i386" \
      "libxext-dev" \
      "libxext-dev:i386" \
      "net-tools" \
      "pkgconf" \
      "unionfs-fuse" \
      "zlib1g-dev" \
      "kconfig-frontends" \
      "g++-11" \
      "g++-11-multilib" \
      "libpulse-dev:i386" \
      "libasound2-dev:i386" \
      "libasound2-plugins:i386" \
      "libusb-1.0-0-dev" \
      "libusb-1.0-0-dev:i386" \
      "libmad0-dev:i386" \
      "libv4l-dev" \
      "libv4l-dev:i386" \
      "libuv1-dev" \
      "libmp3lame-dev:i386" \
      "libmad0-dev:i386" \
      "libv4l-dev:i386" \
      "npm" \
      "nodejs" \
      "xxd" \
      "qemu-system-arm" \
      "qemu-efi-aarch64" \
      "qemu-utils" \
      "nasm" \
      "yasm" \
      "libdivsufsort-dev" \
      "libc++-dev" \
      "libc++abi-dev" \
      "libprotobuf-dev" \
      "protobuf-compiler" \
      "protobuf-c-compiler" \
      "gcc-multilib" \
      "g++-multilib" \
      )

  for (( i = 0; i < ${#PACKAGES[*]}; i++)); do
    dpkg -l ${PACKAGES[$i]} > /dev/null 2>&1
    if [ $? -eq 1 ]; then
      echo "WARNING: no packages found matching ${PACKAGES[$i]}"
      INSTALLS[${#INSTALLS[@]}]=${PACKAGES[$i]}
    fi
  done

  if [ ${#INSTALLS[*]} -eq 0 ]; then
    return
  fi

  if [ ${#INSTALLS[*]} -eq 1 ] && [ "${INSTALLS[0]}" == "kconfig-frontends" ]; then
    return
  fi

  echo "*************************************************************************************"
  echo "The environment of Vela depends on above tools, Run the following command to install:"
  echo ""
  echo " sudo dpkg --add-architecture i386"

  for (( i = 0; i < ${#INSTALLS[*]}; i++)); do
    result=`apt-cache search ${INSTALLS[$i]}`
    if [ "$result" == "" ]; then
      if [ "${INSTALLS[$i]}" == "kconfig-frontends" ]; then
        unset INSTALLS[$i]
      fi
    fi
  done

  echo " sudo apt-get install -y software-properties-common"
  echo " sudo add-apt-repository ppa:ubuntu-toolchain-r/test"
  echo " sudo apt-get update"
  echo " sudo apt-get install -y ${INSTALLS[@]}"
  echo ""
  echo "*************************************************************************************"
}

function setup_toolchain()
{
  V='\033[0;97m'
  E='\033[0;94m'
  L='\033[0;96m'
  A='\033[0;94m'
  B='\033[0;34m'
  N='\033[0m'
  echo -e "${B}**""**""**""**""**""**""**""**""**""**""**""**""**""***${N}"
  echo -e "${B}*${V} __      __   ${E}   ""${L}_ ${A}      "   " ${B}*${N}"
  echo -e "${B}* ${V}\\ \\    / /${E}     ${L}| |${A}      "   " ${B}*${N}"
  echo -e "${B}*  ${V}\\ \\  / /${E} ___  ${L}| |${A}  __ _"   " ${B}*${N}"
  echo -e "${B}*   ${V}\\ \\/ /${E} / _ \\ ${L}| |${A} / _\` |" "${B}*${N}"
  echo -e "${B}*    ${V}\\  / ${E}|  __/ ${L}| |${A}| (_| |"    "${B}*${N}"
  echo -e "${B}*     ${V}\\/ ${E}  \\___| ${L}|_|${A} \\__,_|"  "${B}*${N}"
  echo -e "${B}*           "                         "               *${N}"
  echo -e "${B}**""**""**""**""**""**""**""**""**""**""**""**""**""***${N}"

  SYSTEM=`uname | tr '[:upper:]' '[:lower:]'`
  SYS_ARCH=`uname -m | sed 's/arm64/aarch64/'`

  if [ ${SYSTEM} == "darwin" ]; then
    export MACOSX_DEPLOYMENT_TARGET=11
    BONJOUR=" Welcome, dear macOS user "
    for ((i=0;i<${#BONJOUR};i++)); do
      printf "\e[38;5;$(($RANDOM%213+19))m${BONJOUR:i:1}\e[0m"
    done
    echo #
  fi

  ARCH=(\
      "xtensa" \
      "arm" \
      "arm64" \
      "risc-v" \
      "tc32" )

  TOOLCHAIN=(\
            "gcc" \
            "clang" )

  if [ "$XTENSAD_LICENSE_FILE" == "" ]; then
    export XTENSAD_LICENSE_FILE=28000@10.38.168.2
  fi
  export WASI_SDK_PATH=${ROOTDIR}/prebuilts/clang/${SYSTEM}/wasm
  export PATH=${WASI_SDK_PATH}:$PATH
  export PYTHONPATH=${PYTHONPATH}:${ROOTDIR}/prebuilts/tools/python/dist-packages/pyelftools
  export PYTHONPATH=${PYTHONPATH}:${ROOTDIR}/prebuilts/tools/python/dist-packages/cxxfilt

  for (( i = 0; i < ${#ARCH[*]}; i++)); do
    for (( j = 0; j < ${#TOOLCHAIN[*]}; j++)); do
      if [ -d ${ROOTDIR}/prebuilts/${TOOLCHAIN[$j]}/${SYSTEM}-${SYS_ARCH}/${ARCH[$i]}/bin ]; then
        export PATH=${ROOTDIR}/prebuilts/${TOOLCHAIN[$j]}/${SYSTEM}-${SYS_ARCH}/${ARCH[$i]}/bin:$PATH
      elif [ -d ${ROOTDIR}/prebuilts/${TOOLCHAIN[$j]}/${SYSTEM}/${ARCH[$i]}/bin ]; then
        export PATH=${ROOTDIR}/prebuilts/${TOOLCHAIN[$j]}/${SYSTEM}/${ARCH[$i]}/bin:$PATH
      fi
    done
  done

  # Arm Compiler
  export PATH=${ROOTDIR}/prebuilts/clang/${SYSTEM}/armclang/bin:$PATH

  if [ ! -n "${ARM_PRODUCT_DEF}" ]; then
    export ARM_PRODUCT_DEF=${ROOTDIR}/prebuilts/clang/${SYSTEM}/armclang/mappings/eval.elmap
  fi
  if [ ! -n "${LM_LICENSE_FILE}" ]; then
    export LM_LICENSE_FILE=${HOME}/.arm/ds/licenses/DS000-EV-31030.lic
  fi
  if [ ! -n "${ARMLMD_LICENSE_FILE}" ]; then
    export ARMLMD_LICENSE_FILE=${HOME}/.arm/ds/licenses/DS000-EV-31030.lic
  fi

  # Generate compile database file compile_commands.json
  if type bear >/dev/null 2>&1; then
    # get version of bear
    BEAR_VERSION=$(bear --version | awk '{print $2}' | awk -F. '{printf("%d%03d%03d ", $1,$2,$3)}')

    # judge version of bear
    if [ $BEAR_VERSION -ge 3000000 ]; then
        # BEAR="bear --append --output compile_commands.json -- "
        echo -e "Note: currently not support bear 3.0.0+ for some prebuilt toolchain limited."
    else
        echo -e "Note: bear 2.4.3 in Ubuntu 20.04 works out of box."
        COMPILE_COMMANDS_DB_PATH="${ROOTDIR}/compile_commands"
        if [ ! -d "$COMPILE_COMMANDS_DB_PATH" ]; then
            mkdir -p $COMPILE_COMMANDS_DB_PATH
        fi

        COMPILE_COMMANDS=${ROOTDIR}/compile_commands.json
        COMPILE_COMMANDS_BACKUP=${COMPILE_COMMANDS_DB_PATH}/compile_commands_${1//\//_}_$(date "+%Y-%m-%d-%H-%M-%S").json
        BEAR="bear -a -o ${COMPILE_COMMANDS} "
    fi
  fi

  # Add prebuilt tool
  TOOLS_DIR=${ROOTDIR}/prebuilts/tools/${SYSTEM}/${SYS_ARCH}
  export PATH="${TOOLS_DIR}:$PATH"

  # Additional prebuilt GNU tools
  if [ ${SYSTEM} == "darwin" ]; then
    export PATH=${ROOTDIR}/prebuilts/tools/gnu/${SYSTEM}/${SYS_ARCH}:$PATH
    export PATH=${ROOTDIR}/prebuilts/tools/gnu/${SYSTEM}/universal:$PATH
  fi
}

function build_board()
{
  echo -e "Build command line:"
  echo -e "  ${TOOLSDIR}/configure.sh -e $1"
  echo -e "  make -C ${NUTTXDIR} EXTRAFLAGS="$EXTRA_FLAGS" ${@:2}"
  echo -e "  make -C ${NUTTXDIR} savedefconfig"

  KCONFIG_ARGS="--enable-mconf --disable-nconf --disable-gconf --disable-qconf"
  if [ `uname` == "Darwin" ]; then
    KCONFIG_ARGS+=" --disable-shared --enable-static"
  fi

  if [ ! -f "${ROOTDIR}/prebuilts/kconfig-frontends/bin/kconfig-conf" ] &&
     [ ! -x "$(command -v kconfig-conf)" ]; then
    pushd ${ROOTDIR}/prebuilts/kconfig-frontends
    ./configure --prefix=${ROOTDIR}/prebuilts/kconfig-frontends ${KCONFIG_ARGS} 1>/dev/null
    touch aclocal.m4 Makefile.in
    make install 1>/dev/null
    popd
  fi
  export PATH=${ROOTDIR}/prebuilts/kconfig-frontends/bin:$PATH

  setup_toolchain $1

  if ! ${TOOLSDIR}/configure.sh -e $1; then
    echo "Error: ############# config ${1} fail ##############"
    exit 1
  fi

  if ! ${BEAR} make -C ${NUTTXDIR} EXTRAFLAGS="$EXTRA_FLAGS" ${@:2}; then
    echo "Error: ############# build ${1} fail ##############"
    exit 2
  else
    if [ -f "${COMPILE_COMMANDS}" ]; then
      cp ${COMPILE_COMMANDS} ${COMPILE_COMMANDS_BACKUP}
    fi
  fi

  if [ "${2}" == "distclean" ]; then
    if [ -f "${COMPILE_COMMANDS}" ]; then
      rm -rf ${COMPILE_COMMANDS}
    fi
    return;
  fi

   if ! make -C ${NUTTXDIR} savedefconfig; then
    echo "Error: ############# save ${1} fail ##############"
    exit 3
  fi
  if [ ! -d $1 ]; then
    cp ${NUTTXDIR}/defconfig ${ROOTDIR}/nuttx/boards/*/*/${1/[:|\/]//configs/}
  else
    cp ${NUTTXDIR}/defconfig $1
  fi
}

function pack_sdk()
{
  VENDOR=(\
      "realtek" \
      "bes" )
  BOARDS=(\
      "
      amebaZ
      " \
      "
      best2001_ep
      " \
      )

  if [ "${2}" != "export" ]; then
    exit 0
  fi

  if [ ! -f $OUTDIR/nuttx/.version ]; then
    exit 1
  fi

  source $OUTDIR/nuttx/.version

  local exportzip=$OUTDIR/nuttx/nuttx-export-$CONFIG_VERSION_STRING.zip

  echo $exportzip
  if [ ! -f $exportzip ]; then
    exit 1
  fi

  local version=$CONFIG_VERSION_STRING.$CONFIG_VERSION_BUILD
  local board=`echo $1 | awk -F '/' '{print $4}'`
  local packname=`echo nuttx-sdk-$board-$version.zip`
  local vendor=

  for (( i = 0; i < ${#BOARDS[*]}; i++)); do
    if [ $board == ${BOARDS[$i]} ]; then
      vendor=vendor/${VENDOR[$i]}
      break
    fi
  done

  cp $exportzip .
  zip -r $packname sdk $vendor `basename $exportzip` -x "sdk/.git*" 1>/dev/null 2>&1

  if [ $? -eq 0 ]; then
    echo "############## Export SDK Success: $packname ##############"
  fi

  rm `basename $exportzip`
}

if [ $# == 0 ]; then
  echo "Usage: $0 [-m] <board-name>:<config-name> [-e <extraflags>] [make options]"
  echo ""
  echo "Where:"
  echo "  -m: out of tree build. Or default in tree build without it."
  echo "  -e: pass extra c/c++ flags such as -Werror via make command line"
  exit 1
fi

ROOTDIR=$(dirname $(readlink -f ${0}))
ROOTDIR=$(realpath ${ROOTDIR}/../..)

setup_environment

CONFIGPATH=$2

if [ $1 == "-m" ]; then
  # out of tree build
  confparams=(${CONFIGPATH//:/ })
  configdir=${confparams[1]}

  if [ -z "${configdir}" ]; then
    # handle cases where the end is a "/"
    if [ "${CONFIGPATH:(-1)}" = "/" ]; then
      CONFIGPATH=${CONFIGPATH:0:-1}
    fi
    boarddir=`echo ${CONFIGPATH} | rev | cut -d'/' -f3 | rev`
    configdir=`echo ${CONFIGPATH} | rev | cut -d'/' -f1 | rev`
  else
    boarddir=${confparams[0]}
  fi

  OUTDIR=${ROOTDIR}/out/${boarddir}/${configdir}
  MOUNTDIR=${OUTDIR}/.unionfs
  NUTTXDIR=${MOUNTDIR}/nuttx

  trap cleanup EXIT
  mount_unionfs
  shift
else
  # in tree build
  OUTDIR=${ROOTDIR}
  NUTTXDIR=${ROOTDIR}/nuttx
fi

TOOLSDIR=${NUTTXDIR}/tools
board_config=$1
shift

EXTRA_FLAGS="-Wno-cpp"
if [ "$1" == "-e" ]; then
  shift
  EXTRA_FLAGS+=" $1"
  echo "extraflags: $EXTRA_FLAGS"
  shift
fi

if [ -d ${ROOTDIR}/${board_config} ]; then
  build_board ${ROOTDIR}/${board_config} $*
else
  build_board ${board_config} $*
fi

pack_sdk ${board_config} $*
