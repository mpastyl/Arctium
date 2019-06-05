#define MAX_READINGS 120
#define T 2
#define REPEAT 0


#ifndef SEPARATE_HEADERS
  #define SEPARATE_HEADERS 0
#endif

#ifndef DUMMY_DATA
  #define DYMMY_DATA 0
#endif

#if DUMMY_DATA
  #include "schedule.h"
#endif

// to sting-ize pre-processor values
// in order to print them with #pragma message
// or pass to include, otherwise the "s get removed
#define XSTR(x) STR(x)
#define STR(x) #x

#if !DUMMY_DATA
  #if SEPARATE_HEADERS == 1
    #ifndef SPECIFIC_HEADER
      #error "WHY NOT DEFINED?"
    #else
      #include XSTR(SPECIFIC_HEADER)
    #endif
  #else
    #include "all_the_data_intel.h"
  #endif
#endif
float get_a_new_measurement(int new_id, unsigned long app_counter){

  float val;
  #if !DUMMY_DATA
    #if SEPARATE_HEADERS == 1
    val = Data[app_counter%2500];
    printf("Converted %d.%d from file %s Data[%d]\n",get_int(val),get_dec(val),XSTR(SPECIFIC_HEADER),app_counter%2500);
    #else
    val = Data[(new_id-1+ REPEAT)%NODES][app_counter%MAX_READINGS];
    printf("Converted %d.%d from Data[ %d ][ %lu ]\n",get_int(val), get_dec(val), (new_id-1+ REPEAT)%NODES, app_counter%MAX_READINGS);
    #endif
 #else
    val = 0;
    printf("Converted %d.%d from DUMMY DATA \n",get_int(val), get_dec(val));
 #endif

  return val;
    
}

/* Read the schedule statically.
 * 
 *  Decides whether to transmit or not based on the pre-defined static schedule.
 *  
 *  Reads how many transmitters we should have at this round, e.g. x, and
 *  decides to transmit with propability x/NODES.
 *
 *  returns: 1 if decision is to transmit, otherwise 0.
 */
#if DUMMY_DATA
int app_get_schedule(int new_id, unsigned long app_counter){

    int num_nodes = schedule[app_counter % schedule_size];
    //unsigned short r = ((unsigned short)rand()) % (NODES - 1);
    if ((new_id-1) <= num_nodes){
      return 1;
    }
    else{
      return 0;
    }
}
#endif

float my_fn(float x)
{
  float y = T+ x*x; //For variance monitoring
  return y;
}

int check_aprox_eq(float a, float b){
  float c = a-b;
  if (c==0) return 1;
  if ((c>0)&&(c<0.0001)) return 1;
  if ((c<0)&&(c>-0.0001)) return 1;
  return 0;
}

//Updated: no longer need to loop over all the nodes
void update_global_estimate(struct value_struct * global_estimate, struct value_struct * store_updates, struct value_struct * new_update, int src){

  /*
  int i;
  float x_avg = 0;
  float y_avg = 0;
  for (i=0;i<NODES;i++){
    if (i == (skip_node-1))
      continue;
    x_avg += store_updates[i].x_value;
    y_avg += store_updates[i].y_value;
  }
  x_avg = x_avg / (float) (NODES-1) ;
  y_avg = y_avg / (float) (NODES-1) ;
  //printf("Global estimate %d.%d %d.%d \n", get_int(x_avg),get_dec(x_avg),get_int(y_avg),get_dec(y_avg));
  global_estimate->x_value = x_avg;
  global_estimate->y_value = y_avg;
  */

  //global_estimate->x_value += (new_update->x_value - store_updates[src].x_value) / (float) (NODES -1); 
  //global_estimate->y_value += (new_update->y_value - store_updates[src].y_value) / (float) (NODES -1); 

  //store_updates[src].x_value = new_update->x_value;
  //store_updates[src].y_value = new_update->y_value;
  
  //Now with deltas 
  global_estimate->x_value += new_update->x_value / (float) (NODES-1);
  global_estimate->y_value += new_update->y_value / (float) (NODES-1);

}

int check_local_constrains(struct value_struct * global_estimate, struct value_struct * new_value, unsigned long app_counter, int new_id)
{
  /*
  #ifdef DOUBLE_CHECK
  leds_on(LEDS_RED);
  int ret = TEST_check_local_constrains(global_estimate, new_value);
  leds_off(LEDS_RED);
  #endif
  */

  #if DUMMY_DATA 
    return app_get_schedule(new_id, app_counter);
  #endif

  leds_on(LEDS_RED);


  float a_x = (float) (global_estimate->x_value);
  float a_y = (float) (global_estimate->y_value);


  float b_x = (float) (new_value->x_value);
  float b_y = (float) (new_value->y_value);



  float center_x = ( a_x + b_x)*0.5;
  float center_y = ( a_y + b_y)*0.5;
  float radius_square =  ( (a_x - b_x)*(a_x - b_x) +  (a_y - b_y)*(a_y - b_y) )*0.25;

  if (check_aprox_eq(a_x,b_x) && check_aprox_eq(a_y,b_y)) {
    leds_off(LEDS_RED);
    LOG("Radius is tiny\n");
    return 0;
  }

  //Check crossing the box instead of circle


  float radius = sqrt(radius_square);

  float left = center_x - radius;
  float right = center_x + radius;
  float up = center_y + radius;
  float down = center_y - radius;

  int flag=0;

#ifdef MEASURE_AVG
  if ((a_x>=T) && (b_x<=T)) {
    leds_off(LEDS_RED);
    return 1;
  }
  if ((a_x<=T) && (b_x>=T)){
    leds_off(LEDS_RED);
    return 1;
  }
  leds_off(LEDS_RED);
  return 0;

#endif


  if ((my_fn(left)<up)&&(my_fn(left)>down)) flag=1;
  if ((my_fn(right)<up)&&(my_fn(right)>down)) flag=1;
  if (down > T){
    float point1 = sqrt(down -T); //FIXME change if function changes
    if (( point1>left) && ( point1 < right)) flag=1;
    float point2 = - sqrt(down -T);
    if (( point2>left) && ( point2 < right)) flag=1;
  }
  if (up > T){
    float point1 = sqrt(up -T);
    if (( point1>left) && ( point1 < right)) flag=1;
    float point2 = -sqrt(up -T);
    if (( point2>left) && ( point2 < right)) flag=1;
  }
  if (flag) {
    leds_off(LEDS_RED);
    LOG(" Box was crossed \n");
    #ifdef DOUBLE_CHECK
    if (ret==0){
      LOG(" ERROR-> case 5: old way says no violation, new says yes\n");
      pfloat(" ERROR---> a_x",a_x);
      pfloat(" a_y",a_y);
      pfloat(" b_x",b_x);
      pfloat(" b_y",b_y);
      pfloat(" r_square",radius_square);
      LOG("\n");
    }
    #endif
 
    return 1;
  }
  else{
    leds_off(LEDS_RED);
    LOG(" Box was not crossed, so we should be OK\n");
    #ifdef DOUBLE_CHECK
    if (ret==1) {
      LOG(" ERROR-> case 4: old way says violation, new says no (Shouldn't really happen) \n");
      pfloat(" ERROR---> a_x",a_x);
      pfloat(" a_y",a_y);
      pfloat(" b_x",b_x);
      pfloat(" b_y",b_y);
      pfloat(" r_square",radius_square);
      LOG("\n");
    }
    #endif

    return 0;
  }

}
