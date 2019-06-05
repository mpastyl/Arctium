#!/bin/bash

# set to 1 for motelab testbeds
#export CFLAGS="$CFLAGS -DTINYOS_SERIAL_FRAMES=1"

# set COOJA to 1 for simulating Glossy in Cooja
export CFLAGS="$CFLAGS -DCOOJA=1"
export CFLAGS="$CFLAGS -DMY_APP=1"
export CFLAGS="$CFLAGS -DDISABLE_ETIMER=1"
export CFLAGS="$CFLAGS -DBACKOFF=50"
export CFLAGS="$CFLAGS -DAGGREGATE=1"
export CFLAGS="$CFLAGS -DID_BUFFER_SIZE=1"


export CFLAGS="$CFLAGS -DCRYSTAL_CONF_PERIOD=1.5"
export CFLAGS="$CFLAGS -DCRYSTAL_LOGGING=1"
export CFLAGS="$CFLAGS -DDISABLE_UART=0"
export CFLAGS="$CFLAGS -DCRYSTAL_SINK_ID=1" 
export CFLAGS="$CFLAGS -DSTART_EPOCH=1" 
export CFLAGS="$CFLAGS -DN_FULL_EPOCHS=1"

export CFLAGS="$CFLAGS -DTX_POWER=31 -DRF_CHANNEL=26 -DCONCURRENT_TXS=5 -DNUM_ACTIVE_EPOCHS=1 -DN_TX_S=4 -DN_TX_T=4 -DN_TX_A=4 -DDUR_S_MS=12 -DDUR_T_MS=12 -DDUR_A_MS=12 -DCRYSTAL_LONGSKIP=0 -DCRYSTAL_SYNC_ACKS=1"
export CFLAGS="$CFLAGS -DCRYSTAL_SINK_MAX_EMPTY_TS=3 -DCRYSTAL_MAX_SILENT_TAS=2 -DCRYSTAL_MAX_MISSING_ACKS=4 -DCRYSTAL_SINK_MAX_NOISY_TS=6"
export CFLAGS="$CFLAGS -DCRYSTAL_USE_DYNAMIC_NEMPTY=0"
export CFLAGS="$CFLAGS -DCCA_THRESHOLD=-32 -DCCA_COUNTER_THRESHOLD=80"
export CFLAGS="$CFLAGS -DCRYSTAL_PAYLOAD_LENGTH=8"
#export CFLAGS="$CFLAGS -DSHORT_LOGS=1"

#export CFLAGS="$CFLAGS -DCHHOP_MAPPING=CHMAP_nohop"
export CFLAGS="$CFLAGS -DCHHOP_MAPPING=CHMAP_nomap"
#export CFLAGS="$CFLAGS -DBOOT_CHOPPING=BOOT_nohop"
export CFLAGS="$CFLAGS -DBOOT_CHOPPING=BOOT_hop3"

export CFLAGS="$CFLAGS -DCRYSTAL_START_DELAY_NONSINK=0 -DCRYSTAL_START_DELAY_SINK=0"


cd ../apps/crystal

cp sndtbl_cooja.c sndtbl.c



make clean
rm crystal.sky

method="gm"
timeout="3600000000" #24 hours but shrunk to 1 sec per measurement
for meth in $method
do
    cd ../../simulations
    new_sim="26_nodes_crystal_method_"$meth".csc"
    cp 26_nodes_crystal_generic.csc $new_sim
    sim_id="crystal_"$meth
    sed -i -e "s/EXECUTABLE/$sim_id/g" $new_sim
    sed -i -e "s/FILLME/$timeout/g" $new_sim
    sed -i -e "s/TOTAL_TIMEOUT/3601000000/g" $new_sim
    mv $new_sim ../../simulations/
done


cd ../apps/crystal

for meth in $method
do
     for node in `seq 1 26`
     do
         export CFLAGS="$CFLAGS -DSEPARATE_HEADERS=1"
         data_header="../../Arctium/input_data/all_the_data_intel_node_"$node".h"
         #echo $data_header
         export CFLAGS="$CFLAGS -DSPECIFIC_HEADER=$data_header"
         #CFLAGS+= -DSPECIFIC_HEADER=$(data_header)
         #echo $CFLAGS
         #exit 1
         make clean
         if [ $meth == "naive" ]
         then
            
            export CFLAGS="$CFLAGS -DNAIVE=1"
            make || exit 1
            
         else
            export CFLAGS="$CFLAGS -DNAIVE=0"
            make || exit 1
         fi
         cp crystal.sky "../../simulations/crystal_"$meth"_"$node".sky"
         rm crystal.sky
     done
done

