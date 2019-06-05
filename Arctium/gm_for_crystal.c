#include "rand.h"
#include "sndtbl.c"
#include "utils.c"
#include "gm_core.c"
#include <math.h>

#define KEEP_TRACK_OF_CONTRIBUTIONS 1

#define EARLY_FINISH 1

#define EARLY_REMOVE 1

#define APP_DEBUG 0

#define USE_RECHECK_THRESHOLD 1

#define RECHECK_THRESHOLD (USE_RECHECK_THRESHOLD & (!DUMMY_DATA))

/* TODO: IS_SINK and possibly other 
 * functions will break if the new_id
 * for flocklab runs. Replace them with new_id or?
 *    Replaced them in crystal.c
 *
 * NO_NODE will also be problematic with new ids
 */

/* Now the sink sends the global estimate with 
 * every ack, instead of the update
 */

static uint16_t app_have_packet;
static uint16_t app_seqn;
static uint16_t app_log_seqn;
static uint16_t app_n_packets;

static int new_id=1;
static float val;
static unsigned long app_counter=0;
static struct value_struct new_value;
static struct value_struct raw_new_value;
static struct value_struct last_value;
static struct value_struct drift_vector;
static struct value_struct global_estimate;
static int having_trouble = 0;
static int consecutive_backoffs = 0;


//TODO: test- remove me
#if APP_DEBUG
static uint8_t test_local_ids_acked[NODES+1];
static struct value_struct test_local_values[NODES+1];
#endif

// Aggregation variables
static uint8_t local_id_buffer[ID_BUFFER_SIZE];
static struct value_struct local_value_buffer[ID_BUFFER_SIZE];
static struct value_struct aggregate_value;
static int can_aggregate = 0;

#ifndef NAIVE
  #define NAIVE 0
#endif

#ifndef BACKOFF
  #define BACKOFF 100
#endif

static struct value_struct store_updates[NODES];

#define PACKETS_PER_EPOCH 1

#if CRYSTAL_LOGGING
struct pkt_record {
  uint16_t seqn;
  uint16_t acked;
  
} app_packets[PACKETS_PER_EPOCH];
#endif //CRYSTAL_LOGGING

static void app_init() {

  new_id = find_id();
  srand(new_id);
  printf("Started ");
  #if NAIVE == 1
  printf(" NAIVE");
  #else
  printf("GM");
  #endif
  printf(" with %d nodes \n",NODES);
  new_value.x_value = 0;
  new_value.y_value = 0;
  raw_new_value.x_value = 0;
  raw_new_value.y_value = 0;
  global_estimate.x_value = 0;
  global_estimate.y_value = 0;
  last_value.x_value = 0;
  last_value.y_value = 0;
  int i =0;
  for(i=0; i<ID_BUFFER_SIZE; i++){
    local_id_buffer[i] = 0;
    local_value_buffer[i].x_value = 0;
    local_value_buffer[i].y_value = 0;
  }
  //TODO: test-remove me
#if APP_DEBUG
  for (i=0;i<(NODES+1);i++){
    test_local_ids_acked[i]=0;
  }
#endif
}

static inline void app_pre_S() {
  app_have_packet = 0;
  log_send_seqn = 0;
  app_n_packets = 0;
  //TODO: test-remove me
#if APP_DEBUG
  if(IS_SINK()){
    int i;
    for (i=0;i<(NODES+1);i++){
      test_local_ids_acked[i]=0;
      test_local_values[i].x_value = 0;
      test_local_values[i].y_value = 0;
    }
  }
#endif
}

static inline void app_new_packet() {
  app_seqn ++;
#if CRYSTAL_LOGGING
  app_packets[app_n_packets].seqn = app_seqn;
  app_packets[app_n_packets].acked = 0;
#endif //CRYSTAL_LOGGING
  app_n_packets ++;
  having_trouble = 0;
  consecutive_backoffs = 0;

  // Init aggregation variables
  aggregate_value.x_value = new_value.x_value;// This should already have a the new value right?
  aggregate_value.y_value = new_value.y_value;
  can_aggregate = 0;
  int i =0;
  for(i=0; i<ID_BUFFER_SIZE; i++){
    local_id_buffer[i] = 0;
    local_value_buffer[i].x_value = 0;
    local_value_buffer[i].y_value = 0;
  }

}

static inline void app_mark_acked() {
#if CRYSTAL_LOGGING
  app_packets[app_n_packets-1].acked = 1;
#endif //CRYSTAL_LOGGING
}

static inline void app_post_S(int received) {
  if (IS_SINK())
    return;
#if CONCURRENT_TXS > 0
  /*
  int i;
  int cur_idx;
  if (epoch >= START_EPOCH) {
    cur_idx = ((epoch - START_EPOCH) % NUM_ACTIVE_EPOCHS) * CONCURRENT_TXS;
    for (i=0; i<CONCURRENT_TXS; i++) {
      if (new_id == sndtbl[cur_idx + i]) {
        app_have_packet = 1;
        app_new_packet();
        break;
      }
    }
  }
  */
  #if NAIVE == 0
  if (check_local_constrains(&global_estimate, &drift_vector, app_counter, new_id)){
  #endif
    

    app_have_packet = 1;
    app_new_packet();
    last_value = raw_new_value;
  #if NAIVE == 0
  }
  #endif

#endif // CONCURRENT_TXS
}

static inline int app_pre_T() {
  if(IS_SINK()) return 0;
  log_send_acked = 0;
  unsigned short r;
  r = ( (unsigned short )rand() )% 100;

  // case where you sould back off 
  if (app_have_packet && having_trouble && (consecutive_backoffs <=1) && (r >= BACKOFF)){
      consecutive_backoffs++;
      return 0;
  }

  /*
  leds_off(LEDS_RED);
  leds_off(LEDS_GREEN);
  if (get_size(local_id_buffer,ID_BUFFER_SIZE)==1){
    leds_on(LEDS_RED);
  }
  else if (get_size(local_id_buffer,ID_BUFFER_SIZE)==2){
    leds_on(LEDS_GREEN);
  }
  */

  if (app_have_packet) {
    crystal_data->app.seqn = app_seqn;
    crystal_data->app.src = new_id;
    crystal_data->app.payload.x_value = new_value.x_value;
    crystal_data->app.payload.y_value = new_value.y_value;
    
    if (can_aggregate){ //send the aggregate value if possible
      crystal_data->app.payload.x_value = aggregate_value.x_value;
      crystal_data->app.payload.y_value = aggregate_value.y_value;
    }
    int i=0;
    for(i=0; i<ID_BUFFER_SIZE; i++){
      if (can_aggregate){
        crystal_data->app.id_buffer[i] = local_id_buffer[i];
      }
      else{
        crystal_data->app.id_buffer[i] = 0;
      }
    } 
    
    log_send_seqn = app_seqn;  // for logging
    consecutive_backoffs = 0;
    return 1;
  }
  return 0;
}

static inline void app_between_TA(int received) {
  // CS: here, if sink, store the update you just received
  if (received) {
    log_recv_src = crystal_data->app.src;
    log_recv_seqn = crystal_data->app.seqn;
    if (IS_SINK()) {
      
      //TODO: test- remove me
#if APP_DEBUG
      test_local_ids_acked[log_recv_src]++;
      test_local_values[log_recv_src].x_value += crystal_data->app.payload.x_value;
      test_local_values[log_recv_src].y_value += crystal_data->app.payload.y_value;
      int ii=0;
      for(ii=0; ii<ID_BUFFER_SIZE; ii++){
        if (crystal_data->app.id_buffer[ii]!=0){
            //test_local_ids_acked[crystal_data->app.id_buffer[ii]] = log_recv_src;
            test_local_ids_acked[crystal_data->app.id_buffer[ii]] ++;
        }
      }
#endif

      /*
      if (get_size(crystal_data->app.id_buffer, ID_BUFFER_SIZE) ==2){
        leds_toggle(LEDS_RED);
      }
      */

      int src = crystal_data->app.src - 1;
      struct value_struct new_update = crystal_data->app.payload;
      update_global_estimate(&global_estimate, store_updates, &new_update, src);
      uint8_t new_id_buffer[ID_BUFFER_SIZE]; //TODO:remove me, just leave it in id buffer, since crystal_data is in a union with crystal_ack?
      int i=0;
      for(i=0; i<ID_BUFFER_SIZE; i++){
        new_id_buffer[i] = (uint8_t) (crystal_data->app.id_buffer[i]);
      }
      
      // fill in the ack payload
      crystal_ack->app.seqn = log_recv_seqn;
      crystal_ack->app.src = log_recv_src;
      crystal_ack->app.update.x_value = global_estimate.x_value;
      crystal_ack->app.update.y_value = global_estimate.y_value; 
      for(i=0; i<ID_BUFFER_SIZE; i++){
        crystal_ack->app.id_buffer[i] = new_id_buffer[i];
      }
    }
    else{//If not sink, and during backoff, see if you can combine 
      //TODO: Here if i am not combining and see someone sending in my place, can I stop trying to send? 
      if ((log_recv_src != new_id) && having_trouble){
        if (!is_full(local_id_buffer, ID_BUFFER_SIZE)){
         if (is_empty(crystal_data->app.id_buffer, ID_BUFFER_SIZE)){
          if (!is_member(local_id_buffer, ID_BUFFER_SIZE, (uint8_t) log_recv_src)){
            can_aggregate = 1;
            //leds_toggle(LEDS_GREEN);
            //add_to_buffer(local_id_buffer, ID_BUFFER_SIZE, (uint8_t) log_recv_src);
            //add_to_value_buffer(local_value_buffer, local_id_buffer, ID_BUFFER_SIZE, crystal_data->app.payload);
            add_to_both_buffers(local_id_buffer, local_value_buffer, ID_BUFFER_SIZE, (uint8_t) log_recv_src, crystal_data->app.payload);
            aggregate_value.x_value += crystal_data->app.payload.x_value;
            aggregate_value.y_value += crystal_data->app.payload.y_value;
          }
         }
#if EARLY_FINISH
         else if ((!can_aggregate) && is_member(crystal_data->app.id_buffer,ID_BUFFER_SIZE, (uint8_t) new_id)){
          log_send_acked = 1;
          app_mark_acked();
          if (app_n_packets < PACKETS_PER_EPOCH) {
            app_new_packet();
          }
          else {
            app_have_packet = 0;
            having_trouble = 0;
            can_aggregate = 0;
            flush_buffer(local_id_buffer, ID_BUFFER_SIZE);
            flush_value_buffer(local_value_buffer, ID_BUFFER_SIZE);
          }
         }
#endif
#if EARLY_REMOVE
       else if ((can_aggregate) && (get_size(crystal_data->app.id_buffer, ID_BUFFER_SIZE) >= get_size(local_id_buffer, ID_BUFFER_SIZE))){
          //try to remove what is already beeing aggregated by a more succesfull aggregator
          if (is_member(local_id_buffer, ID_BUFFER_SIZE, (uint8_t) (crystal_data->app.src))){
            remove_from_buffers(local_id_buffer, local_value_buffer, ID_BUFFER_SIZE, (uint8_t) (crystal_data->app.src));
          }
          int i;
          for(i=0; i<ID_BUFFER_SIZE; i++){
            if ( (crystal_data->app.id_buffer[i]!=0) && is_member(local_id_buffer, ID_BUFFER_SIZE, crystal_data->app.id_buffer[i]) ){
              remove_from_buffers(local_id_buffer, local_value_buffer, ID_BUFFER_SIZE, crystal_data->app.id_buffer[i]);
            }
          }
          if (is_empty(local_id_buffer, ID_BUFFER_SIZE))
            can_aggregate = 0;
       }
#endif
        }
      }
    }
  }
  else {
    crystal_ack->app.seqn=NO_SEQN;
    crystal_ack->app.src=NO_NODE;
  }
}

static inline void app_post_A(int received) {
  // non-sink: if acked us, stop sending data
  log_send_acked = 0;

  
  // When called from sink, received is 0
  if (received) {
    if ((crystal_ack->app.src != NO_NODE) && ( crystal_ack->app.seqn != NO_SEQN )) {

      int src = crystal_ack->app.src - 1;
      global_estimate.x_value = crystal_ack->app.update.x_value;
      global_estimate.y_value = crystal_ack->app.update.y_value;
    
      //Update the drift vector again because it is affected by the global estimate
      drift_vector.x_value = global_estimate.x_value + raw_new_value.x_value - last_value.x_value;
      drift_vector.y_value = global_estimate.y_value + raw_new_value.y_value - last_value.y_value;

#if RECHECK_THRESHOLD & (!NAIVE)
     if ((!app_have_packet) && (check_local_constrains(&global_estimate, &drift_vector, app_counter, new_id))){
      app_have_packet = 1;
      app_new_packet();
      last_value = raw_new_value;
      return;
     }
#endif //RECHECK_THRESHOLD & (!NAIVE)
    }
  }
  

  if (app_have_packet && received) {
    

#if EARLY_FINISH
    if (((crystal_ack->app.src == new_id) && (crystal_ack->app.seqn == app_seqn)) || (is_member(crystal_ack->app.id_buffer,ID_BUFFER_SIZE, (uint8_t)new_id) && is_empty(local_id_buffer,ID_BUFFER_SIZE)) ) {
#else
    if (((crystal_ack->app.src == new_id) && (crystal_ack->app.seqn == app_seqn)) || is_member(crystal_ack->app.id_buffer,ID_BUFFER_SIZE, (uint8_t)new_id)) {
#endif
      //FIXME: make sure i don't have pending values to aggregate
      log_send_acked = 1;
      app_mark_acked();
      if (app_n_packets < PACKETS_PER_EPOCH) {
        app_new_packet();
      }
      else {
        app_have_packet = 0;
        having_trouble = 0;
        can_aggregate = 0;
        flush_buffer(local_id_buffer, ID_BUFFER_SIZE);
        flush_value_buffer(local_value_buffer, ID_BUFFER_SIZE);
      }
      //TODO: remove me
      //if (is_member(crystal_ack->app.id_buffer,ID_BUFFER_SIZE,(uint8_t) new_id) && !( crystal_ack->app.src == new_id ) ){
      //      leds_toggle(LEDS_BLUE);
      //}
      //if (is_full(crystal_ack->app.id_buffer, ID_BUFFER_SIZE)){
      //  leds_toggle(LEDS_RED);
      //}
    }
    else{// If we receive and it was not us, we go to backoff mode
      if (!IS_SINK()) {
        having_trouble = 1;
        // If the receiver or smthing in the buffer is one of the IDs I track, flush the buffer and stop aggregating for now.
        // TODO: instead of flushing, remove what was acked
#if KEEP_TRACK_OF_CONTRIBUTIONS
        if (is_member(local_id_buffer, ID_BUFFER_SIZE, (uint8_t) (crystal_ack->app.src))){
          remove_from_buffers(local_id_buffer, local_value_buffer, ID_BUFFER_SIZE, (uint8_t) (crystal_ack->app.src));
        }
        int i;
        for(i=0; i<ID_BUFFER_SIZE; i++){
          if ( (crystal_ack->app.id_buffer[i]!=0) && is_member(local_id_buffer, ID_BUFFER_SIZE, crystal_ack->app.id_buffer[i]) ){
            remove_from_buffers(local_id_buffer, local_value_buffer, ID_BUFFER_SIZE, crystal_ack->app.id_buffer[i]);
          }
        }

        //If I was acked but had aggregated values, remove my contribution
        if (is_member(crystal_ack->app.id_buffer,ID_BUFFER_SIZE, (uint8_t)new_id)){
          //If the buffer got empty, no need to keep sending
          if (is_empty(local_id_buffer, ID_BUFFER_SIZE)){ 
            log_send_acked = 1;
            app_mark_acked();
            if (app_n_packets < PACKETS_PER_EPOCH) {
              app_new_packet();
            }
            else {
              app_have_packet = 0;
              having_trouble = 0;
              can_aggregate = 0;
              flush_buffer(local_id_buffer, ID_BUFFER_SIZE);
              flush_value_buffer(local_value_buffer, ID_BUFFER_SIZE);
            }
          }
          else{
            //WARNING: I have to remove my contribution. My id will still reach the sink twice, but the second time my contribution will be 0
            new_value.x_value =0;
            new_value.y_value =0;
          }
        }

        if (is_empty(local_id_buffer, ID_BUFFER_SIZE))
          can_aggregate = 0;
        struct value_struct sum;
        find_aggregate(local_value_buffer, ID_BUFFER_SIZE, &sum);
        aggregate_value.x_value = new_value.x_value + sum.x_value;
        aggregate_value.y_value = new_value.y_value + sum.y_value;
        
#else
        if (is_member(local_id_buffer, ID_BUFFER_SIZE, (uint8_t) (crystal_ack->app.src))){
          flush_buffer(local_id_buffer, ID_BUFFER_SIZE);
          can_aggregate = 0;
          aggregate_value.x_value = new_value.x_value;
          aggregate_value.y_value = new_value.y_value;
        }
        int i;
        for(i=0; i<ID_BUFFER_SIZE; i++){
          if ( (crystal_ack->app.id_buffer[i]!=0) && is_member(local_id_buffer, ID_BUFFER_SIZE, crystal_ack->app.id_buffer[i]) ){
            flush_buffer(local_id_buffer, ID_BUFFER_SIZE);
            can_aggregate = 0;
            aggregate_value.x_value = new_value.x_value;
            aggregate_value.y_value = new_value.y_value;
          }
        }
#endif
      }
    }
  }
}


static inline void app_epoch_end() {
  // All nodes update their global estimate.
  printf("Global estimate %d.%d %d.%d \n", get_int(global_estimate.x_value),get_dec(global_estimate.x_value),get_int(global_estimate.y_value),get_dec(global_estimate.y_value));
  if (!IS_SINK()){ //--- non-sink gets a new measurement and does GM threshold checking
    printf("Hey lets get a new measurements size of payload %d \n", sizeof (struct value_struct));
    val = get_a_new_measurement(new_id, app_counter);
    raw_new_value.x_value = val;
    raw_new_value.y_value = val*val; //keeping the square of the value (for variance monitoring)
    app_counter++;

    drift_vector.x_value = global_estimate.x_value + raw_new_value.x_value - last_value.x_value; //TODO: should last value be the merged one?
    drift_vector.y_value = global_estimate.y_value + raw_new_value.y_value - last_value.y_value;
  
    store_updates[new_id -1].x_value = new_value.x_value;  
    store_updates[new_id -1].y_value = new_value.y_value;
    
    new_value.x_value = raw_new_value.x_value - last_value.x_value;  
    new_value.y_value = raw_new_value.y_value - last_value.y_value;  

  } 
  else { //--- sink does nothing yet...
  }
}
static inline void app_ping() {
}

#if CRYSTAL_LOGGING
void app_print_logs() {
  if (!IS_SINK()) {
    int i;
    for (i=0; i<app_n_packets; i++) {
      printf("A %u:%u %u %u\n", epoch, 
          app_packets[i].seqn, 
          app_packets[i].acked, 
          app_log_seqn);
      app_log_seqn ++;
    }
  }
  //TODO: test-remove me
#if APP_DEBUG
  else{
    printf("TEST: "); 
    int i;
    for (i=0;i<(NODES+1);i++){
      printf("%d ", test_local_ids_acked[i]);
    }
    printf("\n");
    printf("TEST: "); 
    for (i=0;i<(NODES+1);i++){
      printf("%d.%d ", get_int(test_local_values[i].x_value), get_dec( test_local_values[i].x_value));
    }
    printf("\n");
  }
#endif
}
#endif //CRYSTAL_LOGGING

