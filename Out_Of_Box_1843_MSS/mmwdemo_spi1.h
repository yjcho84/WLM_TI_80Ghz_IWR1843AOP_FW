/**
 *   @file  mmwdemo_spi1.h
 *
 *   @brief
 *      SPI1 (MibSPI Channel A) slave-mode init and fixed command/response
 *      example for the mmw Demo running on IWR1843AOP.
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2026 Texas Instruments, Inc.
 */

#ifndef MMWDEMO_SPI1_H
#define MMWDEMO_SPI1_H

#include <xdc/std.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! @brief Number of bytes in the fixed command (master -> slave). Command is
 *         0xA5 0x5A 0x01 0x02 (see gMmwDemoSpi1ExpectedCmd in mmwdemo_spi1.c).
 *         Must be a multiple of MMWDEMO_SPI1_CHUNK_LEN. */
#define MMWDEMO_SPI1_MSG_LEN            4U

/*! @brief Number of bytes in the response (slave -> master), and therefore
 *         the number of distinct response codes encodable: see
 *         @ref MmwDemo_spi1SetResponseCode. The diagnosed MISO fault
 *         (2026-08-25) only ever flips a bit that should be 0, right after a
 *         driven 1, into a spurious 1 - a bit that should be 1 has never
 *         once been misread as 0, and attempts to correct it in-band (extra
 *         CS setup delay, a throwaway leading byte, repeating the same byte
 *         N times and ANDing the reads) all failed because the corruption is
 *         reproducible/deterministic for a given bit pattern, not
 *         independent per attempt. The response is therefore thermometer
 *         (unary) coded instead of using an arbitrary bit pattern: a code in
 *         [0, MMWDEMO_SPI1_RESP_LEN] is sent as that many leading 0xFF bytes
 *         followed by 0x00 bytes. Every bit of every 0xFF byte is provably
 *         immune to the fault (no bit in it is ever required to be 0,
 *         regardless of what precedes it on the line - confirmed reliable
 *         over 64/64 transfers in testing), and a 0x00 byte that picks up a
 *         stray corrupted bit still reads as "not 0xFF" to the master's
 *         decoder, so the decoded code is unaffected either way. Must be a
 *         multiple of MMWDEMO_SPI1_CHUNK_LEN. */
#define MMWDEMO_SPI1_RESP_LEN            4U

/*! @brief Number of bytes transferred per SPI_transfer() / CS assertion.
 *         Per the IWR1843AOP datasheet (SWRS317, section 7.10.5.4 "Typical
 *         Interface Protocol Diagram (Peripheral Mode)"): "Host should ensure
 *         that CS is toggled for every 16 bits of transfer through SPI" when
 *         this device is the SPI slave - toggling more often (every 8 bits)
 *         still satisfies that. Set to 1 because the non-DMA/interrupt-driven
 *         MIBSPI_ISR path could not keep up with the 2nd byte of a 16-bit
 *         continuous-CS transfer in testing (only the 1st byte of each
 *         2-byte chunk was ever captured correctly). MMWDEMO_SPI1_MSG_LEN
 *         must be a multiple of this. */
#define MMWDEMO_SPI1_CHUNK_LEN           1U

/*! @brief SYSBIOS task priority used for @ref MmwDemo_spi1SlaveTask.
 *         Was bumped to 6 (above every radar task) to test a task-preemption
 *         theory for corrupted response bytes, but that made no measurable
 *         difference to the corruption (still 100% wrong, just more
 *         deterministic) while starving the radar object-detection task (no
 *         numDetectedObj/d-su output at all during the test) - so task
 *         preemption was not the actual cause and this is reverted back to
 *         its original low priority. */
#define MMWDEMO_SPI1_TASK_PRIORITY      2

/**
 *  @b Description
 *  @n
 *      Configures the SPI1 (MibSPI Channel A) pinmux and opens the SPI driver
 *      in SLAVE mode on the IWR1843AOP (ALP180A package) balls:
 *          CLK  -> D2, CS -> C2, MOSI -> F2, MISO -> D1
 *      (per IWR1843AOP datasheet SWRS317, Table 6-1).
 *
 *      Must be called once, before @ref MmwDemo_spi1SlaveTask is scheduled.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
extern int32_t MmwDemo_spi1Init(void);

/**
 *  @b Description
 *  @n
 *      Sets which response code is sent the next time the command is
 *      matched: thermometer/unary-encodes `code` into the response buffer
 *      as `code` leading 0xFF bytes followed by 0x00 bytes (see
 *      MMWDEMO_SPI1_RESP_LEN for why). `code` is clamped to
 *      [0, MMWDEMO_SPI1_RESP_LEN] if out of range.
 *
 *      Not synchronized against @ref MmwDemo_spi1SlaveTask - call this from
 *      application code well before the next expected command match (e.g.
 *      right after handling the previous one), not concurrently with a
 *      transfer in progress.
 *
 *  @param[in] code     Response code to send next, in [0, MMWDEMO_SPI1_RESP_LEN]
 *
 *  @retval
 *      Not Applicable.
 */
extern void MmwDemo_spi1SetResponseCode(uint8_t code);

/**
 *  @b Description
 *  @n
 *      Example SYSBIOS task body for SPI1. Waits (blocking) for a fixed
 *      command pattern from the SPI master and replies with a fixed
 *      response. Intended to be passed directly to Task_create().
 *
 *  @param[in] arg0     Unused (Task_create signature)
 *  @param[in] arg1     Unused (Task_create signature)
 *
 *  @retval
 *      Not Applicable.
 */
extern void MmwDemo_spi1SlaveTask(UArg arg0, UArg arg1);

#ifdef __cplusplus
}
#endif

#endif /* MMWDEMO_SPI1_H */
