/* Host receiver admission budgets, not a claim about camera-server limits. */
#ifndef SSP_RECEIVER_LIMITS_H
#define SSP_RECEIVER_LIMITS_H
#define SSP_MAX_PACKET_BYTES (32u * 1024u * 1024u)
#define SSP_MAX_IPC_BODY_BYTES (SSP_MAX_PACKET_BYTES + 1024u)
#define SSP_MAX_QUEUE_BYTES (64u * 1024u * 1024u)
#define SSP_MAX_QUEUE_PACKETS 32u
#endif
