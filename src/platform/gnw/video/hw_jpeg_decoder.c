/*
 * Hardware JPEG decode for the video homebrew.
 *
 * JPEG core → small YCbCr strip (RAM_EMU) → DMA2D → RGB565 back-buffer.
 * Chunked output so we never need a full-frame YCbCr buffer (~115 KiB) and
 * never park YCbCr in an LCD framebuffer (that looked like blue/green blocks).
 *
 * Polling HAL, no JPEG/MDMA IRQs — does not fight the firmware vector table.
 */

#include "hw_jpeg_decoder.h"
#include "gw_lcd.h"
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

static uint32_t s_streaming;
static uint32_t s_out_y;
static uint32_t s_bytes_per_mcu_row;
static uint32_t s_mcu_lines;
static uint32_t s_chunk_bytes;
static uint32_t cssMode;
static uint32_t inputLineOffset;
static uint32_t s_dma2d_ready;

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

static int COPY_JpegOutInit(void);
static int COPY_JpegOutConfigLayers(void);
static void COPY_JpegOut_Rect(uint32_t src, uint32_t lines, uint32_t dst_y);

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

void video_jpeg_set_work(uint32_t work, uint32_t work_size)
{
    JPEGBufferAddress = work;
    JPEGBufferSize = work_size;
}

uint32_t video_jpeg_deinit(void)
{
    (void)HAL_DMA2D_DeInit(&DMA2D_Handle);
    s_dma2d_ready = 0;
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
    s_streaming = 0;
    s_out_y = 0;
    s_dma2d_ready = 0;
    g_jpeg_hal = g_jpeg_err = g_jpeg_rej = g_jpeg_sub = g_jpeg_need = 0;
    memset(&JPEG_info, 0, sizeof(JPEG_info));

    HAL_StatusTypeDef st = HAL_JPEG_Decode(&JPEG_Handle, (uint8_t *)SrcAddress, SrcSize,
                                           (uint8_t *)JPEGBufferAddress, JPEGBufferSize,
                                           JPEG_DECODE_TIMEOUT_MS);
    wdog_refresh();
    g_jpeg_hal = (uint32_t)st;
    g_jpeg_err = JPEG_Handle.ErrorCode;
    if (s_dma2d_ready)
        (void)HAL_DMA2D_DeInit(&DMA2D_Handle);
    s_dma2d_ready = 0;
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

static uint32_t chunk_lines_from_bytes(uint32_t nbytes)
{
    if (s_bytes_per_mcu_row == 0)
        return 0;
    return (nbytes / s_bytes_per_mcu_row) * s_mcu_lines;
}

void HAL_JPEG_DataReadyCallback(JPEG_HandleTypeDef *hJPEG, uint8_t *pDataOut, uint32_t OutDataLength)
{
    if (disable_transfer || decode_rejected || OutDataLength == 0) {
        HAL_JPEG_ConfigOutputBuffer(hJPEG, (uint8_t *)JPEGBufferAddress,
                                    s_chunk_bytes ? s_chunk_bytes : JPEGBufferSize);
        return;
    }

    if (!s_streaming) {
        /* Full-frame mode: convert once in DecodeCplt. */
        return;
    }

    /* Pause output while we DMA2D this strip, then reuse the same strip. */
    (void)HAL_JPEG_Pause(hJPEG, JPEG_PAUSE_RESUME_OUTPUT);

    {
        uint32_t lines = chunk_lines_from_bytes(OutDataLength);
        if (lines > 0 && s_out_y < JPEG_info.ImageHeight) {
            if (s_out_y + lines > JPEG_info.ImageHeight)
                lines = JPEG_info.ImageHeight - s_out_y;
            COPY_JpegOut_Rect((uint32_t)pDataOut, lines, (uint32_t)yPos + s_out_y);
            s_out_y += lines;
        }
    }

    HAL_JPEG_ConfigOutputBuffer(hJPEG, (uint8_t *)JPEGBufferAddress, s_chunk_bytes);
    (void)HAL_JPEG_Resume(hJPEG, JPEG_PAUSE_RESUME_OUTPUT);
    video_jpeg_poll();
    wdog_refresh();
}

void HAL_JPEG_ErrorCallback(JPEG_HandleTypeDef *hJPEG)
{
    (void)hJPEG;
    decode_rejected = 1;
}

void HAL_JPEG_DecodeCpltCallback(JPEG_HandleTypeDef *hJPEG)
{
    (void)hJPEG;
    if (disable_transfer || decode_rejected)
        return;
    if (!s_streaming) {
        COPY_JpegOut_Rect(JPEGBufferAddress, JPEG_info.ImageHeight, yPos);
    }
    /* Streaming: strips already blitted in DataReadyCallback. */
}

void HAL_JPEG_InfoReadyCallback(JPEG_HandleTypeDef *hJPEG, JPEG_ConfTypeDef *pInfo)
{
    uint32_t ImgSize;

    (void)pInfo;
    if (HAL_OK != HAL_JPEG_GetInfo(hJPEG, &JPEG_info)) {
        decode_rejected = 1;
        g_jpeg_rej = 1;
        (void)HAL_JPEG_Pause(hJPEG, JPEG_PAUSE_RESUME_INPUT_OUTPUT);
        return;
    }

    g_jpeg_sub = JPEG_info.ChromaSubsampling;
    if (JPEG_info.ChromaSubsampling == JPEG_420_SUBSAMPLING) {
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 16) * MCU_ROUND(JPEG_info.ImageHeight, 16) * 3 / 2;
        s_mcu_lines = 16;
        s_bytes_per_mcu_row = MCU_ROUND(JPEG_info.ImageWidth, 16) * 16u * 3u / 2u;
    } else if (JPEG_info.ChromaSubsampling == JPEG_422_SUBSAMPLING) {
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 16) * MCU_ROUND(JPEG_info.ImageHeight, 8) * 2;
        s_mcu_lines = 8;
        s_bytes_per_mcu_row = MCU_ROUND(JPEG_info.ImageWidth, 16) * 8u * 2u;
    } else {
        ImgSize = MCU_ROUND(JPEG_info.ImageWidth, 8) * MCU_ROUND(JPEG_info.ImageHeight, 8) * 3;
        s_mcu_lines = 8;
        s_bytes_per_mcu_row = MCU_ROUND(JPEG_info.ImageWidth, 8) * 8u * 3u;
    }

    g_jpeg_need = ImgSize;
    s_out_y = 0;
    s_chunk_bytes = 0;
    s_streaming = (ImgSize > JPEGBufferSize) ? 1u : 0u;

    if (s_streaming) {
        if (s_bytes_per_mcu_row == 0 || JPEGBufferSize < s_bytes_per_mcu_row) {
            decode_rejected = 1;
            g_jpeg_rej = 2;
            (void)HAL_JPEG_Pause(hJPEG, JPEG_PAUSE_RESUME_INPUT_OUTPUT);
            return;
        }
        /* Only whole MCU rows — partial rows break DMA2D YCbCr pitch. */
        s_chunk_bytes = (JPEGBufferSize / s_bytes_per_mcu_row) * s_bytes_per_mcu_row;
        HAL_JPEG_ConfigOutputBuffer(hJPEG, (uint8_t *)JPEGBufferAddress, s_chunk_bytes);
    } else if (ImgSize > JPEGBufferSize) {
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

static void COPY_JpegOut_Rect(uint32_t src, uint32_t lines, uint32_t dst_y)
{
    uint32_t destination;

    if (lines == 0)
        return;

    if (!s_dma2d_ready) {
        if (COPY_JpegOutInit() != 0 || COPY_JpegOutConfigLayers() != 0)
            return;
        s_dma2d_ready = 1;
    }

    destination = FrameBufferAddress + ((dst_y * LCD_X_SIZE) + xPos) * 2u;
    SCB_CleanDCache_by_Addr((uint32_t *)src, (int32_t)((JPEGBufferSize + 31u) & ~31u));
    if (HAL_DMA2D_Start(&DMA2D_Handle, src, destination,
                        JPEG_info.ImageWidth, lines) != HAL_OK)
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
