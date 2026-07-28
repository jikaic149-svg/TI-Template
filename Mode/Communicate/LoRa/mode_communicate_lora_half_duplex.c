#include "mode_communicate.h"

#include <stddef.h>

static bool valid_turn(Mode_Communicate_LoRaTurn turn)
{
    return (turn == MODE_COMMUNICATE_LORA_TURN_LOCAL) ||
        (turn == MODE_COMMUNICATE_LORA_TURN_PEER);
}

bool Mode_Communicate_LoRaHalfDuplex_Init(
    Mode_Communicate_LoRaHalfDuplexContext *context,
    Mode_Communicate_LoRaTurn initial_turn)
{
    if (context != NULL) {
        *context = (Mode_Communicate_LoRaHalfDuplexContext){0};
    }
    if ((context == NULL) || !valid_turn(initial_turn)) {
        return false;
    }
    context->turn = initial_turn;
    context->initialized = true;
    return true;
}

bool Mode_Communicate_LoRaHalfDuplex_Update(
    const Mode_Communicate_LoRaHalfDuplexContext *context,
    Mode_Communicate_LoRaHalfDuplexOutput *output)
{
    if (output != NULL) {
        *output = (Mode_Communicate_LoRaHalfDuplexOutput){0};
    }
    if ((context == NULL) || !context->initialized ||
        !valid_turn(context->turn) || (output == NULL)) {
        return false;
    }

    *output = (Mode_Communicate_LoRaHalfDuplexOutput){
        .duplex_policy = MODE_COMMUNICATE_DUPLEX_HALF_DUPLEX_TURN,
        .turn = context->turn,
        .transmit_allowed =
            context->turn == MODE_COMMUNICATE_LORA_TURN_LOCAL,
        .receive_expected =
            context->turn == MODE_COMMUNICATE_LORA_TURN_PEER,
    };
    return true;
}

bool Mode_Communicate_LoRaHalfDuplex_NotifyTxAccepted(
    Mode_Communicate_LoRaHalfDuplexContext *context)
{
    if ((context == NULL) || !context->initialized ||
        (context->turn != MODE_COMMUNICATE_LORA_TURN_LOCAL)) {
        return false;
    }
    context->turn = MODE_COMMUNICATE_LORA_TURN_PEER;
    return true;
}

bool Mode_Communicate_LoRaHalfDuplex_NotifyPeerFrameReceived(
    Mode_Communicate_LoRaHalfDuplexContext *context)
{
    if ((context == NULL) || !context->initialized ||
        (context->turn != MODE_COMMUNICATE_LORA_TURN_PEER)) {
        return false;
    }
    context->turn = MODE_COMMUNICATE_LORA_TURN_LOCAL;
    return true;
}
