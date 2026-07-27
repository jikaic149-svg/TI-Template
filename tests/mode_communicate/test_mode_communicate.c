#include "mode_communicate.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    Mode_Communicate_BluetoothPCContext pc_context = {0};
    Mode_Communicate_BluetoothDualMCUContext dual_mcu_context = {0};
    Mode_Communicate_BluetoothOutput output;

    assert(!Mode_Communicate_BluetoothPC_Update(&pc_context, &output));
    assert(Mode_Communicate_BluetoothPC_Init(&pc_context));
    assert(Mode_Communicate_BluetoothPC_Update(&pc_context, &output));
    assert(output.mode == BLUETOOTH_MODE_PC);
    assert(output.format == MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM);
    assert(!output.sends_periodic_hello);
    assert(!output.requires_peer_node_id);

    assert(!Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, &output));
    assert(Mode_Communicate_BluetoothDualMCU_Init(&dual_mcu_context));
    assert(Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, &output));
    assert(output.mode == BLUETOOTH_MODE_DUAL_MCU);
    assert(output.format == MODE_COMMUNICATE_BLUETOOTH_FORMAT_PACKET_V1);
    assert(output.sends_periodic_hello);
    assert(output.requires_peer_node_id);

    assert(!Mode_Communicate_BluetoothPC_Init(NULL));
    assert(!Mode_Communicate_BluetoothPC_Update(NULL, &output));
    assert(!Mode_Communicate_BluetoothPC_Update(&pc_context, NULL));
    assert(!Mode_Communicate_BluetoothDualMCU_Init(NULL));
    assert(!Mode_Communicate_BluetoothDualMCU_Update(NULL, &output));
    assert(!Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, NULL));

    puts("Communicate mode tests: PASS");
    return 0;
}
