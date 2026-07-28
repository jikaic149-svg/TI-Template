#include "mode_communicate.h"

#include <stddef.h>

bool Mode_Communicate_BluetoothDualMCU_Init(
    Mode_Communicate_BluetoothDualMCUContext *context)
{
    if (context == NULL) {
        return false;
    }
    context->initialized = false;
    context->initialized = true;
    return true;
}

bool Mode_Communicate_BluetoothDualMCU_Update(
    const Mode_Communicate_BluetoothDualMCUContext *context,
    Mode_Communicate_BluetoothOutput *output)
{
    if (output == NULL) {
        return false;
    }
    *output = (Mode_Communicate_BluetoothOutput){
        .mode = BLUETOOTH_MODE_DUAL_MCU,
        .format = MODE_COMMUNICATE_BLUETOOTH_FORMAT_PACKET_V1,
        .duplex_policy = MODE_COMMUNICATE_DUPLEX_UNRESTRICTED,
        .sends_periodic_hello = true,
        .requires_peer_node_id = true,
    };
    return (context != NULL) && context->initialized;
}
