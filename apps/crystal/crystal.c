/*
 * Copyright (c) 2018, University of Trento, Italy and 
 * Fondazione Bruno Kessler, Trento, Italy.
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of its 
 *    contributors may be used to endorse or promote products derived from this 
 *    software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Authors: Timofei Istomin <tim.ist@gmail.com>
 *          Matteo Trobinger <matteo.trobinger@unitn.it>
 *
 */



#include "crystal.h"
#include "cc2420.h"
#include "node-id.h"
#include "ds2411.h"

#include "packets.h"

union {
  crystal_sync_struct sync;
  crystal_data_struct data;
  crystal_ack_struct ack;
} crystal_data_buf;

#define CRYSTAL_BUF_LEN (sizeof(crystal_data_buf))
#define BZERO_BUF() bzero(&crystal_data_buf, CRYSTAL_BUF_LEN)

#define crystal_sync ((crystal_sync_struct*)&crystal_data_buf) 
#define crystal_data ((crystal_data_struct*)&crystal_data_buf) 
#define crystal_ack ((crystal_ack_struct*)&crystal_data_buf) 

static struct glossy glossy_S, glossy_T, glossy_A;  // Glossy structure to be used in different phases

static struct rtimer rt;              // Rtimer used to schedule Crystal.
static struct pt pt;                  // Protothread used to schedule Crystal.
static uint16_t skew_estimated;       // Not zero if the clock skew over a period of length CRYSTAL_PERIOD has already been estimated.
static uint16_t synced_with_ack;      // Synchronized with an acknowledgement (A phase)
static uint16_t ever_synced_with_s;   // Synchronized with an S at least once
static uint16_t n_noack_epochs;       // Number of consecutive epochs the node did not synchronize with any acknowledgement
static int sync_missed = 0;           // Current number of consecutive S phases without synchronization (reference time not computed)
                                      // WARNING: skew division breaks if sync_missed is unsigned!

static rtimer_clock_t t_phase_start;  // Starting time (low-frequency clock) of a Glossy phase
static rtimer_clock_t t_phase_stop;   // Stopping time of a Glossy phase
static rtimer_clock_t t_wakeup;       // Time to wake up to prepare for the next epoch

static int period_skew;               // Current estimation of clock skew over a period of length CRYSTAL_PERIOD

uint8_t channel;  // current channel

rtimer_clock_t estimated_ref_time;   // estimated reference time for the current epoch
rtimer_clock_t corrected_ref_time_s; // reference time acquired during the S slot of the current epoch
rtimer_clock_t corrected_ref_time;   // reference time acquired during the S or an A slot of the current epoch
rtimer_clock_t skewed_ref_time;      // reference time in the local time frame

crystal_epoch_t epoch;        // epoch seqn received from the sink (or extrapolated)

uint16_t correct_packet; // whether the received packet is correct

uint16_t skip_S;
uint16_t n_ta_tx;      // how many times node tried to send data in the epoch
uint16_t n_ta;         // how many "ta" sequences there were in the epoch
uint16_t n_empty_ts;   // number of consecutive "T" phases without data
uint16_t n_high_noise; // number of consecutive "T" phases with high noise
uint16_t n_noacks;     // num. of consecutive "A" phases without any acks
uint16_t n_bad_acks;   // num. of consecutive "A" phases with bad acks
uint16_t n_all_acks;   // num. of negative and positive acks
uint16_t sleep_order;  // sink sent the sleep command
uint16_t n_badtype_a;  // num. of packets of wrong type received in A phase
uint16_t n_badlen_a;   // num. of packets of wrong length received in A phase
uint16_t n_badcrc_a;   // num. of packets with wrong CRC received in A phase
uint16_t recvtype_s;   // type of a packet received in S phase
uint16_t recvlen_s;    // length of a packet received in S phase
uint16_t recvsrc_s;    // source address of a packet received in S phase

uint32_t end_of_s_time; // timestamp of the end of the S phase, relative to the ref_time
uint16_t ack_skew_err;  // "wrong" ACK skew
uint16_t hopcount;
uint16_t rx_count_s, tx_count_s;  // tx and rx counters for S phase as reported by Glossy
uint16_t ton_s, ton_t, ton_a; // actual duration of the phases
uint16_t tf_s, tf_t, tf_a; // actual duration of the phases when all N packets are received
uint16_t n_short_s, n_short_t, n_short_a; // number of "complete" S, T and A phases (those counted in tf_s, tf_t and tf_a)
uint16_t cca_busy_cnt;

// info about current TA
uint16_t log_recv_seqn;
uint16_t log_recv_src;
uint16_t log_recv_type;
uint16_t log_recv_length;
uint16_t log_recv_err;

uint16_t log_send_seqn;
uint16_t log_send_acked;

#define TA_DURATION (DUR_T+DUR_A+2*CRYSTAL_INTER_PHASE_GAP)

// it's important to wait the maximum possible S phase duration before starting the TAs!
#define TAS_START_OFFS (CRYSTAL_INIT_GUARD*2 + DUR_S + CRYSTAL_INTER_PHASE_GAP)
#define PHASE_T_OFFS(n) (TAS_START_OFFS + (n)*TA_DURATION)
#define PHASE_A_OFFS(n) (PHASE_T_OFFS(n) + (DUR_T + CRYSTAL_INTER_PHASE_GAP))


// Time for the radio crystal oscillator to stabilize
#define OSC_STAB_TIME (RTIMER_SECOND/500) // 2 ms

#if CRYSTAL_LOGGING
#define LOGGING_GAP 100 // give 3 ms gap after the last TA before printing 
// this should be set in accordance with the slot durations so that
// the max number of TAs fits into the period minus time needed for
// printing the logs
//#if CRYSTAL_PERIOD < RTIMER_SECOND/3 + LOGGING_GAP + CRYSTAL_INIT_GUARD + DUR_S + CRYSTAL_INTER_PHASE_GAP + 100
//#error Period is too short for printing
//#endif
#define CRYSTAL_MAX_ACTIVE_TIME (CRYSTAL_PERIOD - RTIMER_SECOND/3 - LOGGING_GAP)
#else
#define CRYSTAL_MAX_ACTIVE_TIME (CRYSTAL_PERIOD - CRYSTAL_INIT_GUARD - CRYSTAL_INTER_PHASE_GAP - 100)
#endif

#define CRYSTAL_MAX_TAS (((unsigned int)(CRYSTAL_MAX_ACTIVE_TIME - TAS_START_OFFS))/(TA_DURATION))



// True if the current time offset is before the first TA and there is time to schedule TA 0
#define IS_WELL_BEFORE_TAS(offs) ((offs) + CRYSTAL_INTER_PHASE_GAP < PHASE_T_OFFS(0))
// True if the current time offset is before the first TA (number 0)
#define IS_BEFORE_TAS(offs) ((offs) < TAS_START_OFFS)
// gives the current TA number from a time offset from the epoch reference time
// valid only when IS_BEFORE_TAS() holds
#define N_TA_FROM_OFFS(offs) ((offs - TAS_START_OFFS)/TA_DURATION)

#define N_TA_TO_REF(tref, n) (tref-PHASE_A_OFFS(n))

#define N_MISSED_FOR_INIT_GUARD 3

#define N_SILENT_EPOCHS_TO_RESET 100
#define N_SILENT_EPOCHS_TO_STOP_SENDING 3

#define SYSTEM_RESET() do {WDTCTL = 0;} while(0)

#define APP_PING_INTERVAL (RTIMER_SECOND / 31) // 32 ms

// info about a data packet received during T phase
struct recv_info {
  uint8_t n_ta;
  uint8_t src;
  uint16_t seqn;
  uint8_t type;
  uint8_t rx_count;
  uint8_t length;
  uint8_t err_code;
};

// info about a data packet sent during T phase
struct send_info {
  uint8_t n_ta;
  uint16_t seqn;
  uint8_t rx_count;
  uint8_t acked;
};



#if CRYSTAL_LOGGING
#define MAX_LOG_TAS 100
struct recv_info recv_t[MAX_LOG_TAS];
int n_rec_rx; // number of receive records in the array

struct send_info send_t[MAX_LOG_TAS];
int n_rec_tx; // number of send records in the array
#endif //CRYSTAL_LOGGING

void inline log_ta_rx() {
#if CRYSTAL_LOGGING
  if (n_rec_rx < MAX_LOG_TAS) {
    recv_t[n_rec_rx].n_ta = n_ta;
    recv_t[n_rec_rx].src = log_recv_src;
    recv_t[n_rec_rx].seqn = log_recv_seqn;
    recv_t[n_rec_rx].type = log_recv_type;
    recv_t[n_rec_rx].rx_count = get_rx_cnt();
    recv_t[n_rec_rx].length = log_recv_length;
    recv_t[n_rec_rx].err_code = log_recv_err;
    n_rec_rx ++;
  }
#endif //CRYSTAL_LOGGING
}

void inline log_ta_tx() {
#if CRYSTAL_LOGGING
  if (n_rec_tx < MAX_LOG_TAS) {
    send_t[n_rec_tx].n_ta = n_ta;
    send_t[n_rec_tx].seqn = log_send_seqn;
    send_t[n_rec_tx].rx_count = get_rx_cnt();
    send_t[n_rec_tx].acked = log_send_acked;
    n_rec_tx ++;
  }
#endif //CRYSTAL_LOGGING
}

#define IS_SINK() (node_id == CRYSTAL_SINK_ID)

#define IS_SYNCED()          (is_t_ref_l_updated())

#if CRYSTAL_USE_DYNAMIC_NEMPTY
#define CRYSTAL_SINK_MAX_EMPTY_TS_DYNAMIC(n_ta_) (((n_ta_)>1)?(CRYSTAL_SINK_MAX_EMPTY_TS):1)
#warning ------------- !!! USING DYNAMIC N_EMPTY !!! -------------
#else
#define CRYSTAL_SINK_MAX_EMPTY_TS_DYNAMIC(n_ta_) CRYSTAL_SINK_MAX_EMPTY_TS
#endif

#define UPDATE_TF(tf, n_short, transmitting, N) if (get_rx_cnt() >= ((transmitting)?((N)-1):(N))) { \
  tf += get_rtx_on(); \
  n_short ++; \
}

#define CRYSTAL_BAD_DATA   1
#define CRYSTAL_BAD_CRC    2
#define CRYSTAL_HIGH_NOISE 3
#define CRYSTAL_SILENCE    4

//#if PRINT_GRAZ && CRYSTAL_LOGGING
#if 0
#define PRINT_BUF_SIZE 50
static char print_buf[PRINT_BUF_SIZE];
#define PRINTF(format, ...) do {\
  snprintf(print_buf, PRINT_BUF_SIZE, format "\n\n\n\n", __VA_ARGS__);\
  printf(print_buf);\
  clock_delay(300);\
  printf(print_buf);\
} while(0)
#else
#define PRINTF(format, ...) printf(format, __VA_ARGS__)
#endif


#include "chseq.c"
#include "app.c"

#if CRYSTAL_LOGGING
PROCESS(crystal_print_stats_process, "Crystal print stats");
PROCESS(alive_print_process, "");
#endif //CRYSTAL_LOGGING


// The "Long timer" implementation
static int lt_num_rounds;
static rtimer_clock_t lt_set_time;

#define longtimer_set(time, rounds) do {\
   lt_num_rounds = (rounds);\
   lt_set_time = (time);\
   while (lt_num_rounds > 0) { \
       lt_num_rounds --;\
       rtimer_set(t, lt_set_time, timer_handler, ptr); \
       PT_YIELD(&pt);\
   }\
   rtimer_set(t, lt_set_time, timer_handler, ptr); \
} while (0)

// workarounds for wrong ref time reported by glossy (which happens VERY rarely)
// sometimes it happens due to a wrong hopcount

#define MAX_CORRECT_HOPS 30

inline int correct_hops() {
#if (MAX_CORRECT_HOPS>0)
  return (get_relay_cnt()<=MAX_CORRECT_HOPS);
#else
  return 1;
#endif
}

#define CRYSTAL_ACK_SKEW_ERROR_DETECTION 1 
inline int correct_ack_skew(rtimer_clock_t new_ref) {
#if (CRYSTAL_ACK_SKEW_ERROR_DETECTION)
  static int new_skew;
#if (MAX_CORRECT_HOPS>0)
  if (get_relay_cnt()>MAX_CORRECT_HOPS)
    return 0;
#endif
  new_skew = new_ref - corrected_ref_time;
  //if (new_skew < 20 && new_skew > -20)  // IPSN'18
  if (new_skew < 60 && new_skew > -60)
    return 1;  // the skew looks correct
  else if (sync_missed && !synced_with_ack) {
    return 1;  // the skew is big but we did not synchronise during the current epoch, so probably it is fine
  }
  else {
    // signal error (0) only if not synchronised with S or another A in the current epoch.
    ack_skew_err = new_skew;
    return 0;
  }
#else
  return 1;
#endif
}


rtimer_callback_t timer_handler;

char sink_timer_handler(struct rtimer *t, void *ptr) {
  static rtimer_clock_t ref_time;
  PT_BEGIN(&pt);

#if CRYSTAL_START_DELAY_SINK > 0
  // just to delay a bit (for testbeds)
  longtimer_set(RTIMER_NOW()-1, CRYSTAL_START_DELAY_SINK);
  // will just print the alive message
#if CRYSTAL_LOGGING
  process_poll(&alive_print_process);
#endif
  PT_YIELD(&pt);
#endif

  leds_off(LEDS_RED);
  ref_time = RTIMER_NOW() + OSC_STAB_TIME + GLOSSY_PRE_TIME + 16; // + 16 just to be sure
  t_phase_start = ref_time;
  while (1) {
    
// -- Phase S (root) ----------------------------------------------------------------- S (root) ---

    cc2420_oscon();

    app_pre_S();

    // wait for the oscillator to stabilize
    rtimer_set(t, t_phase_start - (GLOSSY_PRE_TIME + 16), timer_handler, ptr);
    PT_YIELD(&pt);

    epoch ++;
    crystal_sync->epoch = epoch;
    crystal_sync->src = node_id;
    t_phase_stop = t_phase_start + DUR_S;

    tf_s = 0; tf_t = 0; tf_a = 0;
    n_short_s = 0; n_short_t = 0; n_short_a = 0;
    ton_s = 0; ton_t = 0; ton_a = 0;
    cca_busy_cnt = 0;

    channel = get_channel_epoch(epoch);

    glossy_start(&glossy_S, (uint8_t *)crystal_sync, CRYSTAL_SYNC_LEN,
        GLOSSY_INITIATOR, channel, GLOSSY_SYNC, N_TX_S,
        0, // don't stop on sync
        CRYSTAL_TYPE_SYNC, 
        0, // don't ignore type
        t_phase_start, t_phase_stop, timer_handler, t, ptr);
    // Yield the protothread. It will be resumed when Glossy terminates.
    PT_YIELD(&pt);

    // Stop Glossy.
    glossy_stop();
    //leds_off(LEDS_BLUE);
    ton_s = get_rtx_on();
    UPDATE_TF(tf_s, n_short_s, 1, N_TX_S);
    tx_count_s = get_tx_cnt();
    rx_count_s = get_rx_cnt();

    app_post_S(0);
    BZERO_BUF();
// -- Phase S end (root) --------------------------------------------------------- S end (root) ---
    
    n_empty_ts = 0;
    n_high_noise = 0;
    n_ta = 0;
    sleep_order = 0;
    log_recv_err = 0;
    while (!sleep_order && n_ta < CRYSTAL_MAX_TAS) {

// -- Phase T (root) ----------------------------------------------------------------- T (root) ---
      t_phase_start = ref_time - CRYSTAL_SHORT_GUARD + PHASE_T_OFFS(n_ta);
      t_phase_stop = t_phase_start + DUR_T + CRYSTAL_SHORT_GUARD + CRYSTAL_SINK_END_GUARD;

      channel = get_channel_epoch_ta(epoch, n_ta);

      app_pre_T();

      glossy_start(&glossy_T, (uint8_t *)crystal_data, CRYSTAL_DATA_LEN,
          GLOSSY_RECEIVER, channel, GLOSSY_NO_SYNC, N_TX_T,
          0, // don't stop on sync
          CRYSTAL_TYPE_DATA, 
          0, // don't ignore type
          t_phase_start, t_phase_stop, timer_handler, t, ptr);

      PT_YIELD(&pt);
      glossy_stop();
      ton_t += get_rtx_on();
      UPDATE_TF(tf_t, n_short_t, 0, N_TX_T);
      
      correct_packet = 0;
      cca_busy_cnt = get_cca_busy_cnt();
      log_recv_src = 0;
      if (get_rx_cnt()) { // received data
        n_empty_ts = 0;
        n_high_noise = 0;
        log_recv_type = get_app_header();
        log_recv_length = get_data_len();
        correct_packet = (log_recv_length == CRYSTAL_DATA_LEN && log_recv_type == CRYSTAL_TYPE_DATA);
        log_recv_err = correct_packet?0:CRYSTAL_BAD_DATA;
      }
      else if (is_corrupted()) {
        n_empty_ts = 0;
        n_high_noise = 0;
        log_recv_type = 0;
        log_recv_seqn = 0;
        log_recv_length = 0;
        log_recv_err = CRYSTAL_BAD_CRC;
      }
#if (CRYSTAL_SINK_MAX_NOISY_TS > 0)
      else if (cca_busy_cnt > CCA_COUNTER_THRESHOLD) {
        //n_empty_ts = 0; // should we reset it, it's a good question
        n_high_noise ++;
        log_recv_type = 0;
        log_recv_seqn = 0;
        log_recv_length = cca_busy_cnt;
        log_recv_err = CRYSTAL_HIGH_NOISE;
      }
#endif
      else {
        // just silence
        n_high_noise = 0;
        n_empty_ts ++;
        // logging for debugging
        log_recv_type = 0;
        log_recv_seqn = 0;
        log_recv_length = cca_busy_cnt;
        log_recv_err = CRYSTAL_SILENCE;
      }

      app_between_TA(correct_packet);
      log_ta_rx();
      //BZERO_BUF(); // cannot zero out as it has data for A
// -- Phase T end (root) --------------------------------------------------------- T end (root) ---
      sleep_order = 
        epoch >= N_FULL_EPOCHS && (
        (n_ta         >= CRYSTAL_MAX_TAS-1) || 
        (n_empty_ts   >= CRYSTAL_SINK_MAX_EMPTY_TS_DYNAMIC(n_ta)) || 
        (CRYSTAL_SINK_MAX_NOISY_TS && n_high_noise >= CRYSTAL_SINK_MAX_NOISY_TS)// && (n_high_noise >= CRYSTAL_SINK_MAX_EMPTY_TS_DYNAMIC(n_ta))
      );
// -- Phase A (root) ----------------------------------------------------------------- A (root) ---

      if (sleep_order)
        CRYSTAL_SET_ACK_SLEEP(crystal_ack);
      else
        CRYSTAL_SET_ACK_AWAKE(crystal_ack);
      
      crystal_ack->n_ta = n_ta;
      crystal_ack->epoch = epoch;
      t_phase_start = ref_time + PHASE_A_OFFS(n_ta);
      t_phase_stop = t_phase_start + DUR_A;

      glossy_start(&glossy_A, (uint8_t *)crystal_ack, CRYSTAL_ACK_LEN,
          GLOSSY_INITIATOR, channel, CRYSTAL_SYNC_ACKS, N_TX_A,
          0, // don't stop on sync
          CRYSTAL_TYPE_ACK, 
          0, // don't ignore type
          t_phase_start, t_phase_stop, timer_handler, t, ptr);

      PT_YIELD(&pt);
      glossy_stop();
      ton_a += get_rtx_on();
      UPDATE_TF(tf_a, n_short_a, 1, N_TX_A);

      app_post_A(0);
      BZERO_BUF();
// -- Phase A end (root) --------------------------------------------------------- A end (root) ---

      n_ta ++;
    }
    app_epoch_end();

    cc2420_oscoff(); // put radio to deep sleep

    
#if CRYSTAL_LOGGING
    // Now we have a long pause, good time to print
    rtimer_set(t, ref_time + PHASE_T_OFFS(CRYSTAL_MAX_TAS) + LOGGING_GAP, timer_handler, ptr);
    PT_YIELD(&pt);
    process_poll(&crystal_print_stats_process);
#endif //CRYSTAL_LOGGING

    ref_time += CRYSTAL_PERIOD;
    t_phase_start = ref_time;

    // time to wake up to prepare for the next epoch
    t_wakeup = t_phase_start - (OSC_STAB_TIME + GLOSSY_PRE_TIME + CRYSTAL_INTER_PHASE_GAP);

    while ((int16_t)(t_wakeup - (RTIMER_NOW() + APP_PING_INTERVAL)) > 16) {
      rtimer_set(t, RTIMER_NOW() + APP_PING_INTERVAL, timer_handler, ptr);
      app_ping();
      PT_YIELD(&pt);
    }

    rtimer_set(t, t_wakeup, timer_handler, ptr);
    PT_YIELD(&pt);
  }
  PT_END(&pt);
}

char nonsink_timer_handler(struct rtimer *t, void *ptr) {
  static rtimer_clock_t now;
  static rtimer_clock_t offs;
  static rtimer_clock_t s_guard;
  PT_BEGIN(&pt);
#if CRYSTAL_START_DELAY_SINK > 0
  // just to delay a bit (for testbeds)
  longtimer_set(RTIMER_NOW()-1, CRYSTAL_START_DELAY_NONSINK);
  // will just print the alive message
#if CRYSTAL_LOGGING
  process_poll(&alive_print_process);
#endif
  PT_YIELD(&pt);
#endif
  channel = get_channel_node_bootstrap(SCAN_RX_NOTHING);

  // Scanning loop
  while (1) {
    bzero(&glossy_S, sizeof(glossy_S)); // reset the Glossy timing info
    t_phase_start = RTIMER_NOW() + (GLOSSY_PRE_TIME + 6); // + 6 is just to be sure
    t_phase_stop = t_phase_start + CRYSTAL_SCAN_SLOT_DURATION;

    glossy_start(&glossy_S, (uint8_t *)crystal_sync, 0 /* size is not specified */,
        GLOSSY_RECEIVER, channel, GLOSSY_SYNC, 5 /* N */,
        //1, // stop immediately on sync
        0, // don't stop on sync
        0, // don't specify packet type
        1, // ignore type (receive anything)
        t_phase_start, t_phase_stop, timer_handler, t, ptr);
    PT_YIELD(&pt);
    glossy_stop();

    if (get_rx_cnt() > 0) {
      recvtype_s = get_app_header();
      recvlen_s = get_data_len();

      // Sync packet received
      if (recvtype_s == CRYSTAL_TYPE_SYNC && 
          recvlen_s  == CRYSTAL_SYNC_LEN  &&
          crystal_sync->src  == CRYSTAL_SINK_ID) {
        epoch = crystal_sync->epoch;
        n_ta = 0;
        if (IS_SYNCED()) {
          corrected_ref_time = get_t_ref_l();
          break; // exit the scanning
        }
        channel = get_channel_node_bootstrap(SCAN_RX_S);
        continue;
      }
      // Ack packet received
      else if (recvtype_s == CRYSTAL_TYPE_ACK && 
               recvlen_s  == CRYSTAL_ACK_LEN) {
        epoch = crystal_ack->epoch;
        n_ta = crystal_ack->n_ta;
        if (IS_SYNCED()) {
          corrected_ref_time = get_t_ref_l() - PHASE_A_OFFS(n_ta);
          break; // exit the scanning
        }
        channel = get_channel_node_bootstrap(SCAN_RX_A);
        continue;
      }
      // Data packet received
      else if (recvtype_s == CRYSTAL_TYPE_DATA
               /* && recvlen_s  == CRYSTAL_DATA_LEN*/
          // not checking the length because Glossy currently does not
          // copy the packet to the application buffer in this situation
          ) {
        continue; // scan again on the same channel waiting for an ACK
        // it is safe because Glossy exits immediately if it receives a non-syncronizing packet
      }
    }
    channel = get_channel_node_bootstrap(SCAN_RX_NOTHING);
  }

  if (recvtype_s != CRYSTAL_TYPE_SYNC)
    bzero(&glossy_S, sizeof(glossy_S)); // reset the timing info if a non-S packet was received

  BZERO_BUF();
  leds_off(LEDS_RED);

  // useful for debugging in Cooja
  //rtimer_set(t, RTIMER_NOW() + 15670, timer_handler, ptr);
  //PT_YIELD(&pt);

  now = RTIMER_NOW();
  offs = now - (corrected_ref_time - CRYSTAL_REF_SHIFT) + 20; // 20 just to be sure

  if (offs + CRYSTAL_INIT_GUARD + OSC_STAB_TIME + GLOSSY_PRE_TIME > CRYSTAL_PERIOD) {
    // We are that late so the next epoch started
    // (for sure this will not work with period of 2s)
    epoch ++;
    corrected_ref_time += CRYSTAL_PERIOD;
    if (offs > CRYSTAL_PERIOD) // safe to subtract 
      offs -= CRYSTAL_PERIOD;
    else // avoid wrapping around 0
      offs = 0;
  }
  
  // here we are either well before the next epoch's S
  // or right after (or inside) the current epoch's S

  if (IS_BEFORE_TAS(offs)) { // before TA chain but after S
    skip_S = 1;
    if (IS_WELL_BEFORE_TAS(offs)) {
      n_ta = 0;
    }
    else {
      n_ta = 1;
    }
  }
  else { // within or after TA chain
    n_ta = N_TA_FROM_OFFS(offs + CRYSTAL_INTER_PHASE_GAP) + 1;
    if (n_ta < CRYSTAL_MAX_TAS) { // within TA chain
      skip_S = 1;
    }
    else { // outside of the TA chain, capture the next S
      n_ta = 0;
    }
  }

  // here we have the ref time pointing at the previous epoch
  corrected_ref_time_s = corrected_ref_time;

  /* For S if we are not skipping it */
  estimated_ref_time = corrected_ref_time + CRYSTAL_PERIOD;
  skewed_ref_time = estimated_ref_time;
  t_phase_start = estimated_ref_time - CRYSTAL_REF_SHIFT - CRYSTAL_INIT_GUARD;
  t_phase_stop = t_phase_start + DUR_S + 2*CRYSTAL_INIT_GUARD; 
    
  while (1) {
    if (!skip_S) {
      cc2420_oscon();

      app_pre_S();

      tf_s = 0; tf_t = 0; tf_a = 0;
      n_short_s = 0; n_short_t = 0; n_short_a = 0;
      ton_s = 0; ton_t = 0; ton_a = 0;
      n_badlen_a = 0; n_badtype_a = 0; n_badcrc_a = 0;
      ack_skew_err = 0;
      cca_busy_cnt = 0;

      // wait for the oscillator to stabilize
      rtimer_set(t, t_phase_start - (GLOSSY_PRE_TIME + 16), timer_handler, ptr);
      PT_YIELD(&pt);

      // -- Phase S (non-root) --------------------------------------------------------- S (non-root) ---
      epoch ++;

      channel = get_channel_epoch(epoch);

      glossy_start(&glossy_S, (uint8_t *)crystal_sync, CRYSTAL_SYNC_LEN,
          GLOSSY_RECEIVER, channel, GLOSSY_SYNC, N_TX_S,
          0, // don't stop on sync
          CRYSTAL_TYPE_SYNC, 
          0, // don't ignore type
          t_phase_start, t_phase_stop, timer_handler, t, ptr);

      PT_YIELD(&pt);
      glossy_stop();

      ton_s = get_rtx_on();
      UPDATE_TF(tf_s, n_short_s, 0, N_TX_S);

      recvlen_s = get_data_len();
      recvtype_s = get_app_header();
      recvsrc_s = crystal_sync->src;
      rx_count_s = get_rx_cnt();
      tx_count_s = get_tx_cnt();

      correct_packet = (recvtype_s == CRYSTAL_TYPE_SYNC 
          && recvsrc_s  == CRYSTAL_SINK_ID
          && recvlen_s  == CRYSTAL_SYNC_LEN);

      if (rx_count_s > 0 && correct_packet) {
        epoch = crystal_sync->epoch;
        hopcount = get_relay_cnt();
      }
      if (IS_SYNCED() && rx_count_s > 0
          && correct_packet
          && correct_hops()) {
        corrected_ref_time_s = get_t_ref_l();
        corrected_ref_time = corrected_ref_time_s; // use this corrected ref time in the current epoch

        if (ever_synced_with_s) {
          // can estimate skew
          period_skew = (int16_t)(corrected_ref_time_s - (skewed_ref_time + CRYSTAL_PERIOD)) / (sync_missed + 1);
          skew_estimated = 1;
        }
    
        skewed_ref_time = corrected_ref_time_s;
        ever_synced_with_s = 1;
        sync_missed = 0;
      }
      else {
        sync_missed++;
        skewed_ref_time += CRYSTAL_PERIOD;
        corrected_ref_time = estimated_ref_time; // use the estimate if didn't update
        corrected_ref_time_s = estimated_ref_time;
      }
      //end_of_s_time = RTIMER_NOW()-ref_time; // just for debugging

      app_post_S(correct_packet);
      BZERO_BUF();

      n_ta = 0;
    }
    skip_S = 0;

// -- Phase S end (non-root) ------------------------------------------------- S end (non-root) ---

    n_empty_ts = 0;
    n_noacks = 0;
    n_high_noise = 0;
    n_bad_acks = 0;
    n_ta_tx = 0;
    n_all_acks = 0;
    sleep_order = 0;
    synced_with_ack = 0;

    while (1) { /* TA loop */
// -- Phase T (non-root) --------------------------------------------------------- T (non-root) ---
      static int guard;
      static uint16_t have_packet;
      static int i_tx;
      log_recv_err = 0;
      log_recv_src = 0;
      correct_packet = 0;
      have_packet = app_pre_T();
      i_tx = (have_packet && 
          (sync_missed < N_SILENT_EPOCHS_TO_STOP_SENDING || n_noack_epochs < N_SILENT_EPOCHS_TO_STOP_SENDING));
      // TODO: instead of just suppressing tx when out of sync it's better to scan for ACKs or Sync beacons...

      if (i_tx) {
        n_ta_tx ++;
        // no guards if I transmit
        guard = 0;
      }
      else {
        // guards for receiving
        guard = (sync_missed && !synced_with_ack)?CRYSTAL_SHORT_GUARD_NOSYNC:CRYSTAL_SHORT_GUARD;
      }
      t_phase_start = corrected_ref_time + PHASE_T_OFFS(n_ta) - CRYSTAL_REF_SHIFT - guard;
      t_phase_stop = t_phase_start + DUR_T + guard;

      //choice of the channel for each T-A slot
      channel = get_channel_epoch_ta(epoch, n_ta);
      
      glossy_start(&glossy_T, (uint8_t *)crystal_data, CRYSTAL_DATA_LEN, 
          i_tx, channel, GLOSSY_NO_SYNC, N_TX_T,
          0, // don't stop on sync
          CRYSTAL_TYPE_DATA, 
          0, // don't ignore type
          t_phase_start, t_phase_stop, timer_handler, t, ptr);

      PT_YIELD(&pt);
      glossy_stop();
      ton_t += get_rtx_on();
      UPDATE_TF(tf_t, n_short_t, i_tx, N_TX_T);
      
      if (!i_tx) { 
        if (get_rx_cnt()) { // received data
          log_recv_type = get_app_header();
          log_recv_length = get_data_len();
          correct_packet = (log_recv_length == CRYSTAL_DATA_LEN && log_recv_type == CRYSTAL_TYPE_DATA);
          log_recv_err = correct_packet?0:CRYSTAL_BAD_DATA;
          n_empty_ts = 0;
        }
        else if (is_corrupted()) {
          log_recv_type = 0;
          log_recv_seqn = 0;
          log_recv_length = 0;
          log_recv_err = CRYSTAL_BAD_CRC;
          //n_empty_ts = 0; // keep it as it is to give another chance but not too many chances
        } 
        else { // TODO: should we check for the high noise also here?
          log_recv_type = 0;
          log_recv_seqn = 0;
          log_recv_length = 0;
          log_recv_err = CRYSTAL_SILENCE;
          n_empty_ts ++;
        }
        cca_busy_cnt = get_cca_busy_cnt();
      }

      app_between_TA(correct_packet);

      BZERO_BUF();
      
// -- Phase T end (non-root) ------------------------------------------------- T end (non-root) ---

// -- Phase A (non-root) --------------------------------------------------------- A (non-root) ---

      correct_packet = 0;
      guard = (sync_missed && !synced_with_ack)?CRYSTAL_SHORT_GUARD_NOSYNC:CRYSTAL_SHORT_GUARD;
      t_phase_start = corrected_ref_time - guard + PHASE_A_OFFS(n_ta) - CRYSTAL_REF_SHIFT;
      t_phase_stop = t_phase_start + DUR_A + guard;

      glossy_start(&glossy_A, (uint8_t *)crystal_ack, CRYSTAL_ACK_LEN, 
          GLOSSY_RECEIVER, channel, CRYSTAL_SYNC_ACKS, N_TX_A,
          0, // don't stop on sync
          CRYSTAL_TYPE_ACK, 
          0, // don't ignore type
          t_phase_start, t_phase_stop, timer_handler, t, ptr);

      PT_YIELD(&pt);
      glossy_stop();
      ton_a += get_rtx_on();
      UPDATE_TF(tf_a, n_short_a, 0, N_TX_A);

      if (get_rx_cnt()) {
        if (get_data_len() == CRYSTAL_ACK_LEN 
            && get_app_header() == CRYSTAL_TYPE_ACK 
            && CRYSTAL_ACK_CMD_CORRECT(crystal_ack)) {
          correct_packet = 1;
          n_noacks = 0;
          n_bad_acks = 0;
          n_all_acks ++;
          // Updating the epoch in case we "skipped" some epochs but got an ACK
          // We can "skip" epochs if we are too late for the next TA and set the timer to the past
          epoch = crystal_ack->epoch; 

          #if (CRYSTAL_SYNC_ACKS)
          // sometimes we get a packet with a corrupted n_ta
          // (e.g. 234) that's why checking the value
          // sometimes also the ref time is reported incorrectly, so have to check
          if (IS_SYNCED() && crystal_ack->n_ta == n_ta
                  && correct_ack_skew(N_TA_TO_REF(get_t_ref_l(), crystal_ack->n_ta))
              ) {
            
            corrected_ref_time = N_TA_TO_REF(get_t_ref_l(), crystal_ack->n_ta);
            synced_with_ack ++;
            n_noack_epochs = 0; // it's important to reset it here to reenable TX right away (if it was suppressed)
          }
          #endif
          
          if (CRYSTAL_ACK_SLEEP(crystal_ack)) {
            sleep_order = 1;
          }
        }
        else {
          // received something but not an ack (we might be out of sync)
          // n_noacks ++; // keep it as it is to give another chance but not too many chances
          n_bad_acks ++;
        }

        // logging info about bad packets
        if (get_app_header() != CRYSTAL_TYPE_ACK)
          n_badtype_a ++;
        if (get_data_len() != CRYSTAL_ACK_LEN)
          n_badlen_a ++;

        n_high_noise = 0;
      }
      else if (is_corrupted()) { // bad CRC
        // n_noacks ++; // keep it as it is to give another chance but not too many chances
        n_bad_acks ++;
        n_badcrc_a ++;
        n_high_noise = 0;
      }
      else { // not received anything
        if (CRYSTAL_MAX_NOISY_AS == 0)
          n_noacks ++; // no "noise detection"
        else if (get_cca_busy_cnt() > CCA_COUNTER_THRESHOLD) {
          n_high_noise ++;
          if (n_high_noise > CRYSTAL_MAX_NOISY_AS)
            n_noacks ++;
        }
        else {
          n_noacks ++;
          n_high_noise = 0;
        }
      }

      app_post_A(correct_packet);

      if (i_tx)
        log_ta_tx();
      else
        log_ta_rx();

      BZERO_BUF();
// -- Phase A end (non-root) ------------------------------------------------- A end (non-root) ---
      n_ta ++;

      // shall we stop?
      if (sleep_order || (n_ta >= CRYSTAL_MAX_TAS) || // always stop when ordered or max is reached
          (epoch >= N_FULL_EPOCHS && (
              (  have_packet  && (n_noacks >= CRYSTAL_MAX_MISSING_ACKS)) ||
              ((!have_packet) && (n_noacks >= CRYSTAL_MAX_SILENT_TAS) && n_empty_ts >= CRYSTAL_MAX_SILENT_TAS)
            )
          )
        ) {
        
        break; // Stop the TA chain
      }
    } /* End of TA loop */

    if (!synced_with_ack) {
      n_noack_epochs ++;
    }
    cc2420_oscoff(); // deep sleep
    app_epoch_end();

#if CRYSTAL_LOGGING
    // Now we have a long pause, good time to print
    rtimer_set(t, corrected_ref_time + PHASE_T_OFFS(CRYSTAL_MAX_TAS) + LOGGING_GAP - CRYSTAL_REF_SHIFT, timer_handler, ptr);
    PT_YIELD(&pt);
    process_poll(&crystal_print_stats_process);
#endif //CRYSTAL_LOGGING

    s_guard = (!skew_estimated || sync_missed >= N_MISSED_FOR_INIT_GUARD)?CRYSTAL_INIT_GUARD:CRYSTAL_LONG_GUARD;

    // Schedule begin of next Glossy phase based on S reference time
    estimated_ref_time = corrected_ref_time_s + CRYSTAL_PERIOD + period_skew;
    t_phase_start = estimated_ref_time - CRYSTAL_REF_SHIFT - s_guard;
    t_phase_stop = t_phase_start + DUR_S + 2*s_guard;

    // time to wake up to prepare for the next epoch
    t_wakeup = t_phase_start - (OSC_STAB_TIME + GLOSSY_PRE_TIME + CRYSTAL_INTER_PHASE_GAP);

    while ((int16_t)(t_wakeup - (RTIMER_NOW() + APP_PING_INTERVAL)) > 16) {
      rtimer_set(t, RTIMER_NOW() + APP_PING_INTERVAL, timer_handler, ptr);
      app_ping();
      PT_YIELD(&pt);
    }

    rtimer_set(t, t_wakeup, timer_handler, ptr);
    PT_YIELD(&pt);

    if (sync_missed > N_SILENT_EPOCHS_TO_RESET && n_noack_epochs > N_SILENT_EPOCHS_TO_RESET) {
      SYSTEM_RESET();
    }
    
  }
  PT_END(&pt);
}

// TUNING AGC to avoid saturation in RSSI readings
// warning: it makes the reception less reliable
void tune_AGC_radio() {
  unsigned reg_agctst;
  FASTSPI_GETREG(CC2420_AGCTST1, reg_agctst);
  FASTSPI_SETREG(CC2420_AGCTST1, (reg_agctst + (1 << 8) + (1 << 13)));
}


PROCESS(crystal_test, "Crystal test");
AUTOSTART_PROCESSES(&crystal_test);
PROCESS_THREAD(crystal_test, ev, data)
{
  PROCESS_BEGIN();

  app_init();

  if (IS_SINK())
    timer_handler = sink_timer_handler;
  else
    timer_handler = nonsink_timer_handler;

  leds_on(LEDS_RED);

  channel = RF_CHANNEL;
  cc2420_set_txpower(TX_POWER);
  cc2420_set_cca_threshold(CCA_THRESHOLD);
  //tune_AGC_radio();

#if CRYSTAL_LOGGING
  // Start print stats processes.
  process_start(&crystal_print_stats_process, NULL);
  process_start(&alive_print_process, NULL);
#endif // CRYSTAL_LOGGING
  // Start Glossy busy-waiting process.
  process_start(&glossy_process, NULL);
  // Start Crystal
  rtimer_set(&rt, RTIMER_NOW() + 10, timer_handler, NULL);

  PROCESS_END();
}


#if CRYSTAL_LOGGING
PROCESS_THREAD(crystal_print_stats_process, ev, data)
{
  static int i;
  static int noise;
  static int first_time = 1;
  static unsigned long avg_radio_on;
  static uint16_t scan_channel;
  PROCESS_BEGIN();

  while(1) {
    PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
    scan_channel = channel_array[epoch % get_num_channels()];
    cc2420_set_channel(scan_channel);
    cc2420_oscon();
    noise = cc2420_rssi();
    cc2420_oscoff();

    if (!IS_SINK()) {
      printf("S %u:%u %u %u:%u %d %u\n", epoch, n_ta_tx, n_all_acks, synced_with_ack, sync_missed, period_skew, hopcount);
      printf("P %u:%u %u %u:%u %u %u %d:%ld\n", epoch, recvsrc_s, recvtype_s, recvlen_s, n_badtype_a, n_badlen_a, n_badcrc_a, ack_skew_err, end_of_s_time);
    }
    
    printf("R %u:%u %u:%d %u:%u %u %u\n", epoch, n_ta, n_rec_rx, noise, scan_channel, tx_count_s, rx_count_s, cca_busy_cnt);
    for (i=0; i<n_rec_rx; i++) {
      printf("T %u:%u %u %u %u %u %u %u %u\n", epoch, i,
        recv_t[i].type,
        recv_t[i].src,
        recv_t[i].seqn,
        recv_t[i].n_ta,
        recv_t[i].rx_count,
        recv_t[i].length,
        recv_t[i].err_code);
    }
    for (i=0; i<n_rec_tx; i++) {
      printf("Q %u:%u %u %u %u %u\n", epoch, i,
        send_t[i].seqn,
        send_t[i].n_ta,
        send_t[i].rx_count,
        send_t[i].acked);
    }

    app_print_logs();
    /*printf("D %u:%u %lu %u:%u %lu %u\n", epoch,
        glossy_S.T_slot_h, glossy_S.T_slot_h_sum, glossy_S.win_cnt,
        glossy_A.T_slot_h, glossy_A.T_slot_h_sum, glossy_A.win_cnt
        );*/
    
#if ENERGEST_CONF_ON
    if (!first_time) {
      // Compute average radio-on time.
      avg_radio_on = (energest_type_time(ENERGEST_TYPE_LISTEN) + energest_type_time(ENERGEST_TYPE_TRANSMIT))
         * 1e6 /
        (energest_type_time(ENERGEST_TYPE_CPU) + energest_type_time(ENERGEST_TYPE_LPM));
      // Print information about average radio-on time per second.
      printf("E %u:%lu.%03lu:%u %u %u\n", epoch,
          avg_radio_on / 1000, avg_radio_on % 1000, ton_s, ton_t, ton_a);
      printf("F %u:%u %u %u:%u %u %u\n", epoch,
          tf_s, tf_t, tf_a, n_short_s, n_short_t, n_short_a);
    }
    // Initialize Energest values.
    energest_init();
    first_time = 0;
#endif /* ENERGEST_CONF_ON */
      
    n_rec_rx = 0;
    n_rec_tx = 0;
  }
  
  PROCESS_END();
}

PROCESS_THREAD(alive_print_process, ev, data) {
  PROCESS_BEGIN();
  PROCESS_YIELD_UNTIL(ev == PROCESS_EVENT_POLL);
  printf("I am alive! EUI-64: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n", ds2411_id[0],ds2411_id[1],ds2411_id[2],ds2411_id[3],ds2411_id[4],ds2411_id[5],ds2411_id[6],ds2411_id[7]);
  PROCESS_END();
}

#endif //CRYSTAL_LOGGING
