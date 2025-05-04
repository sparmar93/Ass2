#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE -1

struct sender_entry {
    struct pkt packet;
    int acked;
    float send_time;
};

static struct sender_entry send_window[WINDOWSIZE];
static int base_A;
static int nextseq_A;

/* Receiver side buffer */
struct receiver_entry {
    struct pkt packet;
    int received;
};

static struct receiver_entry recv_window[SEQSPACE];
static int expected_B;

int ComputeChecksum(struct pkt packet) {
    int checksum = 0;
    checksum += packet.seqnum + packet.acknum;
    for (int i = 0; i < 20; i++) {
        checksum += packet.payload[i];
    }
    return checksum;
}

bool IsCorrupted(struct pkt packet) {
    return ComputeChecksum(packet) != packet.checksum;
}

/******** A (Sender) ********/

void A_output(struct msg message) {
    int win_used = (nextseq_A - base_A + SEQSPACE) % SEQSPACE;
    if (win_used < WINDOWSIZE) {
        struct pkt p;
        for (int i = 0; i < 20; i++) {
            p.payload[i] = message.data[i];
        }
        p.seqnum = nextseq_A;
        p.acknum = NOTINUSE;
        p.checksum = ComputeChecksum(p);

        send_window[nextseq_A % WINDOWSIZE].packet = p;
        send_window[nextseq_A % WINDOWSIZE].acked = 0;

        if (TRACE > 0) {
            printf("Sending packet %d to layer 3\n", p.seqnum);
        }

        tolayer3(A, p);
        if (base_A == nextseq_A) {
            starttimer(A, RTT);
        }

        nextseq_A = (nextseq_A + 1) % SEQSPACE;
    } else {
        if (TRACE > 0) printf("----A: New message arrives, send window is full\n");
        window_full++;
    }
}

void A_input(struct pkt ackpkt) {
    if (!IsCorrupted(ackpkt)) {
        int acknum = ackpkt.acknum;
        if (TRACE > 0) {
            printf("----A: uncorrupted ACK %d is received\n", acknum);
        }

        total_ACKs_received++;

        if (!send_window[acknum % WINDOWSIZE].acked) {
            send_window[acknum % WINDOWSIZE].acked = 1;
            new_ACKs++;
        }

        while (send_window[base_A % WINDOWSIZE].acked && base_A != nextseq_A) {
            base_A = (base_A + 1) % SEQSPACE;
        }

        stoptimer(A);
        if (base_A != nextseq_A) {
            starttimer(A, RTT);
        }
    } else if (TRACE > 0) {
        printf("----A: corrupted ACK is received, do nothing!\n");
    }
}

void A_timerinterrupt(void) {
    if (TRACE > 0)
        printf("----A: time out,resend packets!\n");

    for (int i = 0; i < WINDOWSIZE; i++) {
        if (!send_window[i].acked) {
            tolayer3(A, send_window[i].packet);
            packets_resent++;
            if (TRACE > 0)
                printf("---A: resending packet %d\n", send_window[i].packet.seqnum);
        }
    }

    starttimer(A, RTT);
}

void A_init(void) {
    base_A = 0;
    nextseq_A = 0;
    for (int i = 0; i < WINDOWSIZE; i++) {
        send_window[i].acked = 1;
    }
}

/******** B (Receiver) ********/

void B_input(struct pkt packet) {
    struct pkt ackpkt;

    if (!IsCorrupted(packet)) {
        int seq = packet.seqnum;

        if ((seq >= expected_B && seq < expected_B + WINDOWSIZE) ||
            (expected_B + WINDOWSIZE >= SEQSPACE && seq < (expected_B + WINDOWSIZE) % SEQSPACE)) {

            if (!recv_window[seq].received) {
                recv_window[seq].packet = packet;
                recv_window[seq].received = 1;
                if (TRACE > 0)
                    printf("----B: packet %d is correctly received, send ACK!\n", seq);
            }

            while (recv_window[expected_B].received) {
                tolayer5(B, recv_window[expected_B].packet.payload);
                packets_received++;
                recv_window[expected_B].received = 0;
                expected_B = (expected_B + 1) % SEQSPACE;
            }
        } else {
            if (TRACE > 0)
                printf("----B: packet %d is outside window, resend ACK!\n", seq);
        }
    } else {
        if (TRACE > 0)
            printf("----B: packet corrupted, resend last ACK\n");
    }

    ackpkt.acknum = packet.seqnum;
    ackpkt.seqnum = 0;
    for (int i = 0; i < 20; i++)
        ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);

    tolayer3(B, ackpkt);
    if (TRACE > 0)
        printf("Sending ACK %d to layer 3\n", ackpkt.acknum);
}

void B_init(void) {
    expected_B = 0;
    for (int i = 0; i < SEQSPACE; i++) {
        recv_window[i].received = 0;
    }
}

/* Optional functions for bidirectional */
void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
