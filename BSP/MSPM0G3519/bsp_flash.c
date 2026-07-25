#include "bsp_flash.h"

#include "bsp_resource_map.h"

#include <ti/driverlib/dl_flashctl.h>

_Static_assert(
    BSP_FLASH_DATA_CAPACITY_WORDS * sizeof(uint32_t) ==
        DL_FLASHCTL_SECTOR_SIZE,
    "BSP DATA-Flash capacity must match one hardware sector");

bool BSP_Flash_DataAvailable(void)
{
    return true;
}

bool BSP_Flash_DataReadWords(uint32_t *data, size_t word_count)
{
    volatile const uint32_t *const source =
        (volatile const uint32_t *)(uintptr_t)FLASHCTL_DATA_ADDRESS;

    if ((data == NULL) || (word_count == 0U) ||
        (word_count > BSP_FLASH_DATA_CAPACITY_WORDS)) {
        return false;
    }
    for (size_t index = 0U; index < word_count; ++index) {
        data[index] = source[index];
    }
    return true;
}

bool BSP_Flash_DataReplaceWords(uint32_t *data, size_t word_count)
{
    DL_FLASHCTL_COMMAND_STATUS status;
    const uint32_t primask = __get_PRIMASK();

    if ((data == NULL) || (word_count == 0U) ||
        ((word_count & 1U) != 0U) ||
        (word_count > BSP_FLASH_DATA_CAPACITY_WORDS)) {
        return false;
    }

    /* The DriverLib FromRAM operations block until DATA Flash is readable
     * again. Prevent an interrupt from fetching a handler from main Flash
     * while either command is active, then restore the caller's IRQ state. */
    __disable_irq();
    DL_FlashCTL_executeClearStatus(FLASHCTL);
    DL_FlashCTL_unprotectSector(FLASHCTL, FLASHCTL_DATA_ADDRESS,
        DL_FLASHCTL_REGION_SELECT_MAIN);
    status = DL_FlashCTL_eraseMemoryFromRAM(FLASHCTL,
        FLASHCTL_DATA_ADDRESS, DL_FLASHCTL_COMMAND_SIZE_SECTOR);
    if (status == DL_FLASHCTL_COMMAND_STATUS_PASSED) {
        DL_FlashCTL_executeClearStatus(FLASHCTL);
        DL_FlashCTL_unprotectSector(FLASHCTL, FLASHCTL_DATA_ADDRESS,
            DL_FLASHCTL_REGION_SELECT_MAIN);
        status = DL_FlashCTL_programMemoryBlockingFromRAM64WithECCGenerated(
            FLASHCTL, FLASHCTL_DATA_ADDRESS, data, (uint32_t)word_count,
            DL_FLASHCTL_REGION_SELECT_MAIN);
    }
    if (primask == 0U) {
        __enable_irq();
    }
    if (status != DL_FLASHCTL_COMMAND_STATUS_PASSED) {
        return false;
    }

    volatile const uint32_t *const stored =
        (volatile const uint32_t *)(uintptr_t)FLASHCTL_DATA_ADDRESS;
    for (size_t index = 0U; index < word_count; ++index) {
        if (stored[index] != data[index]) {
            return false;
        }
    }
    return true;
}
