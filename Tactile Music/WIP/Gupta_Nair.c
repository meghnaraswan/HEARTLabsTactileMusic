/*
 *  Copyright 2003 by Spectrum Digital Incorporated.
 *  All rights reserved. Property of Spectrum Digital Incorporated.
 */

/*
 *  ======== dsk_app.c ========
 *
 *  Version 1.00
 *
 *  This example digitally processes audio data from the line input on the
 *  AIC23 codec and plays the result on the line output.  It uses the McBSP
 *  and EDMA to efficiently handle the data transfer without intervention from
 *  the DSP.
 *
 *  Data transfer
 *
 *  Audio data is transferred back and forth from the codec through McBSP2,
 *  a bidirectional serial port.  The EDMA is configured to take every 16-bit
 *  signed audio sample arriving on McBSP1 and store it in a buffer in memory
 *  until it can be processed.  Once it has been processed, the EDMA
 *  controller sends the data back to McBSP1 for transmission.
 *
 *  A second serial port, McBSP0 is used to control/configure the AIC23.  The
 *  codec receives serial commands through McBSP0 that set configuration
 *  parameters such as volume, sample rate and data format.
 *
 *  In addition to basic EDMA transfers, this example uses 2 special
 *  techniques to make audio processing more convenient and efficient:
 *
 *  1)  Ping-pong data buffering in memory
 *  2)  Linked EDMA transfers
 *
 *  Applications with single buffers for receive and transmit data are
 *  very tricky and timing dependent because new data constantly overwrites
 *  the data being transmitted.  Ping-pong buffering is a technique where two
 *  buffers (referred to as the PING buffer and the PONG buffer) are used for
 *  a data transfer instead of only one.  The EDMA is configured to fill the
 *  PING buffer first, then the PONG buffer.  While the PONG buffer is being
 *  filled, the PING buffer can be processed with the knowledge that the
 *  current EDMA transfer won't overwrite it.  This example uses ping-pong
 *  buffers on both transmit and receive ends for a total of four buffers.
 *
 *  The EDMA controller must be configured slightly differently for each
 *  buffer.  When a buffer is filled, the EDMA controller generates an
 *  interrupt.  The interrupt handler must reload the configuration
 *  for the next buffer before the next audio sample arrives.  An EDMA
 *  feature called linked transfers is used to make this event less time
 *  critical.  Each configuration is created in advance and the EDMA
 *  controller automatically links to the next configuration when the
 *  current configuration is finished.  An interrupt is still generated,
 *  but it serves only to signal the DSP that it can process the data.
 *  The only time constraint is that all the audio data must be processed
 *  before the the active buffer fills up, which is much longer than the
 *  time between audio samples.  It is much easier to satisfy real-time
 *  constraints with this implementation.
 *
 *  Program flow
 *
 *  When the program is run, the individual DSP/BIOS modules are initialized
 *  as configured in dsk_app.cdb with the DSP/BIOS configuration tool.  The
 *  main() function is then called as the main user thread.  In this example
 *  main() performs application initialization and starts the EDMA data
 *  transfers.  When main exits, control passes back entirely to DSP/BIOS
 *  which services any interrupts or threads on an as-needed basis.
 *
 *  The edmaHwi() interrupt service routine is called when a buffer has been
 *  filled.  It contains a state variable named pingOrPong that indicates
 *  whether the buffer is a PING or PONG buffer.  dmaHwi switches the buffer
 *  state to the opposite buffer and calls the SWI thread processBuffer to
 *  process the audio data.
 *
 *  Other Functions
 *
 *  The example includes a few other functions that are executed in the
 *  background as examples of the multitasking that DSP/BIOS is capable of:
 *
 *  1)  blinkLED() toggles LED #0 every 500ms if DIP switch #0 is depressed.
 *      It is a periodic thread with a period of 500 ticks.
 *
 *  2)  load() simulates a 20-25% dummy load if DIP switch #1 is depressed.
 *      It represents other computation that may need to be done.  It is a
 *      periodic thread with a period of 10ms.
 *
 *  Please see the 6713 DSK help file under Software/Examples for more
 *  detailed information on this example.
 */

/*
 *  DSP/BIOS is configured using the DSP/BIOS configuration tool.  Settings
 *  for this example are stored in a configuration file called dsk_app.cdb.
 *  At compile time, Code Composer will auto-generate DSP/BIOS related files
 *  based on these settings.  A header file called dsk_appcfg.h contains the
 *  results of the autogeneration and must be included for proper operation.
 *  The name of the file is taken from dsk_app.cdb and adding cfg.h.
 */
#include "dsk_appcfg.h"

/*
 *  These are include files that support interfaces to BIOS and CSL modules
 *  used by the program.
 */
#include <std.h>
#include <swi.h>
#include <log.h>
#include <c6x.h>
#include <csl.h>
#include <csl_edma.h>
#include <csl_irq.h>
#include <csl_mcbsp.h>
#include <stdio.h>
/*
 *  The 6713 DSK Board Support Library is divided into several modules, each
 *  of which has its own include file.  The file dsk6713.h must be included
 *  in every program that uses the BSL.  This example also uses the
 *  DIP, LED and AIC23 modules.
 */
#include <dsk6713.h>
#include <dsk6713_led.h>
#include <dsk6713_dip.h>
#include <aic23.h>
#include <string.h>

/* Function prototypes */
void initIrq(void);
void initMcbsp(void);
void initEdma(void);
void processBuffer(void);
void edmaHwi(void);

/* Constants for the buffered ping-pong transfer */
#define BUFFSIZE 1024
#define PING 0
#define PONG 1
#define num_of_coeffs 101	// order of filter = 100
float AvgPLP=0.0, AvgPBP=0.0, AvgPHP=0.0;
float PLP=0.0,PBP=0.0,PHP=0.0;

/*coeffs for LED display*/
float lp1[13] ={0.127174276079605, 0.0581343489943583, 0.0681122463081755, 0.0766052817881472, 0.0830675938972334, 0.0871853443909994, 0.0884935091352945, 0.0871853443909994, 0.0830675938972334, 0.0766052817881472, 0.0681122463081755, 0.0581343489943583, 0.127174276079605};

float bp1[13]={0.0109723768383746, -0.0467943943338264, -0.0741398108994016, -0.149777301781025, 0.117993634189359, 0.192388845547486, 0.294512671843853, 0.192388845547486, 0.117993634189359, -0.149777301781025, -0.0741398108994016, -0.0467943943338264, 0.0109723768383746};

float hp1[13]={-0.0351103427314022, 0.120418583869658, 0.0883153039547716, 0.00865009773730016, -0.134411547496756, -0.277541793649009, 0.662413172546772, -0.277541793649009, -0.134411547496756, 0.00865009773730016, 0.0883153039547716, 0.120418583869658, -0.0351103427314022};


/*coeffs for filtering signal*/
float lp[101] = {0.000794206273044022, 0.000548194416095954, 0.000663759028931897, 0.000730868445533115, 0.000725042173370078, 0.000625403097601075, 0.000417966493673838, 9.95735925426323e-05, -0.000319729398910800, -0.000814113631211842, -0.00134291964974873, -0.00185146995043936, -0.00227604976855320, -0.00254877142578663, -0.00260556539105503, -0.00239330106441487, -0.00187925585202128, -0.00105780163669138, 4.29242654544079e-05, 0.00135928483246650, 0.00279201516631559, 0.00421174592220747, 0.00546638622898337, 0.00639450217019192, 0.00684017826993213, 0.00667108818699390, 0.00579404736101161, 0.00417181129718697, 0.00183584406187777, -0.00110549621443575, -0.00446772626977129, -0.00799355070314555, -0.0113648346373487, -0.0142260751327139, -0.0162041723486797, -0.0169421532171993, -0.0161267529445259, -0.0135198182013752, -0.00898349682512086, -0.00250101512389786, 0.00581172956195405, 0.0157047045708828, 0.0268009604244810, 0.0386166827614341, 0.0505881284732122, 0.0621078232828363, 0.0725640566815100, 0.0813837006945989, 0.0880710804968140, 0.0922444628176314, 0.0936628894927666, 0.0922444628176314, 0.0880710804968140, 0.0813837006945989, 0.0725640566815100, 0.0621078232828363, 0.0505881284732122, 0.0386166827614341, 0.0268009604244810, 0.0157047045708828, 0.00581172956195405, -0.00250101512389786, -0.00898349682512086, -0.0135198182013752, -0.0161267529445259, -0.0169421532171993, -0.0162041723486797, -0.0142260751327139, -0.0113648346373487, -0.00799355070314555, -0.00446772626977129, -0.00110549621443575, 0.00183584406187777, 0.00417181129718697, 0.00579404736101161, 0.00667108818699390, 0.00684017826993213, 0.00639450217019192, 0.00546638622898337, 0.00421174592220747, 0.00279201516631559, 0.00135928483246650, 4.29242654544079e-05, -0.00105780163669138, -0.00187925585202128, -0.00239330106441487, -0.00260556539105503, -0.00254877142578663, -0.00227604976855320, -0.00185146995043936, -0.00134291964974873, -0.000814113631211842, -0.000319729398910800, 9.95735925426323e-05, 0.000417966493673838, 0.000625403097601075, 0.000725042173370078, 0.000730868445533115, 0.000663759028931897, 0.000548194416095954, 0.000794206273044022};

float bp[101] = {-3.44129163333276e-05, -9.55777451440713e-06, 2.64899380714646e-05, 5.55858616219839e-05, 3.76123224727201e-05, -2.37894569729972e-05, -6.72758486875963e-05, -3.02726953188300e-05, 7.08690878564160e-05, 0.000131345355737871, 5.67311481966252e-05, -0.000116289533926509, -0.000219521231249759, -0.000113433667247428, 0.000135376673604786, 0.000265948932286808, 7.06744440433403e-05, -0.000338537990629421, -0.000547637061692444, -0.000212085991955519, 0.000515626914967612, 0.000938423771622001, 0.000297806786620044, -0.00155269478647518, -0.00386395990955181, -0.00284323714036927, -0.00500833002678241, -0.00574101906720537, -0.00440682620397027, 0.000735045794638078, 0.00809925858140467, 0.0127424168773443, 0.0110040579026694, 0.00561901315991171, 0.00468191357995884, 0.0132770865785417, 0.0256501707807756, 0.0279518390867756, 0.0118933403200059, -0.0136292140137046, -0.0275398620655114, -0.0175859792010491, 0.00229633694770758, -0.000485843011545146, -0.0454831232415294, -0.110965780974025, -0.140265785480084, -0.0854100172458280, 0.0460340892645079, 0.184341744473006, 0.243276200782189, 0.184341744473006, 0.0460340892645078, -0.0854100172458280, -0.140265785480084, -0.110965780974025, -0.0454831232415294, -0.000485843011545156, 0.00229633694770758, -0.0175859792010492, -0.0275398620655114, -0.0136292140137046, 0.0118933403200059, 0.0279518390867756, 0.0256501707807756, 0.0132770865785417, 0.00468191357995884, 0.00561901315991170, 0.0110040579026694, 0.0127424168773443, 0.00809925858140467, 0.000735045794638078, -0.00440682620397027, -0.00574101906720537, -0.00500833002678241, -0.00284323714036927, -0.00386395990955181, -0.00155269478647518, 0.000297806786620044, 0.000938423771622001, 0.000515626914967613, -0.000212085991955519, -0.000547637061692444, -0.000338537990629421, 7.06744440433402e-05, 0.000265948932286808, 0.000135376673604786, -0.000113433667247428, -0.000219521231249759, -0.000116289533926509, 5.67311481966252e-05, 0.000131345355737871, 7.08690878564160e-05, -3.02726953188300e-05, -6.72758486875963e-05, -2.37894569729972e-05, 3.76123224727201e-05, 5.55858616219839e-05, 2.64899380714646e-05, -9.55777451440713e-06, -3.44129163333276e-05};

float hp[101] = {-3.71378189903913e-06, -0.000385543776213135, -0.000174286613119065, 0.000155157397306870, 0.000447502003880196, 0.000299012398223559, -0.000291860363810950, -0.000755737883023330, -0.000461619506027581, 0.000503496026963570, 0.00118813038360108, 0.000665865452239553, -0.000816332162203236, -0.00177382913521060, -0.000911957710963548, 0.00126274639089030, 0.00254599785197993, 0.00119733493481360, -0.00188298883432695, -0.00354369458049196, -0.00151685212622968, 0.00272779656450504, 0.00481532178960400, 0.00186296024772780, -0.00386354139677245, -0.00642521516611152, -0.00222550571371127, 0.00538243640005939, 0.00846605162099258, 0.00259215799409269, -0.00742229885590284, -0.0110854266837481, -0.00294952555849002, 0.0102082964516512, 0.0145406925245107, 0.00328346668466473, -0.0141499399473485, -0.0193309086282212, -0.00357979828755701, 0.0200969128979563, 0.0265683984078155, 0.00382562538269158, -0.0301733841938447, -0.0392988908601045, -0.00400986239956292, 0.0516171374349632, 0.0697678224436065, 0.00412388283250039, -0.135158465095099, -0.277442591451597, 0.662504031041749, -0.277442591451597, -0.135158465095099, 0.00412388283250039, 0.0697678224436065, 0.0516171374349632, -0.00400986239956292, -0.0392988908601045, -0.0301733841938447, 0.00382562538269158, 0.0265683984078155, 0.0200969128979563, -0.00357979828755701, -0.0193309086282212, -0.0141499399473485, 0.00328346668466473, 0.0145406925245107, 0.0102082964516512, -0.00294952555849002, -0.0110854266837481, -0.00742229885590284, 0.00259215799409269, 0.00846605162099258, 0.00538243640005939, -0.00222550571371127, -0.00642521516611152, -0.00386354139677245, 0.00186296024772780, 0.00481532178960400, 0.00272779656450504, -0.00151685212622968, -0.00354369458049196, -0.00188298883432695, 0.00119733493481360, 0.00254599785197993, 0.00126274639089030, -0.000911957710963548, -0.00177382913521060, -0.000816332162203236, 0.000665865452239553, 0.00118813038360108, 0.000503496026963570, -0.000461619506027581, -0.000755737883023330, -0.000291860363810950, 0.000299012398223559, 0.000447502003880196, 0.000155157397306870, -0.000174286613119065, -0.000385543776213135, -3.71378189903913e-06};

int dip_value;	//gets the number corresponding to the dip  switches pressed
float presentsamp; 	/*present sample is defined as a global variable as
			it gets the value from processBuffer and is used in blinkLED simulatenously*/

/*
 * Data buffer declarations - the program uses four logical buffers of size
 * BUFFSIZE, one ping and one pong buffer on both receive and transmit sides.
 */
Int16 gBufferXmtPing[BUFFSIZE];  // Transmit PING buffer
//Int16 gBufferXmtSing[BUFFSIZE];  // Transmit SING buffer
Int16 gBufferXmtPong[BUFFSIZE];  // Transmit PONG buffer

Int16 gBufferRcvPing[BUFFSIZE];  // Receive PING buffer
//Int16 gBufferRcvSing[BUFFSIZE];  // Receive SING buffer
Int16 gBufferRcvPong[BUFFSIZE];  // Receive PONG buffer

EDMA_Handle hEdmaXmt;            // EDMA channel handles
EDMA_Handle hEdmaReloadXmtPing;
EDMA_Handle hEdmaReloadXmtPong;
EDMA_Handle hEdmaRcv;
EDMA_Handle hEdmaReloadRcvPing;
EDMA_Handle hEdmaReloadRcvPong;

MCBSP_Handle hMcbsp1;                 // McBSP1 (codec data) handle

Int16 gXmtChan;                       // TCC codes (see initEDMA())
Int16 gRcvChan;

/*
 *  EDMA Config data structure
 */

/* Transmit side EDMA configuration */
EDMA_Config gEdmaConfigXmt = {
    EDMA_FMKS(OPT, PRI, HIGH)          |  // Priority
    EDMA_FMKS(OPT, ESIZE, 16BIT)       |  // Element size
    EDMA_FMKS(OPT, 2DS, NO)            |  // 2 dimensional source?
    EDMA_FMKS(OPT, SUM, INC)           |  // Src update mode
    EDMA_FMKS(OPT, 2DD, NO)            |  // 2 dimensional dest
    EDMA_FMKS(OPT, DUM, NONE)          |  // Dest update mode
    EDMA_FMKS(OPT, TCINT, YES)         |  // Cause EDMA interrupt?
    EDMA_FMKS(OPT, TCC, OF(0))         |  // Transfer complete code
    EDMA_FMKS(OPT, LINK, YES)          |  // Enable link parameters?
    EDMA_FMKS(OPT, FS, NO),               // Use frame sync?

    (Uint32)&gBufferXmtPing,              // Src address

    EDMA_FMK (CNT, FRMCNT, NULL)       |  // Frame count
    EDMA_FMK (CNT, ELECNT, BUFFSIZE),     // Element count

    EDMA_FMKS(DST, DST, OF(0)),           // Dest address

    EDMA_FMKS(IDX, FRMIDX, DEFAULT)    |  // Frame index value
    EDMA_FMKS(IDX, ELEIDX, DEFAULT),      // Element index value

    EDMA_FMK (RLD, ELERLD, NULL)       |  // Reload element
    EDMA_FMK (RLD, LINK, NULL)            // Reload link
};

/* Receive side EDMA configuration */
EDMA_Config gEdmaConfigRcv = {
    EDMA_FMKS(OPT, PRI, HIGH)          |  // Priority
    EDMA_FMKS(OPT, ESIZE, 16BIT)       |  // Element size
    EDMA_FMKS(OPT, 2DS, NO)            |  // 2 dimensional source?
    EDMA_FMKS(OPT, SUM, NONE)          |  // Src update mode
    EDMA_FMKS(OPT, 2DD, NO)            |  // 2 dimensional dest
    EDMA_FMKS(OPT, DUM, INC)           |  // Dest update mode
    EDMA_FMKS(OPT, TCINT, YES)         |  // Cause EDMA interrupt?
    EDMA_FMKS(OPT, TCC, OF(0))         |  // Transfer complete code
    EDMA_FMKS(OPT, LINK, YES)          |  // Enable link parameters?
    EDMA_FMKS(OPT, FS, NO),               // Use frame sync?

    EDMA_FMKS(SRC, SRC, OF(0)),           // Src address

    EDMA_FMK (CNT, FRMCNT, NULL)       |  // Frame count
    EDMA_FMK (CNT, ELECNT, BUFFSIZE),     // Element count

    (Uint32)&gBufferRcvPing,              // Dest address

    EDMA_FMKS(IDX, FRMIDX, DEFAULT)    |  // Frame index value
    EDMA_FMKS(IDX, ELEIDX, DEFAULT),      // Element index value

    EDMA_FMK (RLD, ELERLD, NULL)       |  // Reload element
    EDMA_FMK (RLD, LINK, NULL)            // Reload link
};

/* McBSP codec data channel configuration */
static MCBSP_Config mcbspCfg1 = {
        MCBSP_FMKS(SPCR, FREE, NO)              |
        MCBSP_FMKS(SPCR, SOFT, NO)              |
        MCBSP_FMKS(SPCR, FRST, YES)             |
        MCBSP_FMKS(SPCR, GRST, YES)             |
        MCBSP_FMKS(SPCR, XINTM, XRDY)           |
        MCBSP_FMKS(SPCR, XSYNCERR, NO)          |
        MCBSP_FMKS(SPCR, XRST, YES)             |
        MCBSP_FMKS(SPCR, DLB, OFF)              |
        MCBSP_FMKS(SPCR, RJUST, RZF)            |
        MCBSP_FMKS(SPCR, CLKSTP, DISABLE)       |
        MCBSP_FMKS(SPCR, DXENA, OFF)            |
        MCBSP_FMKS(SPCR, RINTM, RRDY)           |
        MCBSP_FMKS(SPCR, RSYNCERR, NO)          |
        MCBSP_FMKS(SPCR, RRST, YES),

        MCBSP_FMKS(RCR, RPHASE, SINGLE)         |
        MCBSP_FMKS(RCR, RFRLEN2, DEFAULT)       |
        MCBSP_FMKS(RCR, RWDLEN2, DEFAULT)       |
        MCBSP_FMKS(RCR, RCOMPAND, MSB)          |
        MCBSP_FMKS(RCR, RFIG, NO)               |
        MCBSP_FMKS(RCR, RDATDLY, 0BIT)          |
        MCBSP_FMKS(RCR, RFRLEN1, OF(1))         |
        MCBSP_FMKS(RCR, RWDLEN1, 16BIT)         |
        MCBSP_FMKS(RCR, RWDREVRS, DISABLE),

        MCBSP_FMKS(XCR, XPHASE, SINGLE)         |
        MCBSP_FMKS(XCR, XFRLEN2, DEFAULT)       |
        MCBSP_FMKS(XCR, XWDLEN2, DEFAULT)       |
        MCBSP_FMKS(XCR, XCOMPAND, MSB)          |
        MCBSP_FMKS(XCR, XFIG, NO)               |
        MCBSP_FMKS(XCR, XDATDLY, 0BIT)          |
        MCBSP_FMKS(XCR, XFRLEN1, OF(1))         |
        MCBSP_FMKS(XCR, XWDLEN1, 16BIT)         |
        MCBSP_FMKS(XCR, XWDREVRS, DISABLE),

        MCBSP_FMKS(SRGR, GSYNC, DEFAULT)        |
        MCBSP_FMKS(SRGR, CLKSP, DEFAULT)        |
        MCBSP_FMKS(SRGR, CLKSM, DEFAULT)        |
        MCBSP_FMKS(SRGR, FSGM, DEFAULT)         |
        MCBSP_FMKS(SRGR, FPER, DEFAULT)         |
        MCBSP_FMKS(SRGR, FWID, DEFAULT)         |
        MCBSP_FMKS(SRGR, CLKGDV, DEFAULT),

        MCBSP_MCR_DEFAULT,
        MCBSP_RCER_DEFAULT,
        MCBSP_XCER_DEFAULT,

        MCBSP_FMKS(PCR, XIOEN, SP)              |
        MCBSP_FMKS(PCR, RIOEN, SP)              |
        MCBSP_FMKS(PCR, FSXM, EXTERNAL)         |
        MCBSP_FMKS(PCR, FSRM, EXTERNAL)         |
        MCBSP_FMKS(PCR, CLKXM, INPUT)           |
        MCBSP_FMKS(PCR, CLKRM, INPUT)           |
        MCBSP_FMKS(PCR, CLKSSTAT, DEFAULT)      |
        MCBSP_FMKS(PCR, DXSTAT, DEFAULT)        |
        MCBSP_FMKS(PCR, FSXP, ACTIVEHIGH)       |
        MCBSP_FMKS(PCR, FSRP, ACTIVEHIGH)       |
        MCBSP_FMKS(PCR, CLKXP, FALLING)         |
        MCBSP_FMKS(PCR, CLKRP, RISING)
};

/* Codec configuration settings */
AIC23_Params config = {
    0x0017, // 0 DSK6713_AIC23_LEFTINVOL  Left line input channel volume
    0x0017, // 1 DSK6713_AIC23_RIGHTINVOL Right line input channel volume
    0x00f2, // 2 DSK6713_AIC23_LEFTHPVOL  Left channel headphone volume
    0x00f2, // 3 DSK6713_AIC23_RIGHTHPVOL Right channel headphone volume
    0x0011, // 4 DSK6713_AIC23_ANAPATH    Analog audio path control
    0x0000, // 5 DSK6713_AIC23_DIGPATH    Digital audio path control
    0x0000, // 6 DSK6713_AIC23_POWERDOWN  Power down control
    0x0043, // 7 DSK6713_AIC23_DIGIF      Digital audio interface format
    0x000d, // 8 DSK6713_AIC23_SAMPLERATE Sample rate control
    0x0001  // 9 DSK6713_AIC23_DIGACT     Digital interface activation
};


/* --------------------------- main() function -------------------------- */
/*
 *  main() - The main user task.  Performs application initialization and
 *           starts the data transfer.
 */
void main()
{
    /* Initialize Board Support Library */
    DSK6713_init();

    /* Initialize LEDs and DIP switches */
    DSK6713_LED_init();
    DSK6713_DIP_init();

	/*Initialize Chip Support Library*/
	CSL_init();


    /* Clear buffers */
    memset((void *)gBufferXmtPing, 0, BUFFSIZE * 4 * 2);

    AIC23_setParams(&config);  // Configure the codec

    initMcbsp();               // Initialize McBSP1 for audio transfers

    IRQ_globalDisable();       // Disable global interrupts during setup

    initEdma();                // Initialize the EDMA controller

    initIrq();                 // Initialize interrupts

   IRQ_globalEnable();        // Re-enable global interrupts
}


/* ------------------------Helper Functions ----------------------------- */

/*
 *  initMcbsp() - Initialize the McBSP for codec data transfers using the
 *                configuration define at the top of this file.
 */
void initMcbsp()
{
    /* Open the codec data McBSP */
    hMcbsp1 = MCBSP_open(MCBSP_DEV1, MCBSP_OPEN_RESET);

    /* Configure the codec to match the AIC23 data format */
    MCBSP_config(hMcbsp1, &mcbspCfg1);

    /* Start the McBSP running */
    MCBSP_start(hMcbsp1, MCBSP_XMIT_START | MCBSP_RCV_START |
        MCBSP_SRGR_START | MCBSP_SRGR_FRAMESYNC, 220);
}


/*
 *  initIrq() - Initialize and enable the DMA receive interrupt using the CSL.
 *              The interrupt service routine for this interrupt is edmaHwi.
 */
void initIrq(void)
{
    /* Enable EDMA interrupts to the CPU */
   IRQ_clear(IRQ_EVT_EDMAINT);    // Clear any pending EDMA interrupts
   IRQ_enable(IRQ_EVT_EDMAINT);   // Enable EDMA interrupt

}

/*
 *  initEdma() - Initialize the DMA controller.  Use linked transfers to
 *               automatically transition from ping to pong and visa-versa.
 */
void initEdma(void)
{
    /* Configure transmit channel */
    hEdmaXmt = EDMA_open(EDMA_CHA_XEVT1, EDMA_OPEN_RESET);  // get hEdmaXmt handle and reset channel
    hEdmaReloadXmtPing = EDMA_allocTable(-1);               // get hEdmaReloadXmtPing handle
    hEdmaReloadXmtPong = EDMA_allocTable(-1);               // get hEdmaReloadXmtPong handle


	gEdmaConfigXmt.dst = MCBSP_getXmtAddr(hMcbsp1);         // set the desination address to McBSP1 DXR

    gXmtChan = EDMA_intAlloc(-1);                           // get an open TCC

    gEdmaConfigXmt.opt |= EDMA_FMK(OPT,TCC,gXmtChan);       // set TCC to gXmtChan

    EDMA_config(hEdmaXmt, &gEdmaConfigXmt);                 // then configure the registers
    EDMA_config(hEdmaReloadXmtPing, &gEdmaConfigXmt);       // and the reload for Ping


    gEdmaConfigXmt.src = EDMA_SRC_OF(gBufferXmtPong);       // change the structure to have a source of Pong
    EDMA_config(hEdmaReloadXmtPong, &gEdmaConfigXmt);       // and configure the reload for Pong



    EDMA_link(hEdmaXmt,hEdmaReloadXmtPong);                 // link the regs to Pong
    EDMA_link(hEdmaReloadXmtPong,hEdmaReloadXmtPing);       // link Pong to Ping
    EDMA_link(hEdmaReloadXmtPing,hEdmaReloadXmtPong);       // and link Ping to Pong


    /* Configure receive channel */
    hEdmaRcv = EDMA_open(EDMA_CHA_REVT1, EDMA_OPEN_RESET);  // get hEdmaRcv handle and reset channel
    hEdmaReloadRcvPing = EDMA_allocTable(-1);               // get hEdmaReloadRcvPing handle
    hEdmaReloadRcvPong = EDMA_allocTable(-1);               // get hEdmaReloadRcvPong handle

    gEdmaConfigRcv.src = MCBSP_getRcvAddr(hMcbsp1);         // and the desination address to McBSP1 DXR

    gRcvChan = EDMA_intAlloc(-1);                           // get an open TCC
    gEdmaConfigRcv.opt |= EDMA_FMK(OPT,TCC,gRcvChan);       // set TCC to gRcvChan


    EDMA_config(hEdmaRcv, &gEdmaConfigRcv);                 // then configure the registers
    EDMA_config(hEdmaReloadRcvPing, &gEdmaConfigRcv);       // and the reload for Ping

    gEdmaConfigRcv.dst = EDMA_DST_OF(gBufferRcvPong);       // change the structure to have a destination of Pong
    EDMA_config(hEdmaReloadRcvPong, &gEdmaConfigRcv);       // and configure the reload for Pong

    EDMA_link(hEdmaRcv,hEdmaReloadRcvPong);                 // link the regs to Pong
    EDMA_link(hEdmaReloadRcvPong,hEdmaReloadRcvPing);       // link Pong to Ping
    EDMA_link(hEdmaReloadRcvPing,hEdmaReloadRcvPong);       // and link Ping to Pong

    /* Enable interrupts in the EDMA controller */
    EDMA_intClear(gXmtChan);
    EDMA_intClear(gRcvChan);                                // clear any possible spurious interrupts


    EDMA_intEnable(gXmtChan);                               // enable EDMA interrupts (CIER)
    EDMA_intEnable(gRcvChan);                               // enable EDMA interrupts (CIER)

    EDMA_enableChannel(hEdmaXmt);                           // enable EDMA channel
    EDMA_enableChannel(hEdmaRcv);                           // enable EDMA channel


    /* Do a dummy write to generate the first McBSP transmit event */
    MCBSP_write(hMcbsp1, 0);

}


/* ---------------------- Interrupt Service Routines -------------------- */

/*
 *  edmaHwi() - Interrupt service routine for the DMA transfer.  It is
 *              triggered when a complete DMA receive frame has been
 *              transferred.   The edmaHwi ISR is inserted into the interrupt
 *              vector table at compile time through a setting in the DSP/BIOS
 *              configuration under Scheduling --> HWI --> HWI_INT8.  edmaHwi
 *              uses the DSP/BIOS Dispatcher to save register state and make
 *              sure the ISR co-exists with other DSP/BIOS functions.
 */
void edmaHwi(void)
{
    static Uint32 pingOrPong = PING;  // Ping-pong state variable
    static Int16 xmtdone = 0, rcvdone = 0;

    /* Check CIPR to see which transfer completed */
    if (EDMA_intTest(gXmtChan))
    {
        EDMA_intClear(gXmtChan);
        xmtdone = 1;
    }
    if (EDMA_intTest(gRcvChan))
    {
        EDMA_intClear(gRcvChan);
        rcvdone = 1;
    }

    /* If both transfers complete, signal processBufferSwi to handle */
    if (xmtdone && rcvdone)
    {
        if (pingOrPong==PING)
        {
            SWI_or(&processBufferSwi, PING);
            pingOrPong = PONG;
        } else
        {
            SWI_or(&processBufferSwi, PONG);
            pingOrPong = PING;
        }
        rcvdone = 0;
        xmtdone = 0;
    }
}


/* ------------------------------- Threads ------------------------------ */

/*
 *  processBuffer() - Process audio data once it has been received.
 */
void processBuffer(void)
{
    Uint32 pingPong;
	int j, n, x, y, z;
	int n1=0;
	float output;
	float output_left=0.0, output_right=0.0;
	Int i;
	float h[101] = {0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0,0.0};
	int k=0;

switch (dip_value)
 {//depending on the switches chosen/ON, generate the h value.
 case 0:
	k=2; //no pass
	x=0, y=0, z=0;
	break;
 case 1:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = lp[i];
	x=0, y=0, z=1;
	break;
 case 2:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = bp[i];
	x=0, y=1, z=0;
	break;
 case 3:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = lp[i]+bp[i];
	x=0, y=1, z=1;
	break;
 case 4:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = hp[i];
	x=1, y=0, z=0;
	break;
 case 5:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = hp[i]+lp[i];
	x=1, y=0, z=1;
	break;
 case 6:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = hp[i]+bp[i];
	x=1, y=1, z=0;
	break;
 case 7:
	for(i=0;i<num_of_coeffs;i++)
	h[i] = hp[i]+bp[i]+lp[i];
	x=1, y=1, z=1;
	break;
 default:
	k=1; //by-pass
 }


PLP=0.0, PBP=0.0, PHP=0.0;
    /* Get contents of mailbox posted by edmaHwi */
    pingPong =  SWI_getmbox();

if (k==0)//one or more of the switches is ON
{
   /*filtering signal to produce a better sound output*/
  if (pingPong == PING)
  {
	for(i=0;i<1024;i++) // each buffer length = 1024
	{//compute one filtered output sample, and repeat 1024 times for the entire buffer
		output_left = 0.0,output_right=0.0, j=0; //output_left => filtered signal for left channel
		for(n=i+1;n>i-201;n--) // filter left and right channels seperately
                          // using 101 samples on each channel => total 202 samples
		{
		if(n<0) //if samples required are in the previous buffer
			{
			n1 = 1024+n; // take the last few samples from PONG, since current buffer is PING
			output_left += h[j]*gBufferRcvPong[n1];
			}
		else
			{
			output_left += h[j]*gBufferRcvPing[n];
			}
		n--;
    //left & right channel samples alternate
    //so repeat the process for each channel
		if(n<0)
			{
			n1 = 1024+n;
			output_right += h[j]*gBufferRcvPong[n1];
			}
		else
			{
			output_right += h[j]*gBufferRcvPing[n];
			}
		j++;  //get next filter coefficient to multiply
		}
    // put the alternate output samples in Ping buffer for transmission
		gBufferXmtPing[i] = output_right;
		i++;
		gBufferXmtPing[i] = output_left;
	}

	/*Low Pass filtering for LED display */
    if(z==1)
    {
    AvgPLP=0.0, PLP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-12;n--)
		{
		output += lp1[j]*gBufferRcvPing[n];
		j++;
		}
		PLP += output*output;//LPF Power for each output sample added up
		}
		AvgPLP = PLP/1024; //Avg. mean square value for the whole buffer
    }

	/*Band Pass filtering for LED display */
	if(y==1)
    {
	AvgPBP=0.0, PBP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-12;n--)
		{
		output += bp1[j]*gBufferRcvPing[n];
		j++;
		}
		PBP += output*output;
  	  }
		AvgPBP = PBP/1024;
    }

	/*High Pass filtering for LED display */
	if(x==1)
    {
    AvgPHP=0.0, PHP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-12;n--)
		{
		output += hp1[j]*gBufferRcvPing[n];
		j++;
		}
		PHP = output*output;
		}
		AvgPHP = PHP/1024;
    }
 } 	//end of Ping

/*filtering signal to produce a better sound output*/
  else
   {
 	for(i=0;i<1024;i++)
	{
		output_left = 0.0,output_right=0.0, j=0, n1=0;

		for(n=i+1;n>i-201;n--)
		{
			if(n<0)
			{
				n1 = 1024+n;
				output_left += h[j]*gBufferRcvPing[n1];
			}
			else
			{
				output_left += h[j]*gBufferRcvPong[n];
			}
			n--;
			if(n<0)
			{
				n1 = 1024+n;
				output_right += h[j]*gBufferRcvPing[n1];
			}
			else
			{
				output_right += h[j]*gBufferRcvPong[n];
			}
		j++;
		}
		gBufferXmtPong[i] = output_right;
		i++;
		gBufferXmtPong[i] = output_left;
	}

	/*Low Pass filtering for LED display */
	if(z==1)
    {
	AvgPLP=0.0, PLP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-24;n--)
		{
		output += lp1[j]*gBufferRcvPong[n];
		j++;
		}
		PLP += output*output;
  	  }
		AvgPLP = PLP/1024;
    }

	/*Band Pass filtering for LED display */
	if(y==1)
    {
    AvgPBP=0.0, PBP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-12;n--)
		{
		output += bp1[j]*gBufferRcvPong[n];
		j++;
		}
		PBP += output*output;
  	  }
		AvgPBP = PBP/1024;
    }

	/*High Pass filtering for LED display */
	if(x==1)
    {
    AvgPHP=0.0, PHP=0.0;
	for(i=0;i<1024;i++)
	  {
		output = 0.0, j=0;
		for(n=i+1;n>i-12;n--)
		{
		output += hp1[j]*gBufferRcvPong[n];
		j++;
		}
		PHP += output*output;
  	  }
		AvgPHP = PHP/1024;
    }
 } 	//end of Pong
} 	//end of k=0

else if(k==1) //all pass filter when switch 3 is pressed
{
   if (pingPong == PING)
   {	for(i=0;i<1024;i++)
		gBufferXmtPing[i] = gBufferRcvPing[i];
   }	//end of Ping

   else
   {	for(i=0;i<1024;i++)
		gBufferXmtPong[i] = gBufferRcvPong[i];
   } 	//end of Pong
} 	//end of k=1

else 	// no pass filter when no switch is pressed
{
   if (pingPong == PING)
   {	for(i=0;i<1024;i++)
		gBufferXmtPing[i] = 0;
   }	//end of Ping


   else
   {	for(i=0;i<1024;i++)
		gBufferXmtPong[i] = 0;
   } 	//end of Pong
} 	//end of k=2
} //end of processBuffer()

/*
 *  blinkLED() - Periodic thread (PRD) that toggles LED #0 every 500ms if
 *               DIP switch #0 is depressed.  The thread is configured
 *               in the DSP/BIOS configuration tool under Scheduling -->
 *               PRD --> PRD_blinkLed.  The period is set there at 500
 *               ticks, with each tick corresponding to 1ms in real
 *               time.
 */
void blinkLED(void)
{
		if(AvgPLP>800000)
		{
		DSK6713_LED_on(0);
		AvgPLP=0.0;
		}
		else
		{
		DSK6713_LED_off(0);
		}

		if(AvgPBP>400000)
		{
		DSK6713_LED_on(1);
		AvgPBP=0.0;
		}
		else
		{
		DSK6713_LED_off(1);
		}

		if(AvgPHP>125)
		{
		DSK6713_LED_on(2);
		AvgPHP=0.0;
		}
		else
		{
		DSK6713_LED_off(2);
		}

}


/*
 *  load() - PRD that simulates a 20-25% dummy load on a 225MHz 6713 if
 *           DIP switch #1 is depressed.  The thread is configured in
 *           the DSP/BIOS configuration tool under Scheduling --> PRD
 *           PRD_load.  The period is set there at 10 ticks, which each tick
 *           corresponding to 1ms in real time.
 */
void load(void)
{
	dip_value=(!(DSK6713_DIP_get(3))*8)+(!(DSK6713_DIP_get(2))*4)+(!(DSK6713_DIP_get(1))*2)+(!(DSK6713_DIP_get(0))*1);
}
