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

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE (-1)

#define bool int
#define true 1
#define false 0

static float current_sim_time = 0.0;

int ComputeChecksum(struct pkt packet) {
  int checksum = 0;
  for (int i = 0; i < 20; i++) checksum += (int)(packet.payload[i]);
  checksum += packet.seqnum + packet.acknum;
  return checksum;
}

bool IsCorrupted(struct pkt packet) {
  return ComputeChecksum(packet) != packet.checksum;
}

/* ---------- Sender (A) ----------- */

struct sender_entry {
  struct pkt packet;
  int acked;
  float sent_time;
};

static struct sender_entry send_window[WINDOWSIZE];
static int base;
static int nextseqnum;

void A_output(struct msg message) {
  if ((nextseqnum + SEQSPACE - base) % SEQSPACE < WINDOWSIZE) {
    struct pkt pkt;
    for (int i = 0; i < 20; i++) pkt.payload[i] = message.data[i];
    pkt.seqnum = nextseqnum;
    pkt.acknum = NOTINUSE;
    pkt.checksum = ComputeChecksum(pkt);

    send_window[nextseqnum % WINDOWSIZE].packet = pkt;
    send_window[nextseqnum % WINDOWSIZE].acked = 0;
    send_window[nextseqnum % WINDOWSIZE].sent_time = current_sim_time;

    if (TRACE > 0) {
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");
      printf("Sending packet %d to layer 3\n", pkt.seqnum);
    }

    tolayer3(A, pkt);

    if (base == nextseqnum) starttimer(A, RTT);
    nextseqnum = (nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}

void A_input(struct pkt packet) {
  if (!IsCorrupted(packet)) {
    int acknum = packet.acknum;
    if (!send_window[acknum % WINDOWSIZE].acked) {
      send_window[acknum % WINDOWSIZE].acked = 1;
      if (TRACE > 0) {
        printf("----A: uncorrupted ACK %d is received\n", acknum);
        printf("----A: ACK %d is not a duplicate\n", acknum);
      }
      total_ACKs_received++;
      new_ACKs++;

      while (send_window[base % WINDOWSIZE].acked && base != nextseqnum) {
        base = (base + 1) % SEQSPACE;
      }

      stoptimer(A);
      if (base != nextseqnum) starttimer(A, RTT);
    }
  } else if (TRACE > 0) {
    printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void) {
  current_sim_time += RTT;
  float now = current_sim_time;

  if (TRACE > 0)
    printf("----A: time out,resend packets!\n");

  for (int i = 0; i < WINDOWSIZE; i++) {
    if (!send_window[i].acked && (now - send_window[i].sent_time >= RTT)) {
      tolayer3(A, send_window[i].packet);
      send_window[i].sent_time = now;
      packets_resent++;
      if (TRACE > 0)
        printf("---A: resending packet %d\n", send_window[i].packet.seqnum);
    }
  }

  starttimer(A, RTT);
}

void A_init(void) {
  base = 0;
  nextseqnum = 0;
  for (int i = 0; i < WINDOWSIZE; i++)
    send_window[i].acked = 1;
}

/* ----------- Receiver (B) ---------- */

struct receiver_entry {
  struct pkt packet;
  int received;
};

static struct receiver_entry recv_window[WINDOWSIZE];
static int expectedseqnum;

void B_input(struct pkt packet) {
  struct pkt ackpkt;
  int seq = packet.seqnum;

  if (!IsCorrupted(packet)) {
    if ((seq >= expectedseqnum && seq < expectedseqnum + WINDOWSIZE) ||
        (expectedseqnum + WINDOWSIZE >= SEQSPACE && seq < (expectedseqnum + WINDOWSIZE) % SEQSPACE)) {

      int index = seq % WINDOWSIZE;
      if (!recv_window[index].received) {
        recv_window[index].packet = packet;
        recv_window[index].received = 1;
        if (TRACE > 0)
          printf("----B: packet %d is correctly received, send ACK!\n", seq);
      }

      while (recv_window[expectedseqnum % WINDOWSIZE].received) {
        tolayer5(B, recv_window[expectedseqnum % WINDOWSIZE].packet.payload);
        packets_received++;
        recv_window[expectedseqnum % WINDOWSIZE].received = 0;
        expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
      }
    } else if (TRACE > 0) {
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
    }
  } else {
    if (TRACE > 0) printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");
  }

  ackpkt.seqnum = 0;
  ackpkt.acknum = packet.seqnum;
  for (int i = 0; i < 20; i++) ackpkt.payload[i] = '0';
  ackpkt.checksum = ComputeChecksum(ackpkt);
  tolayer3(B, ackpkt);
  if (TRACE > 0)
    printf("Sending ACK %d to layer 3\n", ackpkt.acknum);
}

void B_init(void) {
  expectedseqnum = 0;
  for (int i = 0; i < WINDOWSIZE; i++)
    recv_window[i].received = 0;
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
