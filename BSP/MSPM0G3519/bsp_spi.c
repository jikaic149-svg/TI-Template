#include "bsp_spi.h"

#include <stddef.h>

#include "bsp_resource_map.h"

#define BSP_SPI_COUNT 2U
#define BSP_SPI_MAX_CHIP_SELECTS 2U
#define BSP_SPI_WAIT_STEPS_MS 100U

struct BSP_SPI_Resource {
    SPI_Regs *instance;
    uint32_t bit_rate_hz;
    size_t chip_select_count;
    GPIO_Regs *chip_select_port[BSP_SPI_MAX_CHIP_SELECTS];
    uint32_t chip_select_pin[BSP_SPI_MAX_CHIP_SELECTS];
    bool busy;
};

static struct BSP_SPI_Resource g_spi[BSP_SPI_COUNT] = {
    {
        .instance = BSP_RESOURCE_BMI088_SPI,
        .bit_rate_hz = BSP_RESOURCE_BMI088_SPI_BAUD,
        .chip_select_count = 2U,
        .chip_select_port = {
            BSP_RESOURCE_BMI088_ACCEL_CS_PORT,
            BSP_RESOURCE_BMI088_GYRO_CS_PORT,
        },
        .chip_select_pin = {
            BSP_RESOURCE_BMI088_ACCEL_CS_PIN,
            BSP_RESOURCE_BMI088_GYRO_CS_PIN,
        },
    },
    {
        .instance = BSP_RESOURCE_ICM45686_SPI,
        .bit_rate_hz = BSP_RESOURCE_ICM45686_SPI_BAUD,
        .chip_select_count = 1U,
        .chip_select_port = {BSP_RESOURCE_ICM45686_CS_PORT, NULL},
        .chip_select_pin = {BSP_RESOURCE_ICM45686_CS_PIN, 0U},
    },
};

static struct BSP_SPI_Resource *spi_from_handle(BSP_SPI_Handle spi)
{
    for (size_t index = 0U; index < BSP_SPI_COUNT; ++index) {
        if (spi == &g_spi[index]) {
            return &g_spi[index];
        }
    }
    return NULL;
}

static uint32_t wait_budget(uint32_t timeout_ms)
{
    if (timeout_ms > (UINT32_MAX / BSP_SPI_WAIT_STEPS_MS)) {
        return UINT32_MAX;
    }
    return timeout_ms * BSP_SPI_WAIT_STEPS_MS;
}

static bool wait_step(uint32_t *remaining)
{
    if (*remaining == 0U) {
        return false;
    }
    --(*remaining);
    DL_Common_delayCycles(BSP_RESOURCE_CPU_CLOCK_HZ / 100000U);
    return true;
}

static void chip_select_set(
    const struct BSP_SPI_Resource *spi, size_t index, bool inactive)
{
    if (inactive) {
        DL_GPIO_setPins(
            spi->chip_select_port[index], spi->chip_select_pin[index]);
    } else {
        DL_GPIO_clearPins(
            spi->chip_select_port[index], spi->chip_select_pin[index]);
    }
}

static void drain_receive_fifo(SPI_Regs *instance)
{
    uint8_t discarded;

    while (DL_SPI_receiveDataCheck8(instance, &discarded)) {
    }
}

void BSP_SPI_Init(void)
{
    for (size_t index = 0U; index < BSP_SPI_COUNT; ++index) {
        for (size_t select = 0U;
             select < g_spi[index].chip_select_count;
             ++select) {
            chip_select_set(&g_spi[index], select, true);
        }
        drain_receive_fifo(g_spi[index].instance);
        g_spi[index].busy = false;
    }
}

size_t BSP_SPI_Count(void)
{
    return BSP_SPI_COUNT;
}

BSP_SPI_Handle BSP_SPI_GetHandle(size_t index)
{
    return (index < BSP_SPI_COUNT) ? &g_spi[index] : NULL;
}

bool BSP_SPI_GetInfo(BSP_SPI_Handle spi, BSP_SPI_Info *info)
{
    struct BSP_SPI_Resource *resource = spi_from_handle(spi);

    if ((resource == NULL) || (info == NULL)) {
        return false;
    }
    *info = (BSP_SPI_Info){
        .bit_rate_hz = resource->bit_rate_hz,
        .chip_select_count = resource->chip_select_count,
    };
    return true;
}

BSP_SPI_Result BSP_SPI_Transfer(
    BSP_SPI_Handle spi,
    size_t chip_select_index,
    const uint8_t *tx,
    uint8_t *rx,
    size_t length,
    uint32_t timeout_ms)
{
    struct BSP_SPI_Resource *resource = spi_from_handle(spi);
    uint32_t remaining = wait_budget(timeout_ms);
    BSP_SPI_Result result = BSP_SPI_RESULT_OK;

    if ((resource == NULL) ||
        (chip_select_index >= resource->chip_select_count)) {
        return BSP_SPI_RESULT_INVALID_ARGUMENT;
    }
    if (length == 0U) {
        return BSP_SPI_RESULT_OK;
    }
    if (resource->busy) {
        return BSP_SPI_RESULT_BUSY;
    }

    resource->busy = true;
    while (DL_SPI_isBusy(resource->instance)) {
        if (!wait_step(&remaining)) {
            result = BSP_SPI_RESULT_TIMEOUT;
            goto finish;
        }
    }
    drain_receive_fifo(resource->instance);
    chip_select_set(resource, chip_select_index, false);

    for (size_t index = 0U; index < length; ++index) {
        uint8_t received;

        while (!DL_SPI_transmitDataCheck8(
                resource->instance, (tx != NULL) ? tx[index] : 0U)) {
            if (!wait_step(&remaining)) {
                result = BSP_SPI_RESULT_TIMEOUT;
                goto deselect;
            }
        }
        while (!DL_SPI_receiveDataCheck8(resource->instance, &received)) {
            if (!wait_step(&remaining)) {
                result = BSP_SPI_RESULT_TIMEOUT;
                goto deselect;
            }
        }
        if (rx != NULL) {
            rx[index] = received;
        }
    }
    while (DL_SPI_isBusy(resource->instance)) {
        if (!wait_step(&remaining)) {
            result = BSP_SPI_RESULT_TIMEOUT;
            break;
        }
    }

deselect:
    chip_select_set(resource, chip_select_index, true);
finish:
    drain_receive_fifo(resource->instance);
    resource->busy = false;
    return result;
}
