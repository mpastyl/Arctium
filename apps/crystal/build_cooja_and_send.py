from params import D_params
import sh
import sys
import os

#This horific piece of code gets the parameters form params.py, including cases where you want to test
#multiple values per parameter
def get_configs():
    base_set = {}
    multi_set =[]
    for param in D_params:
        if len(D_params[param]) == 1 :
            base_set[param] = D_params[param][0]
        else:
            for x in D_params[param]:
                multi_set.append((param,x))
    
    flat_multi_set = []
    for x,y in multi_set:
        for w,z in multi_set:
            if (x!=w):
                new_dir ={}
                new_dir[x]= y
                new_dir[w]= z
                if new_dir not in flat_multi_set:
                    flat_multi_set.append(new_dir)

    final_config_set = []
    for x in flat_multi_set:
        new_config = base_set.copy()
        for key in x:
            new_config[key] = x[key]
        final_config_set.append(new_config)

    return final_config_set

configs =  get_configs()


counter= 0
for c in configs:
    counter+= 1
    new_config = "test_config_"+str(counter)+".sh"
    sh.cp("make_all_crystal_26_nodes_send_to_xeon_dummy_data_template.sh", new_config)
    for param in c:
        value = c[param]
        sh.sed("-i", "-e", "s/"+param+"=X/"+param+"="+str(value)+"/g", new_config)
    xeon_dir = "\/home\/chasty\/cooja_experiments\/crystal_experiments\/multi_parameter\/"+str(counter)+"\/"
    xeon_log_path = "\\\\\/home\\\\\/chasty\\\\\/cooja_experiments\\\\\/crystal_experiments\\\\\/multi_parameter\\\\\/"+str(counter)+"\\\\\/"
    sh.sed("-i", "-e","s/XEON_DIR_REPLACEME/"+xeon_dir+"/g",new_config)
    sh.sed("-i", "-e","s/XEON_LOG_PATH_REPLACEME/"+xeon_log_path+"/g",new_config)

    #compile the actual config and send to xeon
    #print sh.sh(new_config, _out=sys.stdout)
    os.system("./"+new_config)
    sh.scp(new_config, "chasty@cse-31228.cse.chalmers.se:/home/chasty/cooja_experiments/crystal_experiments/multi_parameter/"+str(counter))

