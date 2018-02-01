#!/bin/bash
RUNSCRIPT=$(realpath $0)
TASVIR_SRCDIR=$(realpath `dirname $RUNSCRIPT`/..)
TASVIR_BINDIR=$TASVIR_SRCDIR/build/bin
PIDFILE_PREFIX=/var/run/tasvir-

declare -A HOST_NIC=( ["c12"]="87:00.0" ["c13"]="83:00.0" ["c15"]="87:00.0" ["c101"]="05:00.0" ["c102"]="05:00.0" ["c103"]="05:00.0" ["c104"]="05:00.0" ["c105"]="05:00.0" ["c106"]="05:00.0" ["c107"]="05:00.0" ["c108"]="05:00.0" ["c109"]="05:00.0" ["c110"]="05:00.0" ["c111"]="05:00.0" ["c112"]="05:00.0" ["c113"]="05:00.0" ["c114"]="05:00.0" ["c115"]="05:00.0" ["c116"]="05:00.0" ["c121"]="05:00.0" ["c122"]="05:00.0" ["c123"]="05:00.0" ["c124"]="05:00.0" ["c125"]="05:00.0" ["c126"]="05:00.0" ["c127"]="05:00.0" ["c128"]="05:00.0" ["c129"]="05:00.0" ["c130"]="05:00.0" ["c131"]="05:00.0" ["c132"]="05:00.0" ["c133"]="05:00.0" ["c134"]="05:00.0" ["c135"]="0a:00.0" ["c136"]="0a:00.0" )
declare -A HOST_NCORES=( ["c12"]=24 ["c13"]=24 ["c15"]=36 ["c101"]=16 ["c102"]=16 ["c103"]=16 ["c104"]=16 ["c105"]=16 ["c106"]=16 ["c107"]=16 ["c108"]=16 ["c109"]=16 ["c110"]=16 ["c111"]=16 ["c112"]=16 ["c113"]=16 ["c114"]=16 ["c115"]=16 ["c116"]=16 ["c121"]=16 ["c122"]=16 ["c123"]=16 ["c124"]=16 ["c125"]=16 ["c126"]=16 ["c127"]=16 ["c128"]=16 ["c129"]=16 ["c130"]=16 ["c131"]=16 ["c132"]=16 ["c133"]=16 ["c134"]=16 ["c135"]=16 ["c136"]=16 )
# root must come first
declare -a HOST_ALL=(c15 c12 c13 c101 c102 c103 c104 c105 c106 c107 c108 c109 c110 c111 c112 c113 c114 c115 c116 c121 c122 c123 c124 c125 c126 c127 c128 c129 c130 c131 c132 c133 c134 c135 c136)

prepare() {
    echo never >/sys/kernel/mm/transparent_hugepage/enabled
    sysctl vm.nr_hugepages=1050 &>/dev/null

    modprobe igb_uio &>/dev/null
    $RTE_SDK/usertools/dpdk-devbind.py --force -b igb_uio ${HOST_NIC[$HOSTNAME]} &>/dev/null

    for pidfile in ${PIDFILE_PREFIX}*; do
        start-stop-daemon --stop --retry 3 --remove-pidfile --pidfile $pidfile &>/dev/null
    done
    rm -f /dev/shm/tasvir /dev/hugepages/tasvir* /var/run/.tasvir_config &>/dev/null

    echo 1 > /sys/module/processor/parameters/ignore_ppc
    echo 0 > /proc/sys/kernel/nmi_watchdog
    [ -f /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq ] && max_freq=$(cat /sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq)
    for cpu in /sys/devices/system/cpu/cpu[0-9]*; do
        # fix core frequency
        if [ -d $cpu/cpufreq ]; then
            echo performance > $cpu/cpufreq/scaling_governor
            echo $max_freq > $cpu/cpufreq/scaling_min_freq
            echo $max_freq > $cpu/cpufreq/scaling_max_freq
        fi
        # disable c states
        if [ -d $cpu/cpuidle ]; then
            for state in $cpu/cpuidle/state[1-9]/disable; do
                echo 1 > $state/disable
            done
        fi
        # disable p states
        if [ -d /sys/devices/system/cpu/intel_pstate ]; then
            echo 1 > /sys/devices/system/cpu/intel_pstate/no_turbo
            echo 100 > /sys/devices/system/cpu/intel_pstate/min_perf_pct
            echo 100 > /sys/devices/system/cpu/intel_pstate/max_perf_pct
        fi
    done &>/dev/null
    # disable uncore frequency scaling
    wrmsr -a 0x620 0x1d1d
}

compile() {
    export CC=$(which clang)
    export CXX=$(which clang++)

    mkdir $TASVIR_SRCDIR/build &>/dev/null
    cd $TASVIR_SRCDIR/build
    cmake -DCMAKE_BUILD_TYPE=Release .. || exit
    make || exit
}

generate_cmd() {
    generate_cmd_thread() {
        local bgflag
        local pmucmd
        local gdbcmd
        local pmucmd="/opt/tools/pmu-tools/toplev.py -l4 -S"
        local stdbufcmd="stdbuf -o 0 -e 0"
        local redirect
        if [ $tid == d -o $tid == 0 ]; then
            #gdbcmd="gdb -ex run --args"
            redirect="2>&1 | tee $logfile"
        else
            bgflag="--background"
            redirect=">$logfile 2>&1"
        fi
        bgflag=""
        #gdbcmd="gdb -ex run --args"
        redirect="2>&1 | tee $logfile"

        cmd_thread="start-stop-daemon $bgflag --start --make-pidfile --pidfile ${PIDFILE_PREFIX}%TID%.pid --startas /bin/bash -- -c
                    \"exec /usr/bin/numactl -C %CORE% $gdbcmd $stdbufcmd $* $redirect\""
        cmd_thread=$(echo $cmd_thread | sed -e s/%CORE%/$core/g -e s/%TID%/$tid/g -e s/%NTHREADS%/$nthreads/g -e s/%HOST%/$host/g)
        ((core--))
    }

    local nthreads=${nthreads:-1}
    local delay=${delay:-2}

    local host_counter=0
    local host_list=("${host_list[@]:-${HOST_ALL[@]}}")
    local host=${host_list[0]}

    local cmd
    local cmd_app="$*"
    local cmd_ssh
    local cmd_thread
    local session=tasvir_run
    local w=-1
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    local window
    local logdir="$(dirname $RUNSCRIPT)/log/$timestamp"
    local manifest="$logdir/manifest"
    local logfile="$logdir/t%TID%.%HOST%"
    local threads_this=0

    local cmd_byobu="byobu "
    cmd_byobu+="set-option -gq mouse-utf8 on\; "
    cmd_byobu+="set-option -gq mouse-resize-pane on\; "
    cmd_byobu+="set-option -gq mouse-select-pane on\; "
    cmd_byobu+="set-option -gq mouse-select-window on\; "
    cmd_byobu+="set-window-option -gq mode-mouse on\; "
    cmd_byobu+="set-window-option -gq remain-on-exit off\; "
    tmux has-session -t $session &>/dev/null || cmd_byobu+="new-session -Ads $session\; "

    cmd="mkdir -p $logdir &>/dev/null;"
    cmd+="$cmd_byobu"
    [ -z $TMUX ] && cmd+="attach-session -t $session\; " || cmd+="switch-client -t $session\; "

    for tid in `seq 0 $((nthreads - 1))`; do
        [ $tid -ne 0 -a $((tid % 16)) -eq 0 ] && cmd+="; $cmd_byobu "
        if [ $threads_this -eq "${host_nthreads[$host]}" ]; then
            ((host_counter++))
            host=${host_list[$host_counter]}
            threads_this=0
        fi
        if [ $threads_this -eq 0 ]; then
            [ $tid -ne 0 ] && cmd+="select-layout "$([ $w -le 1 ] && echo main-horizontal || echo tiled)"\; "
            # run the daemon
            local pciaddr=${HOST_NIC[$host]}
            local core=$((HOST_NCORES[$host] - 1))
            local cmd_daemon="$TASVIR_BINDIR/tasvir_daemon --core %CORE% --pciaddr $pciaddr"$([ $tid -eq 0 ] && echo " --root")
            local tid2=$tid
            tid=d
            cmd_ssh=$([ $HOSTNAME != "$host" ] && echo "ssh -t $host")
            w=-1
            window=$host-n$nthreads-$timestamp-$((++w))
            generate_cmd_thread $cmd_daemon
            cmd+="new-window -t $session -n $window $cmd_ssh '. $RUNSCRIPT; prepare; $cmd_thread; prepare;'\; "
            cmd+="split-window -t $session:$window "
            tid=$tid2
            threads_this=1
        elif [ $((threads_this % 4)) -eq 0 ]; then
            cmd+="select-layout "$([ $w -eq 1 ] && echo main-horizontal || echo tiled)"\; "
            window=$host-$nthreads-$timestamp-$((++w))
            cmd+="new-window -t $session -n $window "
        else
            cmd+="split-window -t $session:$window "
        fi

        # run the worker
        local cmd_last=  # ". $RUNSCRIPT; prepare;"
        generate_cmd_thread $cmd_app
        cmd+="$cmd_ssh 'ulimit -c unlimited; sleep $delay; $cmd_thread; $cmd_last'\; "
        ((threads_this++))
    done

    cmd+="select-layout "$([ $w -eq 0 ] && echo main-horizontal || echo tiled)"\; "
    cmd+="new-window -t $session -n 0 'echo -e nthreads=$nthreads cmd_app=$cmd_app > $manifest' ;"
    echo "$cmd"
}

benchmark() {
    nr_writes=${nr_writes:-$((50 * 1000 * 1000))}
    writes_per_service=${writes_per_service:-100}
    area_len=${area_len:-$((100 * 1024 * 1024))}
    stride=${stride:-8}
    sync_int=${sync_int:-10000}
    sync_ext=${sync_ext:-100000}

    # first host is root
    declare -a host_list=( c15 c12 c13 c101 c102 c103 c104 c105 c106 c107 c108 c109 c110 c111 c112 c113 c114 c115 c116 c121 c122 c123 c124 c125 c126 c127 c128 c129 c130 c131 c132 c133 c134 c135 c136 )
    declare -A host_nthreads=( ["c12"]=2 ["c13"]=2 ["c15"]=2 ["c101"]=2 ["c102"]=2 ["c103"]=2 ["c104"]=2 ["c105"]=2 ["c106"]=2 ["c107"]=2 ["c108"]=2 ["c109"]=2 ["c110"]=2 ["c111"]=2 ["c112"]=2 ["c113"]=2 ["c114"]=2 ["c115"]=2 ["c116"]=2 ["c121"]=2 ["c122"]=2 ["c123"]=2 ["c124"]=2 ["c125"]=2 ["c126"]=2 ["c127"]=2 ["c128"]=2 ["c129"]=2 ["c130"]=2 ["c131"]=2 ["c132"]=2 ["c133"]=2 ["c134"]=2 ["c135"]=2 ["c136"]=2 )
    nthreads=${nthreads:-1}

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_benchmark %CORE% $nr_writes $writes_per_service $area_len $stride $sync_int $sync_ext)
}

monitor_remote() {
    # first host is root
    declare -a host_list=( c111 c112 c113 c114 c115 c116 c121 c102 c103 c123 c124 c125 c126 c127 c128 c129 c130 c131 c133 c134 c136 )
    declare -A host_nthreads=( ["c102"]=2 ["c103"]=2 ["c104"]=2 ["c111"]=2 ["c112"]=2 ["c113"]=2 ["c114"]=2 ["c115"]=2 ["c116"]=2 ["c121"]=2 ["c122"]=2 ["c123"]=2 ["c124"]=2 ["c125"]=2 ["c126"]=2 ["c127"]=2 ["c128"]=2 ["c129"]=2 ["c130"]=2 ["c131"]=2 ["c133"]=2 ["c134"]=2 ["c136"]=2 )
    nthreads=$1
    local syncint=$2
    local syncext=$3
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/log/monitor/remote.$timestamp.n%NTHREADS%.$syncint.$syncext.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_monitor %TID% %CORE% %NTHREADS% $syncint $syncext)
}

monitor_local() {
    # first host is root
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=8 )
    nthreads=$1
    local syncint=$2
    local syncext=$3
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/log/monitor/local.$timestamp.n%NTHREADS%.$syncint.$syncext.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_monitor %TID% %CORE% %NTHREADS% $syncint $syncext)
}

router_remote() {
    # first host is root
    declare -a host_list=( c111 c112 c113 c114 c115 c116 c121 c102 c103 c123 c124 c125 c126 c127 c128 c129 c130 c131 c133 c134 c136 )
    declare -A host_nthreads=( ["c102"]=2 ["c103"]=2 ["c104"]=2 ["c111"]=2 ["c112"]=2 ["c113"]=2 ["c114"]=2 ["c115"]=2 ["c116"]=2 ["c121"]=2 ["c122"]=2 ["c123"]=2 ["c124"]=2 ["c125"]=2 ["c126"]=2 ["c127"]=2 ["c128"]=2 ["c129"]=2 ["c130"]=2 ["c131"]=2 ["c133"]=2 ["c134"]=2 ["c136"]=2 )
    nthreads=$1
    local subthreads=$2
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/log/router/local.$timestamp.n%NTHREADS%.$subthreads.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/router %TID% %CORE% %NTHREADS% $subthreads)
}

router_local() {
    # first host is root
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=24 )
    nthreads=$1
    local subthreads=$2
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/log/router/local.$timestamp.n%NTHREADS%.$subthreads.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/router %TID% %CORE% %NTHREADS% $subthreads)
}


tasvir_openflow_local() {
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=4 )
    nthreads=${nthreads:-2}
    local syncint=$1
    local syncext=$2
    logfile="$(dirname $RUNSCRIPT)/openflow/tasvir.uniform.local.n%NTHREADS%.$syncint.$syncext.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_openflow %TID% %CORE% %NTHREADS% $syncint $syncext)
}

tasvir_openflow_remote() { ## should be run in local
    declare -a host_list=( c13 c12 )
    declare -A host_nthreads=( ["c13"]=2 ["c12"]=2 )
    nthreads=${nthreads:-2}
    local syncint=$1
    local syncext=$2
    logfile="$(dirname $RUNSCRIPT)/openflow/tasvir.uniform.remote.n%NTHREADS%.$syncint.$syncext.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_openflow %TID% %CORE% %NTHREADS% $syncint $syncext)
}

tasvir_openflow_skew() { ## should be run in local
    declare -a host_list=( c13 c12 )
    declare -A host_nthreads=( ["c13"]=2 ["c12"]=2 )
    nthreads=${nthreads:-2}

    local syncint=$1
    local syncext=$2
    logfile="$(dirname $RUNSCRIPT)/openflow/tasvir.skew.remote.n%NTHREADS%.$syncint.$syncext.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/tasvir_openflow_skew %TID% %CORE% %NTHREADS% $syncint $syncext)
}

openflow() { ## should be run in local
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=4 )
    nthreads=${nthreads:-2}
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/openflow/small.uniform.$timestamp.n%NTHREADS%.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/openflow %TID% %CORE% %NTHREADS%)
}

openflow_skew() { ## should be run in local
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=4 )
    nthreads=${nthreads:-2}
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/openflow/small.skew.$timestamp.n%NTHREADS%.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/openflow_skew %TID% %CORE% %NTHREADS%)
}

openflow_big() { ## should be run in local
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=4 )
    nthreads=${nthreads:-2}
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/openflow/big.uniform.$timestamp.n%NTHREADS%.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/openflow_big %TID% %CORE% %NTHREADS%)
}

openflow_big_skew() { ## should be run in local
    declare -a host_list=( c13 )
    declare -A host_nthreads=( ["c13"]=4 )
    nthreads=${nthreads:-2}
    local timestamp=`date +"%Y%m%d-%H%M%S"`
    logfile="$(dirname $RUNSCRIPT)/openflow/big.skew.$timestamp.n%NTHREADS%.%HOST%.t%TID%"

    eval $(generate_cmd $TASVIR_BINDIR/openflow_big_skew %TID% %CORE% %NTHREADS%)
}

tasvir_lb() {
    pkill -f tasvir_daemon
    pkill -f "cuckoo-lb "
    for i in c{104..107}; do
        ssh $i pkill -f tasvir_daemon &
        ssh $i pkill -f cuckoo-lb &
    done
    sleep 1
    #declare -a host_list=( c104 c105 c106 c107 c108 c109 c110 c111 c112 c113 c114 c115 c116 c121 c122 c123 c124 c125 c126 c127 c128 c129 c130 c131 c132 c133 c134 c135 c136 )
    declare -a host_list=( c103 c104 c105 c106 c107 c108 c109 c110 c111 c112 c113 c114 c115 c116 c123 c124 c125 c126 c127 c128 c129 c130 c131 c132 c133 c134 c121 c122 )
    declare -A host_nthreads=( ["c12"]=2 ["c13"]=2 ["c15"]=2 ["c101"]=2 ["c102"]=2 ["c103"]=2 ["c104"]=2 ["c105"]=2 ["c106"]=2 ["c107"]=2 ["c108"]=2 ["c109"]=2 ["c110"]=2 ["c111"]=2 ["c112"]=2 ["c113"]=2 ["c114"]=2 ["c115"]=2 ["c116"]=2 ["c121"]=2 ["c122"]=2 ["c123"]=2 ["c124"]=2 ["c125"]=2 ["c126"]=2 ["c127"]=2 ["c128"]=2 ["c129"]=2 ["c130"]=2 ["c131"]=2 ["c132"]=2 ["c133"]=2 ["c134"]=2 ["c135"]=2 ["c136"]=2 )
    # Not sure what goes here.
    backends=${backends:-8}
    partitions=${partitions:-16}
    nthreads=${nthreads:-1} # Why is this here?
    eval $(generate_cmd $TASVIR_BINDIR/cuckoo-lb %TID% %CORE% ${partitions} %NTHREADS% ${backends})
}

tasvir_lb_local() {
    pkill -f tasvir_daemon
    pkill -f "cuckoo-lb "
    for i in c{104..107}; do
        ssh $i pkill -f tasvir_daemon &
        ssh $i pkill -f cuckoo-lb &
    done
    sleep 1
    #declare -a host_list=( c104 c105 c106 c107 c108 c109 c110 c111 c112 c113 c114 c115 c116 c121 c122 c123 c124 c125 c126 c127 c128 c129 c130 c131 c132 c133 c134 c135 c136 )
    declare -a host_list=( c15 )
    declare -A host_nthreads=( ["c15"]=18 )
    # Not sure what goes here.
    backends=${backends:-8}
    partitions=${partitions:-16}
    nthreads=${nthreads:-1}
    eval $(generate_cmd $TASVIR_BINDIR/cuckoo-lb %TID% %CORE% ${partitions} %NTHREADS% ${backends})
}

setup_env() {
    # Timezone
    ln -fs /usr/share/zoneinfo/America/Los_Angeles /etc/localtime
    dpkg-reconfigure -f noninteractive tzdata

    # Upgrade
    # sed -i -e s/jessie/testing/g -e s/stretch/testing/g /etc/apt/sources.list
    apt-get update
    apt-get -y dist-upgrade

    # Packages
    apt-get install -y \
        aptitude build-essential byobu clang clang-format clang-tidy cmake curl gdb git llvm-dev python3 python3-pip uthash-dev uuid-dev vim-nox
    byobu-enable-prompt

    # DPDK
    cat >/etc/sysctl.d/50-dpdk.conf <<EOF
    vm.nr_hugepages = 1050
EOF
    service procps restart
    cat >/etc/profile.d/dpdk.sh <<EOF
export RTE_ARCH=x86_64
export RTE_SDK=/opt/resq/nf/dpdk-v17.05
export RTE_TARGET=resq
EOF
    chmod +x /etc/profile.d/dpdk.sh

    # Vim
    cat >/root/.vimrc.before.local <<EOF
let g:spf13_bundle_groups=['general', 'writing', 'programming', 'python', 'misc', 'youcompleteme']
let g:ycm_global_ycm_extra_conf = '~/.vim/.ycm_extra_conf.py'
EOF
    cat >/root/.vimrc.bundles.local <<EOF
Bundle 'rhysd/vim-clang-format'
EOF
    sh <(curl https://j.mp/spf13-vim3 -L)
    vim +BundleInstall! +BundleClean +q
    #cp /opt/.ycm_extra_conf.py /root/.vim/
    cd ~/.vim/bundle/YouCompleteMe
    YCM_CORES=1 python3 ./install.py --clang-completer --system-libclang
}

[ $# == 1 ] && $1
