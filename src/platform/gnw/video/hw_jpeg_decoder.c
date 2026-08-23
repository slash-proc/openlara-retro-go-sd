/*
 * Hardware JPEG decode for the video homebrew.
 *
 * Same recipe as the firmware decoder (JPEG core → YCbCr → DMA2D → RGB565),
 * but linked into this binary so we own SrcSize, the GetDataCallback, and
 * error handling. HAL_JPEG_Decode is polling (CPU feeds the FIFOs) — no JPEG
 * or MDMA IRQ, so we do not fight the firmware vector table.
 *
 * JPEG / DMA2D clocks are already on from firmware boot; MspInit re-enables
 * them and masks the JPEG IRQ. DeInit leaves the peripheral quiet so the
 * launcher can HAL_JPEG_Init its own handle for covers afterwards.
 */

#include "hw_jpeg_decoder.h"
#include "main.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_mdma.h"

#include <stdint.h>
#include <string.h>

static JPEG_HandleTypeDef JPEG_Handle;
static DMA2D_HandleTypeDef DMA2D_Handle;

#define LCD_X_SIZE ((uint32_t)320)
#define JPEG_DECODE_TIMEOUT_MS 200
#define MCU_ROUND(v, n) (((uint32_t)(v) + (n) - 1) / (n) * (n))

static uint16_t xPos;
static uint16_t yPos;
static uint8_t ForegroundAlpha = 0xFF;
static JPEG_ConfTypeDef JPEG_info;
static uint32_t FrameBufferAddress;
static uint32_t JPEGBufferAddress;
static uint32_t JPEGBufferSize;
static uint32_t FrameBufferMode;
static uint32_t disable_transfer;
static uint32_t decode_rejected;

uint32_t g_jpeg_hal, g_jpeg_err, g_jpeg_rej, g_jpeg_sub, g_jpeg_need;

static void (*s_poll)(void);

void video_jpeg_set_poll(void (*fn)(void))
{
    s_poll = fn;
}

void video_jpeg_poll(void)
{
    if (s_poll)
        s_poll();
}

static void COPY_JpegOut(void);

void HAL_JPEG_MspInit(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    __HAL_RCC_JPGDECEN_CLK_ENABLE();
    __HAL_RCC_DMA2D_CLK_ENABLE();
    NVIC_DisableIRQ(JPEG_IRQn);
    NVIC_DisableIRQ(DMA2D_IRQn);
}

void HAL_JPEG_MspDeInit(JPEG_HandleTypeDef *hjpeg)
{
    (void)hjpeg;
    NVIC_DisableIRQ(JPEG_IRQn);
}

static void JPEG_HandleReset(void)
{
    JPEG_Handle.Instance = JPEG;
    JPEG_Handle.State = HAL_JPEG_STATE_RESET;
    JPEG_Handle.Lock = HAL_UNLOCKED;
    JPEG_Handle.ErrorCode = HAL_JPEG_ERROR_NONE;
}

static uint32_t JPEG_DecodeInit(uint32_t work, uint32_t work_size)
{
    JPEGBufferAddress = work;
    JPEGBufferSize = work_size;
    JPEG_HandleReset();
    return HAL_JPEG_Init(&JPEG_Handle);
}

uint32_t video_jpeg_init(uint32_t work, uint32_t work_size)
{
    FrameBufferMode = 1;
    return JPEG_DecodeInit(work, work_size);
}

uint32_t video_jpeg_deinit(void)
{
    (void)HAL_DMA2D_DeInit(&DMA2D_Handle);
    return HAL_JPEG_DeInit(&JPEG_Handle);
}

static uint32_t JPEG_Run(uint32_t SrcAddress, uint32_t SrcSize)
{
    if (SrcAddress == 0 || SrcSize == 0)
        return 1;

    /* HAL floors InDataLength to a multiple of 4; round up so EOI (FF D9)
     * is not dropped. The extra bytes sit inside the 64 KiB slot. */
    SrcSize = (SrcSize + 3u) & ~3u;

    decode_rejected = 0;
    g_jpeg_hal = g_jpeg_err = g_jpeg_rej = g_jpeg_sub = g_jpeg_need = 0;
    memset(&JPEG_info, 0, sizeof(JPEG_info));

    HAL_StatusTypeDef st = HAL_JPEG_Decode(&JPEG_Handle, (uint8_t *)SrcAddress, SrcSize,
                                           (uint8_t *)JPEGBufferAddress, JPEGBufferSize,
                                           JPEG_DECODE_TIMEOUT_MS);
    wdog_refresh();
    g_jpeg_hal = (uint32_t)st;
    g_jpeg_err = JPEG_Handle.ErrorCode;
    if (st != HAL_OK || decode_rejected) {
        (void)HAL_JPEG_Abort(&JPEG_Handle);
        JPEG_Handle.Lock = HAL_UNLOCKED;
        JPEG_Handle.ErrorCode = HAL_JPEG_ERROR_NONE;
        JPEG_Handle.State = HAL_JPEG_STATE_READY;
        return 1;
    }
    return 0;
}

uint32_t video_jpeg_decode(uint32_t src, uint32_t src_size, uint32_t dst,
                           uint16_t x, uint16_t y, uint8_t luma_alpha)
{
    FrameBufferAddress = dst;
    ForegroundAlpha = luma_alpha;
    disable_transfer = 0;
    xPos = x;
    yPos = y;
    return JPEG_Run(src, src_size);
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hJPEG, uint8_t *pDataOut, uint32_t OutDataLength)
{
    (void)hJPEG;
    (void)pDataOut;
    (void)OutDataLength;
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hJPEG)
{
    (void)hJPEG;
    decode_rejected = 1;
}

void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hJPEG)
{
    (void)hJPEG;
    if (disable_transfer == 0 && decode_rejected == 0)
        COPY_JpegOut();
}

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hJPEG, JPEG_ConfTypeDef *pInfo)
{
    (void)pInfo;
    if (HAL_OK != HAL_JPEG_GetInfo(hJPEG, &JPEG_info)) {
        decode_rejected = 1;
        g_jpeg_rej = 1;
        (void)HAL_JPEG_Pause(hJPEG, JPEG_PAUSE_RESUME_INPUT_OUTPUT);
        return;
    }

    uint32_t ImgSize;
    g_jpeg_sub = JPEG_info.ChromaSubsampling;
    if (JPEG_info.ChromaSubsampling == JPEG_420_SUBSAMPLING)
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 16) * MCU_ROUND(JPEG_info.ImageHeight, 16) * 3 / 2;
    else if (JPEG_info.ChromaSubsampling == JPEG_422_SUBSAMPLING)
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 16) * MCU_ROUND(JPEG_info.ImageHeight, 8) * 2;
    else
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 8) * MCU_ROUND(JPEG_info.ImageHeight, 8) * 3;

    g_jpeg_need = ImgSize;
    if (ImgSize > JPEGBufferSize) {
        decode_rejected = 1;
        g_jpeg_rej = 2;
        (void)HAL_JPEG_Pause(hJPEG, JPEG_PAUSE_RESUME_INPUT_OUTPUT);
    }
}

void HAL_JPEG_GetDataCallback(JPEG_HandleTypeDef *hJPEG, uint32_t NbDecodedData)
{
    (void)NbDecodedData;
    /* Exact-size frames hit this on every good decode. Report end-of-input;
     * do not treat it as a rejection. */
    HAL_JPEG_ConfigInputBuffer(hJPEG, NULL, 0);
}

static uint32_t cssMode = DMA2D_CSS_420, inputLineOffset;

static int COPY_JpegOutInit(void)
{
    DMA2D_Handle.Instance = DMA2D;

    if (JPEG_info.ChromaSubsampling == JPEG_420_SUBSAMPLING) {
        cssMode = DMA2D_CSS_420;
        inputLineOffset = JPEG_info.ImageWidth % 16;
        if (inputLineOffset != 0)
            inputLineOffset = 16 - inputLineOffset;
    } else if (JPEG_info.ChromaSubsampling == JPEG_444_SUBSAMPLING) {
        cssMode = DMA2D_NO_CSS;
        inputLineOffset = JPEG_info.ImageWidth % 8;
        if (inputLineOffset != 0)
            inputLineOffset = 8 - inputLineOffset;
    } else if (JPEG_info.ChromaSubsampling == JPEG_422_SUBSAMPLING) {
        cssMode = DMA2D_CSS_422;
        inputLineOffset = JPEG_info.ImageWidth % 16;
        if (inputLineOffset != 0)
            inputLineOffset = 16 - inputLineOffset;
    }

    DMA2D_Handle.Init.Mode = DMA2D_M2M_BLEND_BG;
    DMA2D_Handle.Init.ColorMode = DMA2D_OUTPUT_RGB565;
    DMA2D_Handle.Init.OutputOffset = FrameBufferMode ? (LCD_X_SIZE - JPEG_info.ImageWidth) : 0;
    DMA2D_Handle.Init.AlphaInverted = DMA2D_REGULAR_ALPHA;
    DMA2D_Handle.Init.RedBlueSwap = DMA2D_RB_REGULAR;
    DMA2D_Handle.Init.LineOffsetMode = DMA2D_LOM_PIXELS;
    DMA2D_Handle.XferCpltCallback = NULL;
    return HAL_DMA2D_Init(&DMA2D_Handle) == HAL_OK ? 0 : -1;
}

static int COPY_JpegOutConfigLayers(void)
{
    DMA2D_Handle.LayerCfg[1].AlphaMode = DMA2D_REPLACE_ALPHA;
    DMA2D_Handle.LayerCfg[1].InputAlpha = ForegroundAlpha;
    DMA2D_Handle.LayerCfg[1].InputColorMode = DMA2D_INPUT_YCBCR;
    DMA2D_Handle.LayerCfg[1].ChromaSubSampling = cssMode;
    DMA2D_Handle.LayerCfg[1].InputOffset = inputLineOffset;
    DMA2D_Handle.LayerCfg[1].RedBlueSwap = DMA2D_RB_REGULAR;
    DMA2D_Handle.LayerCfg[1].AlphaInverted = DMA2D_REGULAR_ALPHA;
    if (HAL_DMA2D_ConfigLayer(&DMA2D_Handle, 1) != HAL_OK)
        return -1;

    DMA2D_Handle.LayerCfg[0].AlphaMode = DMA2D_REPLACE_ALPHA;
    DMA2D_Handle.LayerCfg[0].InputAlpha = 0xFF000000;
    DMA2D_Handle.LayerCfg[0].InputColorMode = DMA2D_INPUT_A8;
    DMA2D_Handle.LayerCfg[0].InputOffset = 0;
    DMA2D_Handle.LayerCfg[0].RedBlueSwap = DMA2D_RB_REGULAR;
    DMA2D_Handle.LayerCfg[0].AlphaInverted = DMA2D_REGULAR_ALPHA;
    return HAL_DMA2D_ConfigLayer(&DMA2D_Handle, 0) == HAL_OK ? 0 : -1;
}

static void COPY_JpegOut(void)
{
    uint32_t destination;

    if (COPY_JpegOutInit() != 0 || COPY_JpegOutConfigLayers() != 0)
        return;

    destination = FrameBufferAddress + ((yPos * LCD_X_SIZE) + xPos) * 2u;
    /* LCD FB is uncached; only the YCbCr work buffer lives in cached RAM_EMU.
     * A full SCB_CleanInvalidateDCache() also evicts the PCM ring the SAI ISR
     * is reading — extra AXI latency, and a long stall with no audio feed. */
    SCB_CleanDCache_by_Addr((uint32_t *)JPEGBufferAddress,
                            (int32_t)((JPEGBufferSize + 31u) & ~31u));
    if (HAL_DMA2D_Start(&DMA2D_Handle, JPEGBufferAddress, destination,
                        JPEG_info.ImageWidth, JPEG_info.ImageHeight) != HAL_OK)
        return;
    {
        uint32_t t0 = HAL_GetTick();
        for (;;) {
            HAL_StatusTypeDef st = HAL_DMA2D_PollForTransfer(&DMA2D_Handle, 1);
            if (st == HAL_OK)
                break;
            video_jpeg_poll();
            wdog_refresh();
            if (st != HAL_TIMEOUT || (HAL_GetTick() - t0) > 50u)
                break;
        }
    }
    (void)HAL_DMA2D_DeInit(&DMA2D_Handle);
}

/* HAL_JPEG_Abort's DMA branch references these; polling never takes it. */
HAL_StatusTypeDef HAL_MDMA_Abort(MDMA_HandleTypeDef *hmdma)
{
    (void)hmdma;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_MDMA_Abort_IT(MDMA_HandleTypeDef *hmdma)
{
    (void)hmdma;
    return HAL_OK;
}

HAL_StatusTypeDef HAL_MDMA_Start_IT(MDMA_HandleTypeDef *hmdma, uint32_t SrcAddress,
                                    uint32_t DstAddress, uint32_t BlockDataLength,
                                    uint32_t BlockCount)
{
    (void)hmdma;
    (void)SrcAddress;
    (void)DstAddress;
    (void)BlockDataLength;
    (void)BlockCount;
    return HAL_ERROR;
}
