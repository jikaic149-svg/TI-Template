#ifndef MODE_COMMUNICATE_H
#define MODE_COMMUNICATE_H

#include <stdbool.h>

typedef enum {
    BLUETOOTH_MODE_PC = 0,
    BLUETOOTH_MODE_DUAL_MCU,
} Mode_Communicate_BluetoothMode;

typedef enum {
    MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM = 0,
    MODE_COMMUNICATE_BLUETOOTH_FORMAT_PACKET_V1,
} Mode_Communicate_BluetoothFormat;

typedef enum {
    /* Software does not serialize TX and RX. This is not evidence that the
     * external radio PHY is physically full duplex. */
    MODE_COMMUNICATE_DUPLEX_UNRESTRICTED = 0,
    MODE_COMMUNICATE_DUPLEX_HALF_DUPLEX_TURN,
} Mode_Communicate_DuplexPolicy;

typedef struct {
    Mode_Communicate_BluetoothMode mode;
    Mode_Communicate_BluetoothFormat format;
    Mode_Communicate_DuplexPolicy duplex_policy;
    bool sends_periodic_hello;
    bool requires_peer_node_id;
} Mode_Communicate_BluetoothOutput;

typedef struct {
    bool initialized;
} Mode_Communicate_BluetoothPCContext;

bool Mode_Communicate_BluetoothPC_Init(
    Mode_Communicate_BluetoothPCContext *context);
bool Mode_Communicate_BluetoothPC_Update(
    const Mode_Communicate_BluetoothPCContext *context,
    Mode_Communicate_BluetoothOutput *output);

typedef struct {
    bool initialized;
} Mode_Communicate_BluetoothDualMCUContext;

bool Mode_Communicate_BluetoothDualMCU_Init(
    Mode_Communicate_BluetoothDualMCUContext *context);
bool Mode_Communicate_BluetoothDualMCU_Update(
    const Mode_Communicate_BluetoothDualMCUContext *context,
    Mode_Communicate_BluetoothOutput *output);

typedef enum {
    MODE_COMMUNICATE_LORA_TURN_LOCAL = 0,
    MODE_COMMUNICATE_LORA_TURN_PEER,
} Mode_Communicate_LoRaTurn;

typedef struct {
    Mode_Communicate_LoRaTurn turn;
    bool initialized;
} Mode_Communicate_LoRaHalfDuplexContext;

typedef struct {
    Mode_Communicate_DuplexPolicy duplex_policy;
    Mode_Communicate_LoRaTurn turn;
    bool transmit_allowed;
    bool receive_expected;
} Mode_Communicate_LoRaHalfDuplexOutput;

/* The two peers must start with complementary turns. The Mode does not assign
 * an address as initiator because that is a product decision. */
bool Mode_Communicate_LoRaHalfDuplex_Init(
    Mode_Communicate_LoRaHalfDuplexContext *context,
    Mode_Communicate_LoRaTurn initial_turn);
bool Mode_Communicate_LoRaHalfDuplex_Update(
    const Mode_Communicate_LoRaHalfDuplexContext *context,
    Mode_Communicate_LoRaHalfDuplexOutput *output);

/* Call only after Device_LoRa_Send returned OK. BUSY/error leaves the local
 * turn unchanged. This hands the next transmission opportunity to the peer. */
bool Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(
    Mode_Communicate_LoRaHalfDuplexContext *context);

/* Call only after the Task/protocol layer has validated one complete peer
 * message. Raw UART byte arrival alone is not a message boundary. */
bool Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(
    Mode_Communicate_LoRaHalfDuplexContext *context);

#endif
