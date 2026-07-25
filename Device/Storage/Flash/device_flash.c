#include "device_flash.h"

#include "bsp_flash.h"

bool Device_Flash_IsAvailable(void)
{
    return BSP_Flash_DataAvailable();
}

bool Device_Flash_ReadWords(uint32_t *data, size_t word_count)
{
    return BSP_Flash_DataReadWords(data, word_count);
}

bool Device_Flash_ReplaceWords(uint32_t *data, size_t word_count)
{
    return BSP_Flash_DataReplaceWords(data, word_count);
}
