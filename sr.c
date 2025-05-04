#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

/* ******************************************************************
   Go Back N protocol.  Adapted from J.F.Kurose
   ALTERNATING BIT AND GO-BACK-N NETWORK EMULATOR: VERSION 1.2

   Network properties:
   - one way network delay averages five time units (longer if there
   are other messages in the channel for GBN), but can be larger
   - packets can be corrupted (either the header or the data portion)
   or lost, according to user-defined probabilities
   - packets will be delivered in the order in which they were sent
   (although some can be lost).

   Modifications:
   - removed bidirectional GBN code and other code not used by prac.
   - fixed C style to adhere to current programming style
   - added GBN implementation
**********************************************************************/

#define RTT  16.0       /* round trip time.  MUST BE SET TO 16.0 when submitting assignment */
#define WINDOWSIZE 6    /* the maximum number of buffered unacked packet
                          MUST BE SET TO 6 when submitting assignment */
#define SEQSPACE 7      /* the min sequence space for GBN must be at least windowsize + 1 */
#define NOTINUSE (-1)   /* used to fill header fields that are not being used */
#define bool int
#define true 1
#define false 0

/* generic procedure to compute the checksum of a packet.  Used by both sender and receiver
   the simulator will overwrite part of your packet with 'z's.  It will not overwrite your
   original checksum.  This procedure must generate a different checksum to the original if
   the packet is corrupted.
*/
int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for ( i=0; i<20; i++ )
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  if (packet.checksum == ComputeChecksum(packet))
    return (false);
  else
    return (true);
}


/********* Sender (A) variables and functions ************/

struct sender_entry {
  struct pkt packet;
  int acked;
  float sent_time;
};

static struct sender_entry send_window[WINDOWSIZE];  /* array for storing packets waiting for ACK */
static int base;    /* array indexes of the first/last packet awaiting ACK */
static int nextseqnum;               /* the next sequence number to be used by the sender */
static float current_sim_time = 0.0;

/* called from layer 5 (application layer), passed the message to be sent to other side */
void A_output(struct msg message)
{
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE < WINDOWSIZE) {
    struct pkt pkt;
    int i;
    pkt.seqnum = nextseqnum;
    pkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++) pkt.payload[i] = message.data[i];
    pkt.checksum = ComputeChecksum(pkt);

    send_window[nextseqnum % WINDOWSIZE].packet = pkt;
    send_window[nextseqnum % WINDOWSIZE].acked = 0;
    send_window[nextseqnum % WINDOWSIZE].sent_time = current_sim_time;

    tolayer3(A, pkt);
    if (TRACE > 0)
      printf("A: Sent packet %d\n", pkt.seqnum);

    if (base == nextseqnum) starttimer(A, RTT);
    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("A: Window full, message dropped\n");
    window_full++;
  }
}


/* called from layer 3, when a packet arrives for layer 4
   In this practical this will always be an ACK as B never sends data.
*/
void A_input(struct pkt packet)
{
  if (!IsCorrupted(packet)) {
    int acknum = packet.acknum;
    if (!send_window[acknum % WINDOWSIZE].acked) {
      send_window[acknum % WINDOWSIZE].acked = 1;
      if (TRACE > 0)
        printf("A: ACK %d received\n", acknum);
      total_ACKs_received++;
      new_ACKs++;

      while (send_window[base % WINDOWSIZE].acked && base != nextseqnum) {
        base = (base + 1) % SEQSPACE;
      }

      stoptimer(A);
      if (base != nextseqnum) starttimer(A, RTT);
    }
  } else if (TRACE > 0) {
    printf("A: Corrupted ACK received\n");
  }
}

/* called when A's timer goes off */
void A_timerinterrupt(void)
{
  current_sim_time += RTT;
  float now = current_sim_time;
  int i;
  for (i = 0; i < WINDOWSIZE; i++) {
    if (!send_window[i].acked && (now - send_window[i].sent_time >= RTT)) {
      tolayer3(A, send_window[i].packet);
      send_window[i].sent_time = now;
      packets_resent++;
      if (TRACE > 0)
        printf("A: Timeout, retransmitting packet %d\n", send_window[i].packet.seqnum);
    }
  }
  starttimer(A, RTT);
}



/* the following routine will be called once (only) before any other */
/* entity A routines are called. You can use it to do any initialization */
void A_init(void)
{
  base = 0;
  nextseqnum = 0;
  int i;
  for (i = 0; i < WINDOWSIZE; i++){
    send_window[i].acked = 1;
  }
  /* initialise A's window, buffer and sequence number 
  A_nextseqnum = 0;   A starts with seq num 0, do not change this 
  windowfirst = 0;
  windowlast = -1;    windowlast is where the last packet sent is stored.
		     new packets are placed in winlast + 1
		     so initially this is set to -1
		   
  windowcount = 0; */
}



/********* Receiver (B)  variables and procedures ************/

/*static int expectedseqnum;  the sequence number expected next by the receiver */
/*static int B_nextseqnum;    the sequence number for the next packets sent by B */

struct receiver_entry {
  struct pkt packet;
  int received;
};

static struct receiver_entry recv_window[WINDOWSIZE];
static int expectedseqnum;

/* called from layer 3, when a packet arrives for layer 4 at B*/
void B_input(struct pkt packet)
{
  struct pkt ackpkt;
  int seq = packet.seqnum;
  int i;

  if (!IsCorrupted(packet)) {
    if ((seq >= expectedseqnum && seq < expectedseqnum + WINDOWSIZE) ||
        (expectedseqnum + WINDOWSIZE >= SEQSPACE && seq < (expectedseqnum + WINDOWSIZE) % SEQSPACE)) {

      int index = seq % WINDOWSIZE;
      if (!recv_window[index].received) {
        recv_window[index].packet = packet;
        recv_window[index].received = 1;
        if (TRACE > 0) printf("B: Packet %d received and buffered\n", seq);
      }

      while (recv_window[expectedseqnum % WINDOWSIZE].received) {
        tolayer5(B, recv_window[expectedseqnum % WINDOWSIZE].packet.payload);
        packets_received++;
        recv_window[expectedseqnum % WINDOWSIZE].received = 0;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      }
    } else if (TRACE > 0) {
      printf("B: Packet %d out of window\n", seq);
    }
  } else {
    if (TRACE > 0) printf("B: Packet corrupted\n");
  }

  ackpkt.seqnum = 0;
  ackpkt.acknum = packet.seqnum;
  for (i = 0; i < 20; i++) ackpkt.payload[i] = '0';
  ackpkt.checksum = ComputeChecksum(ackpkt);
  tolayer3(B, ackpkt);
  if (TRACE > 0) printf("B: ACK %d sent\n", ackpkt.acknum);
}

/* the following routine will be called once (only) before any other */
/* entity B routines are called. You can use it to do any initialization */
void B_init(void)
{
  expectedseqnum = 0;
  int i;
  for (i = 0; i < WINDOWSIZE; i++) recv_window[i].received = 0;
}

/******************************************************************************
 * The following functions need be completed only for bi-directional messages *
 *****************************************************************************/

/* Note that with simplex transfer from a-to-B, there is no B_output() */
void B_output(struct msg message)
{
}

/* called when B's timer goes off */
void B_timerinterrupt(void)
{
}
