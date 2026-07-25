#ifndef BSP_SPI_H
#define BSP_SPI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef const struct BSP_SPI_Resource *BSP_SPI_Handle;

typedef enum {
    BSP_SPI_RESULT_OK = 0,
    BSP_SPI_RESULT_INVALID_ARGUMENT,
    BSP_SPI_RESULT_BUSY,
    BSP_SPI_RESULT_TIMEOUT,
} BSP_SPI_Result;

typedef struct {
    uint32_t bit_rate_hz;
    size_t chip_select_count;
} BSP_SPI_Info;

/* Return the number of logical controller transports provided by this BSP. */
size_t BSP_SPI_Count(void);

/* Return a program-lifetime logical handle, or NULL for an invalid index. */
BSP_SPI_Handle BSP_SPI_GetHandle(size_t index);

/* Read immutable transport information. Failure leaves info unchanged. */
bool BSP_SPI_GetInfo(BSP_SPI_Handle spi, BSP_SPI_Info *info);

/* Perform one full-duplex transaction while keeping the selected software CS
 * active-low for the complete transfer. A NULL tx buffer sends zero bytes and
 * a NULL rx buffer discards received bytes. timeout_ms is one finite deadline
 * shared by the entire transfer. */
BSP_SPI_Result BSP_SPI_Transfer(
    BSP_SPI_Handle spi,
    size_t chip_select_index,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length,
    uint32_t timeout_ms);

#endif
