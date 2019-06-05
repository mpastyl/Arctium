#ifndef APP_H
#define APP_H
#if DEPCOMP18
#include "app-depcomp.h"
#elif MY_APP
//#include  "/media/sf_geometric_monitoring_shared/geometric-monitoring/contiki_project/gm_for_crystal.h"
#include  "../../Arctium/gm_for_crystal.h"
#else
#include "app-ipsn18.h"
#endif
#endif //APP_H
