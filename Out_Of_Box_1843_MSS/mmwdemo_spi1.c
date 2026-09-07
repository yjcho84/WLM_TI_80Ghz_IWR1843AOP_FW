/**
 *   @file  mmwdemo_spi1.c
 *
 *   @brief
 *      SPI1 (MibSPI Channel A) slave-mode init and fixed command/response
 *      example for the mmw Demo running on IWR1843AOP.
 *
 *      Ball map used here (IWR1843AOP, ALP180A package, datasheet SWRS317
 *      Table 6-1 "Pin Attributes"):
 *          SPI1 CLK  -> D2   (SOC_XWR18XX_PINE13_PADAF, mode 1 = SPIA_CLK)
 *          SPI1 CS   -> C2   (SOC_XWR18XX_PINE15_PADAG, mode 1 = SPIA_CSN)
 *          SPI1 MOSI -> F2   (SOC_XWR18XX_PIND13_PADAD, mode 1 = SPIA_MOSI)
 *          SPI1 MISO -> D1   (SOC_XWR18XX_PINE14_PADAE, mode 1 = SPIA_MISO)
 *
 *      The SDK's pinmux_xwr18xx.h macro names embed the ISK-package ball
 *      numbers (E13/E15/D13/E14) because that header was written against the
 *      standard EVM package. On the AOP package the same internal pads are
 *      bonded out to D2/C2/F2/D1 instead - the GPIO number, mode value and
 *      full alternate-function list for each macro were cross-checked
 *      against the AOP datasheet and match exactly, so the macros are reused
 *      as-is here.
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2026 Texas Instruments, Inc.
 */

/* Standard Include Files. */
#include <string.h>
#include <stdint.h>

/* BIOS/XDC Include Files. */
#include <xdc/std.h>
#include <ti/sysbios/knl/Task.h>

/* mmWave SDK Include Files. */
#include <ti/drivers/pinmux/pinmux.h>
#include <ti/drivers/spi/SPI.h>
#include <ti/drivers/uart/UART.h>
#include <ti/utils/cli/cli.h>

/* Local Include Files. */
#include "mmwdemo_spi1.h"

#if (MMWDEMO_SPI1_MSG_LEN % MMWDEMO_SPI1_CHUNK_LEN) != 0
#error MMWDEMO_SPI1_MSG_LEN must be a multiple of MMWDEMO_SPI1_CHUNK_LEN
#endif

#if (MMWDEMO_SPI1_RESP_LEN % MMWDEMO_SPI1_CHUNK_LEN) != 0
#error MMWDEMO_SPI1_RESP_LEN must be a multiple of MMWDEMO_SPI1_CHUNK_LEN
#endif

/**************************************************************************
 *************************** Local Definitions ***************************
 **************************************************************************/

/*! @brief Fixed command pattern that the SPI master must send to trigger a response. */
static const uint8_t gMmwDemoSpi1ExpectedCmd[MMWDEMO_SPI1_MSG_LEN] =
{
    0xA5U,
    0x5AU,
    0x01U,
    0x02U
};

/*! @brief Response sent back once the expected command is recognized.
 *         Thermometer/unary-encoded by @ref MmwDemo_spi1SetResponseCode -
 *         see MMWDEMO_SPI1_RESP_LEN for the full rationale. Mutable (not
 *         const) since the code can change between command matches. */
static uint8_t gMmwDemoSpi1FixedResponse[MMWDEMO_SPI1_RESP_LEN];

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/*! @brief SPI1 (SPIA) driver handle, opened by @ref MmwDemo_spi1Init */
static SPI_Handle   gMmwDemoSpi1Handle = NULL;

/**
 *  @b Description
 *  @n
 *      Configures the pinmux for SPI1 (MibSPI Channel A) on the AOP balls
 *      D2(CLK)/C2(CS)/F2(MOSI)/D1(MISO).
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_spi1PinmuxConfig(void)
{
    /* SPI1 CLK -> ball D2 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINE13_PADAF, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINE13_PADAF, SOC_XWR18XX_PINE13_PADAF_SPIA_CLK);

    /* SPI1 CS -> ball C2 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINE15_PADAG, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINE15_PADAG, SOC_XWR18XX_PINE15_PADAG_SPIA_CSN);

    /* SPI1 MOSI -> ball F2 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PIND13_PADAD, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PIND13_PADAD, SOC_XWR18XX_PIND13_PADAD_SPIA_MOSI);

    /* SPI1 MISO -> ball D1 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINE14_PADAE, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINE14_PADAE, SOC_XWR18XX_PINE14_PADAE_SPIA_MISO);
}

/**
 *  @b Description
 *  @n
 *      Configures SPI1 pinmux and opens the SPI driver (SPIA, index 0) in
 *      SLAVE mode, 4-pin (dedicated CS), mode 0, MSB first, 8-bit elements.
 *
 *      DMA is intentionally NOT used for this transfer (dmaEnable = 0): the
 *      DMA-enabled path routes every transfer completion through the SDK's
 *      MIBSPI_sysDmaIntHandler() (ti/drivers/spi/src/mibspi_dma.c), which
 *      calls DebugP_assert(0) - i.e. System_abort(), halting the whole R4F,
 *      not just this task - on any DMA channel mismatch or unexpected
 *      interrupt type (HBC/LFS). Those conditions are realistically
 *      triggered by an external SPI master that doesn't toggle CS on the
 *      exact 16-bit boundary the peripheral-mode protocol requires (see
 *      MMWDEMO_SPI1_CHUNK_LEN), so a real/imperfect master could crash the
 *      whole demo, not just this feature. The plain interrupt-driven path
 *      (MIBSPI_ISR) does not have this failure mode, so it is used instead.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
int32_t MmwDemo_spi1Init(void)
{
    SPI_Params    spiParams;

    /* Bring SPI1 (SPIA) pins out of GPIO mode into their SPI alternate function */
    MmwDemo_spi1PinmuxConfig();

    /* Initialize the SPI driver module (must be called once before SPI_open) */
    SPI_init();

    SPI_Params_init(&spiParams);
    spiParams.mode                      = SPI_SLAVE;
    /* CPHA1 experiment reverted: made things strictly worse on the nRF side
     * (100% idle FF FF FF FF instead of previously-corrupted-but-real-looking
     * response bytes). Back to CPHA0. */
    spiParams.frameFormat               = SPI_POL0_PHA0;
    spiParams.pinMode                   = SPI_PINMODE_4PIN_CS;
    spiParams.shiftFormat               = SPI_MSB_FIRST;
    spiParams.dataSize                  = 8U;
    spiParams.dmaEnable                 = 0U;
    spiParams.u.slaveParams.chipSelect  = 0U;

    /* Index 0 = MibSPI Channel A (SPI1) */
    gMmwDemoSpi1Handle = SPI_open(0U, &spiParams);
    if (gMmwDemoSpi1Handle == NULL)
    {
        CLI_write("Error: MmwDemo_spi1Init - SPI_open (SPIA, SLAVE) failed\n");
        return -1;
    }

    CLI_write("Debug: SPI1 (SPIA) opened in SLAVE mode on D2(CLK)/C2(CS)/F2(MOSI)/D1(MISO)\n");

    /* Default response code until application code calls
     * MmwDemo_spi1SetResponseCode() with something else - 1 (i.e. a single
     * leading 0xFF byte) reproduces the plain ACK behavior this was before
     * multi-code support was added. */
    MmwDemo_spi1SetResponseCode(1U);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      Thermometer/unary-encodes `code` into gMmwDemoSpi1FixedResponse: the
 *      first `code` bytes become 0xFF, the rest 0x00. See
 *      MMWDEMO_SPI1_RESP_LEN for why this encoding is used instead of an
 *      arbitrary bit pattern.
 *
 *  @param[in] code     Response code to send next, in [0, MMWDEMO_SPI1_RESP_LEN];
 *                       clamped if out of range.
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_spi1SetResponseCode(uint8_t code)
{
    uint32_t i;

    if (code > MMWDEMO_SPI1_RESP_LEN)
    {
        code = MMWDEMO_SPI1_RESP_LEN;
    }

    for (i = 0U; i < MMWDEMO_SPI1_RESP_LEN; i++)
    {
        gMmwDemoSpi1FixedResponse[i] = (i < code) ? 0xFFU : 0x00U;
    }
}

/**
 *  @b Description
 *  @n
 *      Transfers a buffer of totalLen bytes in MMWDEMO_SPI1_CHUNK_LEN-byte
 *      chunks, issuing one SPI_transfer() (= one CS assertion) per chunk.
 *      This follows the IWR1843AOP datasheet requirement (SWRS317, section
 *      7.10.5.4) that CS be toggled every 16 bits when the device is the SPI
 *      slave, instead of moving the whole buffer under a single CS assertion.
 *
 *  @param[in]  handle      SPI1 handle, already opened in SLAVE mode
 *  @param[in]  txBuf       Buffer to shift out on MISO (may be a dummy/idle
 *                          pattern if only the received data matters)
 *  @param[out] rxBuf       Buffer to receive into
 *  @param[in]  totalLen    Total number of bytes to move; must be a multiple
 *                          of MMWDEMO_SPI1_CHUNK_LEN
 *
 *  @retval
 *      true    - all chunks transferred successfully
 *  @retval
 *      false   - a chunk transfer failed (status logged internally)
 */
static bool MmwDemo_spi1TransferChunks(SPI_Handle handle, const uint8_t *txBuf, uint8_t *rxBuf, uint32_t totalLen)
{
    /* Zero-initialized so slaveIndex (unused in SPI_SLAVE mode - the actual
     * transfer always targets MIBSPI_SLAVEMODE_TRANS_GROUP - but still
     * range-checked by the driver against MIBSPI_SLAVE_MAX) is a defined 0
     * instead of whatever garbage was left on the stack. Leaving it
     * uninitialized caused intermittent SPI_TRANSFER_INVAL (status=6)
     * failures whenever the leftover stack value happened to be >= 3. */
    SPI_Transaction  spiTransaction = { 0 };
    uint32_t         offset;

    for (offset = 0U; offset < totalLen; offset += MMWDEMO_SPI1_CHUNK_LEN)
    {
        spiTransaction.count = MMWDEMO_SPI1_CHUNK_LEN;
        spiTransaction.txBuf = (void *)&txBuf[offset];
        spiTransaction.rxBuf = (void *)&rxBuf[offset];

        if (SPI_transfer(handle, &spiTransaction) != true)
        {
            CLI_write("Error: MmwDemo_spi1TransferChunks - SPI_transfer failed at offset %d, status=%d\n",
                      (int32_t)offset, (int32_t)spiTransaction.status);
            return false;
        }
    }

    return true;
}

/**
 *  @b Description
 *  @n
 *      Example SYSBIOS task body: continuously scans, one byte at a time,
 *      for the fixed command pattern from the SPI master and replies with
 *      the fixed response.
 *
 *      Phase 1 used to capture a fixed MMWDEMO_SPI1_MSG_LEN-byte window and
 *      memcmp() it as a whole, but that window can end up permanently
 *      phase-shifted against the master's byte stream (there is no
 *      CS-independent handshake to establish byte-0 alignment at boot), and
 *      once shifted it can never self-correct because it always advances by
 *      exactly MMWDEMO_SPI1_MSG_LEN bytes - so it keeps re-sampling the same
 *      wrong slice of the master's repeating command+dummy stream forever.
 *      Scanning byte by byte and re-synchronizing on any mismatch (like the
 *      nRF side's UART1 magic-word parser) is robust to any phase offset.
 *      This requires MMWDEMO_SPI1_CHUNK_LEN == 1 (CS toggling every byte) so
 *      that each SPI_transfer() below corresponds to exactly one byte.
 *
 *        1) receive phase  - slave shifts out a dummy/idle pattern on MISO
 *                             while it captures whatever the master sends,
 *                             1 byte per CS assertion, until MMWDEMO_SPI1_MSG_LEN
 *                             consecutive bytes match the expected command.
 *        2) response phase - once the full command matched, slave shifts
 *                             out the fixed response on MISO, again 1 byte
 *                             per CS assertion (via MmwDemo_spi1TransferChunks).
 *
 *      NOTE: The SPI protocol/driver has no built-in handshake. The slave is
 *      only ready to be clocked while it is blocked inside SPI_transfer();
 *      the master must not start a transaction before that (e.g. via a fixed
 *      startup delay, or a GPIO ready signal such as SPI_HOST_INTR).
 *
 *  @param[in] arg0     Unused
 *  @param[in] arg1     Unused
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_spi1SlaveTask(UArg arg0, UArg arg1)
{
    uint8_t          rxByte;
    uint8_t          rxBuf[MMWDEMO_SPI1_MSG_LEN];
    uint8_t          respRxBuf[MMWDEMO_SPI1_RESP_LEN];
    uint8_t          matchLen = 0U;
    /* Was 0xFFU. Diagnostic showed every corrupted MISO bit is a falling
     * (1->0) transition that occasionally samples as a stale 1 - idling low
     * instead of high removes the 1->0 step at the boundary into the
     * response phase's first bit. See gMmwDemoSpi1FixedResponse. */
    const uint8_t    txIdleByte = 0x00U;
    SPI_Transaction  spiTransaction = { 0 };

    if (gMmwDemoSpi1Handle == NULL)
    {
        CLI_write("Error: MmwDemo_spi1SlaveTask - SPI1 not initialized, call MmwDemo_spi1Init() first\n");
        return;
    }

    while (1)
    {
        /**********************************************************************
         * Phase 1: pull in one byte and test it against the next expected
         * byte of the command pattern, regardless of where in the master's
         * stream it happens to land.
         **********************************************************************/
        spiTransaction.count = 1U;
        spiTransaction.txBuf = (void *)&txIdleByte;
        spiTransaction.rxBuf = (void *)&rxByte;

        if (SPI_transfer(gMmwDemoSpi1Handle, &spiTransaction) != true)
        {
            CLI_write("Error: MmwDemo_spi1SlaveTask - SPI_transfer failed, status=%d\n",
                      (int32_t)spiTransaction.status);
            matchLen = 0U;
            continue;
        }

        if (rxByte == gMmwDemoSpi1ExpectedCmd[matchLen])
        {
            rxBuf[matchLen] = rxByte;
            matchLen++;

            if (matchLen == MMWDEMO_SPI1_MSG_LEN)
            {
                /**********************************************************************
                 * Phase 2: full command matched - send the fixed response,
                 * 1 byte per CS
                 **********************************************************************/
                (void)MmwDemo_spi1TransferChunks(gMmwDemoSpi1Handle, gMmwDemoSpi1FixedResponse, respRxBuf, MMWDEMO_SPI1_RESP_LEN);
                matchLen = 0U;
            }
        }
        else
        {
            if (matchLen > 0U)
            {
                /* Only log when a partial match breaks - logging every byte
                 * that isn't byte 0 of the command would flood the console
                 * with the idle/dummy traffic between commands. */
                CLI_write("Warn: SPI1 cmd resync (had %d/%d bytes, next byte was %02x)\n",
                          (int32_t)matchLen, (int32_t)MMWDEMO_SPI1_MSG_LEN, rxByte);
            }

            /* This byte may itself be byte 0 of a new command - re-test it
             * against the start of the pattern instead of discarding it. */
            matchLen = (rxByte == gMmwDemoSpi1ExpectedCmd[0]) ? 1U : 0U;
            if (matchLen == 1U)
            {
                rxBuf[0] = rxByte;
            }
        }

        /* No per-byte CLI_write here anymore. It used to unconditionally
         * echo every received MOSI byte, but CLI_write is a blocking/polled
         * UART transmit - slow enough that with a multi-byte command sent
         * back-to-back with no gap (MMWDEMO_SPI1_MSG_LEN=4), the slave was
         * still stuck inside CLI_write when the master's next CS pulse
         * arrived, missing that byte's clock edges entirely. Confirmed
         * 2026-08-25: with the echo in place, bytes 1 and 3 of every 4-byte
         * command (0xA5 0x5A 0x01 0x02) were silently dropped every single
         * time - the slave only ever captured 0xA5 then 0x01 (byte 2) before
         * resyncing. Removing it keeps Phase 1 fast enough to keep pace with
         * back-to-back command bytes. */
    }
}
