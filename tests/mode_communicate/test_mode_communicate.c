#include "mode_communicate.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    Mode_Communicate_BluetoothPCContext pc_context = {0};
    Mode_Communicate_BluetoothDualMCUContext dual_mcu_context = {0};
    Mode_Communicate_BluetoothOutput output;
    Mode_Communicate_LoRaHalfDuplexContext lora_a = {0};
    Mode_Communicate_LoRaHalfDuplexContext lora_b = {0};
    Mode_Communicate_LoRaHalfDuplexOutput lora_output;

    assert(!Mode_Communicate_BluetoothPC_Update(&pc_context, &output));
    assert(Mode_Communicate_BluetoothPC_Init(&pc_context));
    assert(Mode_Communicate_BluetoothPC_Update(&pc_context, &output));
    assert(output.mode == BLUETOOTH_MODE_PC);
    assert(output.format == MODE_COMMUNICATE_BLUETOOTH_FORMAT_RAW_STREAM);
    assert(output.duplex_policy == MODE_COMMUNICATE_DUPLEX_UNRESTRICTED);
    assert(!output.sends_periodic_hello);
    assert(!output.requires_peer_node_id);

    assert(!Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, &output));
    assert(Mode_Communicate_BluetoothDualMCU_Init(&dual_mcu_context));
    assert(Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, &output));
    assert(output.mode == BLUETOOTH_MODE_DUAL_MCU);
    assert(output.format == MODE_COMMUNICATE_BLUETOOTH_FORMAT_PACKET_V1);
    assert(output.duplex_policy == MODE_COMMUNICATE_DUPLEX_UNRESTRICTED);
    assert(output.sends_periodic_hello);
    assert(output.requires_peer_node_id);

    assert(!Mode_Communicate_LoRaHalfDuplex_Update(
        &lora_a, &lora_output));
    assert(Mode_Communicate_LoRaHalfDuplex_Init(
        &lora_a, MODE_COMMUNICATE_LORA_TURN_LOCAL));
    assert(Mode_Communicate_LoRaHalfDuplex_Init(
        &lora_b, MODE_COMMUNICATE_LORA_TURN_PEER));
    assert(Mode_Communicate_LoRaHalfDuplex_Update(
        &lora_a, &lora_output));
    assert(lora_output.duplex_policy ==
        MODE_COMMUNICATE_DUPLEX_HALF_DUPLEX_TURN);
    assert(lora_output.turn == MODE_COMMUNICATE_LORA_TURN_LOCAL);
    assert(lora_output.transmit_allowed);
    assert(!lora_output.receive_expected);
    assert(!Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(
        &lora_a));

    /* A sends, B validates the message and receives the next turn. */
    assert(Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(&lora_a));
    assert(!Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(&lora_a));
    assert(Mode_Communicate_LoRaHalfDuplex_Update(
        &lora_a, &lora_output));
    assert(!lora_output.transmit_allowed);
    assert(lora_output.receive_expected);
    assert(Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(
        &lora_b));
    assert(Mode_Communicate_LoRaHalfDuplex_Update(
        &lora_b, &lora_output));
    assert(lora_output.transmit_allowed);

    /* B replies, then A receives the next local turn. */
    assert(Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(&lora_b));
    assert(Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(
        &lora_a));
    assert(Mode_Communicate_LoRaHalfDuplex_Update(
        &lora_a, &lora_output));
    assert(lora_output.transmit_allowed);

    assert(!Mode_Communicate_BluetoothPC_Init(NULL));
    assert(!Mode_Communicate_BluetoothPC_Update(NULL, &output));
    assert(!Mode_Communicate_BluetoothPC_Update(&pc_context, NULL));
    assert(!Mode_Communicate_BluetoothDualMCU_Init(NULL));
    assert(!Mode_Communicate_BluetoothDualMCU_Update(NULL, &output));
    assert(!Mode_Communicate_BluetoothDualMCU_Update(
        &dual_mcu_context, NULL));
    assert(!Mode_Communicate_LoRaHalfDuplex_Init(NULL,
        MODE_COMMUNICATE_LORA_TURN_LOCAL));
    assert(!Mode_Communicate_LoRaHalfDuplex_Init(
        &lora_a, (Mode_Communicate_LoRaTurn)99));
    assert(!Mode_Communicate_LoRaHalfDuplex_Update(NULL, &lora_output));
    assert(!Mode_Communicate_LoRaHalfDuplex_Update(&lora_a, NULL));
    assert(!Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(NULL));
    assert(!Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(NULL));

    puts("Communicate mode tests: PASS");
    return 0;
}
