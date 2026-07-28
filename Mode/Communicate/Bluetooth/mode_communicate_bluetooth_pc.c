#include "mode_communicate.h"

#include <stddef.h>

bool Mode_Communicate_BluetoothPC_Init(
    Mode_Communicate_BluetoothPCContext *context)
{
    if (context == NULL) {
        return false;
    }
    context->initialized = false;
    context->initialized = true;
    return true;
}

bool Mode_Communicate_BluetoothPC_Update(
    const Mode_Communicate_BluetoothPCContext *context,
    Mode_Communicate_BluetoothOutput *output)
{
    if (output == NULL) {
        return false;
    }
    *output = (Mode_Communicate_BluetoothOutput){
        .mode = BLUETOOTH_MODE_PC,
        .format = MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM,
        .duplex_policy = MODE_COMMUNICATE_DUPLEX_UNRESTRICTED,
        .sends_periodic_hello = false,
        .requires_peer_node_id = false,
    };
    return (context != NULL) && context->initialized;
}
