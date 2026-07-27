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

typedef struct {
    Mode_Communicate_BluetoothMode mode;
    Mode_Communicate_BluetoothFormat format;
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

#endif
