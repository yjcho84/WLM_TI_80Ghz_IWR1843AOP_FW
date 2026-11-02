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
#include <stdlib.h>

/* BIOS/XDC Include Files. */
#include <xdc/std.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Semaphore.h>

/* mmWave SDK Include Files. */
#include <ti/drivers/pinmux/pinmux.h>
#include <ti/drivers/spi/SPI.h>
#include <ti/drivers/uart/UART.h>
#include <ti/utils/cli/cli.h>

/* Local Include Files. */
#include "mmwdemo_spi1.h"

#if (MMWDEMO_SPI1_RESP_LEN % MMWDEMO_SPI1_CHUNK_LEN) != 0
#error MMWDEMO_SPI1_RESP_LEN must be a multiple of MMWDEMO_SPI1_CHUNK_LEN
#endif

/**************************************************************************
 *************************** Local Definitions ***************************
 **************************************************************************/

/*! @brief ASCII commands from the SPI master. All share the "spiM" prefix and
 *         are told apart by byte 4. '#' in a pattern matches any ASCII digit.
 *          - "spiMca<E>b<T.TT>", e.g. "spiMca1b1.00": sets
 *            "multiObjBeamForming -1 <E> <T.TT>" at run time (E: '0'/'1').
 *          - "spiMaa<PPPP>", e.g. "spiMaa0300": frameCfg periodicity in ms
 *            (100 ~ 1000, always 4 digits), power-up only.
 *          - "spiMba<M>", e.g. "spiMba0": lowPower adcMode (0/1), power-up only.
 *          Run-time updates (applied at the next frame, like spiMca):
 *          - "spiMda<E>": "clutterRemoval -1 <E>"
 *          - "spiMea<E>": "calibDcRangeSig -1 <E> -5 8 256"
 *          - "spiMfa<E>": "extendedMaxVelocity -1 <E>"
 *          - "spiMga<AA>b<BB>c<CC>d<DD>", e.g. "spiMga45b45c30d30":
 *            "aoaFovCfg -1 -<AA> <BB> -<CC> <DD>" (degrees, 2 digits each,
 *            no '-' sign on the minimums)
 *          - "spiMha<RR.RR>", e.g. "spiMha01.25": "cfarFovCfg -1 0 0 <RR.RR>"
 *            (max range 01.00 ~ 20.00 m, always 5 chars)
 *          - "spiMia<A.A>b<B.B>", e.g. "spiMia1.0b2.5": "cfarFovCfg -1 1 -<A.A> <B.B>"
 *            (velocity 1.0 ~ 2.5 m/s, no '-' sign on the minimum) */
#define MMWDEMO_SPI1_CMD_PREFIX_LEN     4U
#define MMWDEMO_SPI1_CMD_MAX_LEN        17U

#define MMWDEMO_SPI1_CMD_BF             0
#define MMWDEMO_SPI1_CMD_FRAMECFG       1
#define MMWDEMO_SPI1_CMD_LOWPOWER       2
#define MMWDEMO_SPI1_CMD_CLUTTER        3
#define MMWDEMO_SPI1_CMD_CALIBDC        4
#define MMWDEMO_SPI1_CMD_EXTMAXVEL      5
#define MMWDEMO_SPI1_CMD_AOAFOV         6
#define MMWDEMO_SPI1_CMD_FOVRANGE       7
#define MMWDEMO_SPI1_CMD_FOVDOPPLER     8
#define MMWDEMO_SPI1_NUM_CMDS           9U
static const char* const gMmwDemoSpi1CmdPatterns[MMWDEMO_SPI1_NUM_CMDS] =
{
    "spiMca#b#.##",         /* MMWDEMO_SPI1_CMD_BF */
    "spiMaa####",           /* MMWDEMO_SPI1_CMD_FRAMECFG */
    "spiMba#",              /* MMWDEMO_SPI1_CMD_LOWPOWER */
    "spiMda#",              /* MMWDEMO_SPI1_CMD_CLUTTER */
    "spiMea#",              /* MMWDEMO_SPI1_CMD_CALIBDC */
    "spiMfa#",              /* MMWDEMO_SPI1_CMD_EXTMAXVEL */
    "spiMga##b##c##d##",    /* MMWDEMO_SPI1_CMD_AOAFOV */
    "spiMha##.##",          /* MMWDEMO_SPI1_CMD_FOVRANGE */
    "spiMia#.#b#.#"         /* MMWDEMO_SPI1_CMD_FOVDOPPLER */
};

#define MMWDEMO_SPI1_BF_ENABLED_IDX     6U
#define MMWDEMO_SPI1_BF_THR_IDX         8U
#define MMWDEMO_SPI1_BF_THR_LEN         4U
#define MMWDEMO_SPI1_FRAMECFG_VAL_IDX   6U
#define MMWDEMO_SPI1_FRAMECFG_VAL_LEN   4U
#define MMWDEMO_SPI1_LOWPOWER_VAL_IDX   6U
#define MMWDEMO_SPI1_ONOFF_VAL_IDX      6U      /* spiMda / spiMea / spiMfa */
#define MMWDEMO_SPI1_AOAFOV_MINAZ_IDX   6U      /* spiMga<AA>b<BB>c<CC>d<DD> */
#define MMWDEMO_SPI1_AOAFOV_MAXAZ_IDX   9U
#define MMWDEMO_SPI1_AOAFOV_MINEL_IDX   12U
#define MMWDEMO_SPI1_AOAFOV_MAXEL_IDX   15U
#define MMWDEMO_SPI1_FOVRANGE_IDX       6U      /* spiMha<RR.RR> */
#define MMWDEMO_SPI1_FOVDOP_MIN_IDX     6U      /* spiMia<A.A>b<B.B> */
#define MMWDEMO_SPI1_FOVDOP_MAX_IDX     10U

/* Value of the 2 ASCII digits at p, e.g. "45" -> 45 (matcher guarantees digits). */
#define MMWDEMO_SPI1_DIGITS2(p)         ((uint32_t)(((p)[0] - '0') * 10 + ((p)[1] - '0')))

/*! @brief State of the byte-by-byte command matcher. */
typedef struct MmwDemo_Spi1CmdMatcher_t
{
    uint8_t     matchLen;                       /* pattern bytes matched so far */
    int32_t     cmdIdx;                         /* pattern selected by byte 4, -1 if none */
    char        rxBuf[MMWDEMO_SPI1_CMD_MAX_LEN];/* captured command bytes */
} MmwDemo_Spi1CmdMatcher;

/*! @brief Responses to the "spiM.." commands: thermometer code 3 when the
 *         new values were accepted, code 0 when they were rejected. */
static const uint8_t gMmwDemoSpi1OkResponse[MMWDEMO_SPI1_RESP_LEN] =
{
    0xFFU,
    0xFFU,
    0xFFU,
    0x00U
};
static const uint8_t gMmwDemoSpi1ErrResponse[MMWDEMO_SPI1_RESP_LEN] =
{
    0x00U,
    0x00U,
    0x00U,
    0x00U
};

/* Defined in mmw_cli.c */
extern int32_t MmwDemo_CLIUpdateMultiObjBeamForming (char* enabledStr, char* thrScaleStr);
extern int32_t MmwDemo_CLIUpdateFramePeriodicity (uint32_t periodMs);
extern int32_t MmwDemo_CLIUpdateLowPower (uint32_t adcMode);
extern int32_t MmwDemo_CLIUpdateClutterRemoval (uint32_t enabled);
extern int32_t MmwDemo_CLIUpdateCalibDcRangeSig (uint32_t enabled);
extern int32_t MmwDemo_CLIUpdateExtMaxVelocity (uint32_t enabled);
extern int32_t MmwDemo_CLIUpdateAoaFov (uint32_t minAzimMag, uint32_t maxAzim,
                                        uint32_t minElevMag, uint32_t maxElev);
extern int32_t MmwDemo_CLIUpdateCfarFovRange (uint32_t maxX100);
extern int32_t MmwDemo_CLIUpdateCfarFovDoppler (uint32_t minMagX10, uint32_t maxX10);

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/*! @brief SPI1 (SPIA) driver handle, opened by @ref MmwDemo_spi1Init */
static SPI_Handle   gMmwDemoSpi1Handle = NULL;

/*! @brief Posted once both power-up settings (frameCfg, lowPower) arrived,
 *         see @ref MmwDemo_spi1WaitBootCfg. Created by @ref MmwDemo_spi1Init */
static Semaphore_Handle gMmwDemoSpi1BootCfgSem = NULL;
static bool         gMmwDemoSpi1GotFrameCfg = false;
static bool         gMmwDemoSpi1GotLowPower = false;
static bool         gMmwDemoSpi1BootCfgPosted = false;

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

    gMmwDemoSpi1BootCfgSem = Semaphore_create(0, NULL, NULL);
    if (gMmwDemoSpi1BootCfgSem == NULL)
    {
        CLI_write("Error: MmwDemo_spi1Init - boot config semaphore create failed\n");
        return -1;
    }

    return 0;
}

bool MmwDemo_spi1WaitBootCfg(uint32_t timeoutTicks)
{
    if (gMmwDemoSpi1BootCfgSem == NULL)
    {
        return false;
    }

    return (Semaphore_pend(gMmwDemoSpi1BootCfgSem, timeoutTicks) == TRUE);
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
 *      Advances the byte-by-byte matcher for gMmwDemoSpi1CmdPatterns[] by one
 *      received byte: the shared "spiM" prefix is matched first, byte 4 then
 *      selects the pattern. '#' matches any digit, and every matched byte is
 *      captured into rxBuf so the values can be extracted. On a mismatch the
 *      byte is re-tested against byte 0 instead of being discarded, so the
 *      matcher re-synchronizes on any phase offset.
 *
 *      A single matcher is used for all commands on purpose: parallel
 *      matchers would all follow the shared prefix, and the losers' resync
 *      CLI_write would stall this task long enough to drop the rest of the
 *      winning command.
 *
 *  @param[in,out] matcher   Matcher state
 *  @param[in]     rxByte    Byte just received from the master
 *
 *  @retval
 *      >=0     - MMWDEMO_SPI1_CMD_xxx just fully matched (state reset)
 *  @retval
 *      -1      - no command (yet) complete
 */
static int32_t MmwDemo_spi1CmdMatchStep(MmwDemo_Spi1CmdMatcher *matcher, uint8_t rxByte)
{
    const char* pattern = NULL;
    bool        isMatch = false;
    uint32_t    i;
    char        expected;

    if (matcher->matchLen < MMWDEMO_SPI1_CMD_PREFIX_LEN)
    {
        pattern = gMmwDemoSpi1CmdPatterns[0];
    }
    else
    {
        if (matcher->matchLen == MMWDEMO_SPI1_CMD_PREFIX_LEN)
        {
            matcher->cmdIdx = -1;
            for (i = 0U; i < MMWDEMO_SPI1_NUM_CMDS; i++)
            {
                if (rxByte == (uint8_t)gMmwDemoSpi1CmdPatterns[i][MMWDEMO_SPI1_CMD_PREFIX_LEN])
                {
                    matcher->cmdIdx = (int32_t)i;
                    break;
                }
            }
        }
        if (matcher->cmdIdx >= 0)
        {
            pattern = gMmwDemoSpi1CmdPatterns[matcher->cmdIdx];
        }
    }

    if (pattern != NULL)
    {
        expected = pattern[matcher->matchLen];
        isMatch  = (expected == '#') ? ((rxByte >= (uint8_t)'0') && (rxByte <= (uint8_t)'9'))
                                     : (rxByte == (uint8_t)expected);
    }

    if (isMatch)
    {
        matcher->rxBuf[matcher->matchLen] = (char)rxByte;
        matcher->matchLen++;
        if (pattern[matcher->matchLen] == '\0')
        {
            matcher->matchLen = 0U;
            return matcher->cmdIdx;
        }
        return -1;
    }

    if (matcher->matchLen > 0U)
    {
        CLI_write("Warn: SPI1 cmd spiM.. resync (had %d bytes, next byte was %02x)\n",
                  (int32_t)matcher->matchLen, rxByte);
    }

    matcher->matchLen = (rxByte == (uint8_t)gMmwDemoSpi1CmdPatterns[0][0]) ? 1U : 0U;
    if (matcher->matchLen == 1U)
    {
        matcher->rxBuf[0] = (char)rxByte;
    }
    return -1;
}

/**
 *  @b Description
 *  @n
 *      Posts gMmwDemoSpi1BootCfgSem once both power-up settings arrived.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_spi1CheckBootCfgDone(void)
{
    if (gMmwDemoSpi1GotFrameCfg && gMmwDemoSpi1GotLowPower && !gMmwDemoSpi1BootCfgPosted)
    {
        gMmwDemoSpi1BootCfgPosted = true;
        Semaphore_post(gMmwDemoSpi1BootCfgSem);
    }
}

/**
 *  @b Description
 *  @n
 *      Applies a fully matched "spiMca<E>b<T.TT>" command as
 *      "multiObjBeamForming -1 <E> <T.TT>" and replies with
 *      gMmwDemoSpi1OkResponse / gMmwDemoSpi1ErrResponse. The response is
 *      sent before anything is logged, since CLI_write is slow enough for
 *      the master's response clocks to arrive before it returns.
 *
 *  @param[in] rxBuf      The captured command
 *  @param[in] respRxBuf  Scratch buffer for the MISO response transfer
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_spi1HandleBfCmd(const char *rxBuf, uint8_t *respRxBuf)
{
    char    enabledStr[2];
    char    thrScaleStr[MMWDEMO_SPI1_BF_THR_LEN + 1U];
    int32_t status = -1;

    enabledStr[0] = rxBuf[MMWDEMO_SPI1_BF_ENABLED_IDX];
    enabledStr[1] = '\0';
    memcpy(thrScaleStr, &rxBuf[MMWDEMO_SPI1_BF_THR_IDX], MMWDEMO_SPI1_BF_THR_LEN);
    thrScaleStr[MMWDEMO_SPI1_BF_THR_LEN] = '\0';

    if ((enabledStr[0] == '0') || (enabledStr[0] == '1'))
    {
        status = MmwDemo_CLIUpdateMultiObjBeamForming(enabledStr, thrScaleStr);
    }

    (void)MmwDemo_spi1TransferChunks(gMmwDemoSpi1Handle,
                                     (status == 0) ? gMmwDemoSpi1OkResponse : gMmwDemoSpi1ErrResponse,
                                     respRxBuf, MMWDEMO_SPI1_RESP_LEN);

    if (status == 0)
    {
        CLI_write("SPI1: multiObjBeamForming -1 %s %s (applied at next frame)\n",
                  enabledStr, thrScaleStr);
    }
    else
    {
        CLI_write("Error: SPI1 multiObjBeamForming update rejected (enabled=%s thrScale=%s)\n",
                  enabledStr, thrScaleStr);
    }
}

/**
 *  @b Description
 *  @n
 *      Handles a fully matched "spiMaa<PPPP>" (frameCfg periodicity) or
 *      "spiMba<M>" (lowPower adcMode) command. Both only take effect before
 *      the power-up auto configuration runs; a valid value received later is
 *      still acknowledged (so the nRF stops retrying) but ignored. Response
 *      first, logging after - see @ref MmwDemo_spi1HandleBfCmd.
 *
 *  @param[in] cmdIdx     MMWDEMO_SPI1_CMD_FRAMECFG or MMWDEMO_SPI1_CMD_LOWPOWER
 *  @param[in] rxBuf      The captured command
 *  @param[in] respRxBuf  Scratch buffer for the MISO response transfer
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_spi1HandleBootCfgCmd(int32_t cmdIdx, const char *rxBuf, uint8_t *respRxBuf)
{
    char        valStr[MMWDEMO_SPI1_FRAMECFG_VAL_LEN + 1U];
    const char* name;
    int32_t     status;

    if (cmdIdx == MMWDEMO_SPI1_CMD_FRAMECFG)
    {
        name = "frameCfg periodicity";
        memcpy(valStr, &rxBuf[MMWDEMO_SPI1_FRAMECFG_VAL_IDX], MMWDEMO_SPI1_FRAMECFG_VAL_LEN);
        valStr[MMWDEMO_SPI1_FRAMECFG_VAL_LEN] = '\0';
        status = MmwDemo_CLIUpdateFramePeriodicity((uint32_t)atoi(valStr));
    }
    else
    {
        name = "lowPower adcMode";
        valStr[0] = rxBuf[MMWDEMO_SPI1_LOWPOWER_VAL_IDX];
        valStr[1] = '\0';
        status = MmwDemo_CLIUpdateLowPower((uint32_t)atoi(valStr));
    }

    (void)MmwDemo_spi1TransferChunks(gMmwDemoSpi1Handle,
                                     (status >= 0) ? gMmwDemoSpi1OkResponse : gMmwDemoSpi1ErrResponse,
                                     respRxBuf, MMWDEMO_SPI1_RESP_LEN);

    if (status == 0)
    {
        if (cmdIdx == MMWDEMO_SPI1_CMD_FRAMECFG)
        {
            gMmwDemoSpi1GotFrameCfg = true;
        }
        else
        {
            gMmwDemoSpi1GotLowPower = true;
        }
        MmwDemo_spi1CheckBootCfgDone();
        CLI_write("SPI1: %s %s (applied at sensor start)\n", name, valStr);
    }
    else if (status > 0)
    {
        CLI_write("SPI1: %s %s ignored, sensor already configured\n", name, valStr);
    }
    else
    {
        CLI_write("Error: SPI1 %s %s rejected\n", name, valStr);
    }
}

/**
 *  @b Description
 *  @n
 *      Handles a fully matched run-time command, "spiMda#" ~ "spiMia#.#b#.#"
 *      (see gMmwDemoSpi1CmdPatterns): the values are applied at the next
 *      frame, like @ref MmwDemo_spi1HandleBfCmd. Response first, logging
 *      after.
 *
 *  @param[in] cmdIdx     MMWDEMO_SPI1_CMD_CLUTTER ~ MMWDEMO_SPI1_CMD_FOVDOPPLER
 *  @param[in] rxBuf      The captured command
 *  @param[in] respRxBuf  Scratch buffer for the MISO response transfer
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_spi1HandleDynCmd(int32_t cmdIdx, const char *rxBuf, uint8_t *respRxBuf)
{
    uint32_t    onOff = (uint32_t)(rxBuf[MMWDEMO_SPI1_ONOFF_VAL_IDX] - '0');
    uint32_t    v0 = 0U;
    uint32_t    v1 = 0U;
    uint32_t    v2 = 0U;
    uint32_t    v3 = 0U;
    int32_t     status;

    switch (cmdIdx)
    {
        case MMWDEMO_SPI1_CMD_CLUTTER:
            status = MmwDemo_CLIUpdateClutterRemoval(onOff);
            break;
        case MMWDEMO_SPI1_CMD_CALIBDC:
            status = MmwDemo_CLIUpdateCalibDcRangeSig(onOff);
            break;
        case MMWDEMO_SPI1_CMD_EXTMAXVEL:
            status = MmwDemo_CLIUpdateExtMaxVelocity(onOff);
            break;
        case MMWDEMO_SPI1_CMD_AOAFOV:
            v0 = MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_AOAFOV_MINAZ_IDX]);
            v1 = MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_AOAFOV_MAXAZ_IDX]);
            v2 = MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_AOAFOV_MINEL_IDX]);
            v3 = MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_AOAFOV_MAXEL_IDX]);
            status = MmwDemo_CLIUpdateAoaFov(v0, v1, v2, v3);
            break;
        case MMWDEMO_SPI1_CMD_FOVRANGE:
            /* "RR.RR" -> RRRR (meters * 100) */
            v0 = MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_FOVRANGE_IDX]) * 100U +
                 MMWDEMO_SPI1_DIGITS2(&rxBuf[MMWDEMO_SPI1_FOVRANGE_IDX + 3U]);
            status = MmwDemo_CLIUpdateCfarFovRange(v0);
            break;
        case MMWDEMO_SPI1_CMD_FOVDOPPLER:
            /* "A.A" -> AA (m/s * 10) */
            v0 = (uint32_t)(rxBuf[MMWDEMO_SPI1_FOVDOP_MIN_IDX] - '0') * 10U +
                 (uint32_t)(rxBuf[MMWDEMO_SPI1_FOVDOP_MIN_IDX + 2U] - '0');
            v1 = (uint32_t)(rxBuf[MMWDEMO_SPI1_FOVDOP_MAX_IDX] - '0') * 10U +
                 (uint32_t)(rxBuf[MMWDEMO_SPI1_FOVDOP_MAX_IDX + 2U] - '0');
            status = MmwDemo_CLIUpdateCfarFovDoppler(v0, v1);
            break;
        default:
            status = -1;
            break;
    }

    (void)MmwDemo_spi1TransferChunks(gMmwDemoSpi1Handle,
                                     (status == 0) ? gMmwDemoSpi1OkResponse : gMmwDemoSpi1ErrResponse,
                                     respRxBuf, MMWDEMO_SPI1_RESP_LEN);

    if (status != 0)
    {
        char    cmdStr[MMWDEMO_SPI1_CMD_MAX_LEN + 1U];
        size_t  cmdLen = strlen(gMmwDemoSpi1CmdPatterns[cmdIdx]);

        memcpy(cmdStr, rxBuf, cmdLen);
        cmdStr[cmdLen] = '\0';
        CLI_write("Error: SPI1 %s rejected\n", cmdStr);
        return;
    }

    switch (cmdIdx)
    {
        case MMWDEMO_SPI1_CMD_CLUTTER:
            CLI_write("SPI1: clutterRemoval -1 %d (applied at next frame)\n", (int32_t)onOff);
            break;
        case MMWDEMO_SPI1_CMD_CALIBDC:
            CLI_write("SPI1: calibDcRangeSig -1 %d -5 8 256 (applied at next frame)\n", (int32_t)onOff);
            break;
        case MMWDEMO_SPI1_CMD_EXTMAXVEL:
            CLI_write("SPI1: extendedMaxVelocity -1 %d (applied at next frame)\n", (int32_t)onOff);
            break;
        case MMWDEMO_SPI1_CMD_AOAFOV:
            CLI_write("SPI1: aoaFovCfg -1 -%d %d -%d %d (applied at next frame)\n",
                      (int32_t)v0, (int32_t)v1, (int32_t)v2, (int32_t)v3);
            break;
        case MMWDEMO_SPI1_CMD_FOVRANGE:
            CLI_write("SPI1: cfarFovCfg -1 0 0 %d.%02d (applied at next frame)\n",
                      (int32_t)(v0 / 100U), (int32_t)(v0 % 100U));
            break;
        default: /* MMWDEMO_SPI1_CMD_FOVDOPPLER */
            CLI_write("SPI1: cfarFovCfg -1 1 -%d.%d %d.%d (applied at next frame)\n",
                      (int32_t)(v0 / 10U), (int32_t)(v0 % 10U),
                      (int32_t)(v1 / 10U), (int32_t)(v1 % 10U));
            break;
    }
}

/**
 *  @b Description
 *  @n
 *      Example SYSBIOS task body: continuously scans, one byte at a time,
 *      for the "spiM.." commands (see gMmwDemoSpi1CmdPatterns) from the SPI
 *      master and replies with their response.
 *
 *      Scanning byte by byte and re-synchronizing on any mismatch (like the
 *      nRF side's UART1 magic-word parser) is robust to any phase offset
 *      (there is no CS-independent handshake to establish byte-0 alignment
 *      at boot). This requires MMWDEMO_SPI1_CHUNK_LEN == 1 (CS toggling
 *      every byte) so that each SPI_transfer() below corresponds to exactly
 *      one byte.
 *
 *        1) receive phase  - slave shifts out a dummy/idle pattern on MISO
 *                             while it captures whatever the master sends,
 *                             1 byte per CS assertion, until the full
 *                             command matched.
 *        2) response phase - once the full command matched, slave shifts
 *                             out the response on MISO, again 1 byte
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
    uint8_t          respRxBuf[MMWDEMO_SPI1_RESP_LEN];
    MmwDemo_Spi1CmdMatcher matcher = { 0 };
    int32_t          cmdIdx;
    /* Was 0xFFU. Diagnostic showed every corrupted MISO bit is a falling
     * (1->0) transition that occasionally samples as a stale 1 - idling low
     * instead of high removes the 1->0 step at the boundary into the
     * response phase's first bit. See MMWDEMO_SPI1_RESP_LEN. */
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
            matcher.matchLen = 0U;
            continue;
        }

        /**********************************************************************
         * Phase 2: a full command matched - send its response, 1 byte per CS.
         **********************************************************************/
        cmdIdx = MmwDemo_spi1CmdMatchStep(&matcher, rxByte);
        if (cmdIdx == MMWDEMO_SPI1_CMD_BF)
        {
            MmwDemo_spi1HandleBfCmd(matcher.rxBuf, respRxBuf);
        }
        else if ((cmdIdx == MMWDEMO_SPI1_CMD_FRAMECFG) || (cmdIdx == MMWDEMO_SPI1_CMD_LOWPOWER))
        {
            MmwDemo_spi1HandleBootCfgCmd(cmdIdx, matcher.rxBuf, respRxBuf);
        }
        else if (cmdIdx >= MMWDEMO_SPI1_CMD_CLUTTER)
        {
            MmwDemo_spi1HandleDynCmd(cmdIdx, matcher.rxBuf, respRxBuf);
        }

        /* No per-byte CLI_write here. CLI_write is a blocking/polled UART
         * transmit - slow enough that with a multi-byte command sent
         * back-to-back with no gap, the slave is still stuck inside
         * CLI_write when the master's next CS pulse arrives, missing that
         * byte's clock edges entirely (confirmed 2026-08-25: every other
         * command byte was dropped with a per-byte echo in place). */
    }
}
