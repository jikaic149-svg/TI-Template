#ifndef DEVICE_FLASH_H
#define DEVICE_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Transport-independent access to the board's single persistent DATA-Flash
 * image. MCU addresses, erase rules and interrupt protection stay in BSP. */
bool Device_Flash_IsAvailable(void);
bool Device_Flash_ReadWords(uint32_t *data, size_t word_count);
bool Device_Flash_ReplaceWords(uint32_t *data, size_t word_count);

#endif
