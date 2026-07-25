#ifndef BSP_FLASH_H
#define BSP_FLASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The public BSP exposes one logical DATA-Flash sector without leaking its
 * MCU address or DriverLib types. Writes replace the complete sector content;
 * callers should therefore keep all persistent records in one image. */
#define BSP_FLASH_DATA_CAPACITY_WORDS 256U

bool BSP_Flash_DataAvailable(void);
bool BSP_Flash_DataReadWords(uint32_t *data, size_t word_count);
bool BSP_Flash_DataReplaceWords(uint32_t *data, size_t word_count);

#endif
