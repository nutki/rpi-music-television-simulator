#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>


#define BIT(x) (1<<(x))

/* VEC Registers */
#define VEC_REVID			0x100

#define VEC_CONFIG0			0x104
#define VEC_CONFIG0_YDEL_MASK		GENMASK(28, 26)
#define VEC_CONFIG0_YDEL(x)		((x) << 26)
#define VEC_CONFIG0_CDEL_MASK		GENMASK(25, 24)
#define VEC_CONFIG0_CDEL(x)		((x) << 24)
#define VEC_CONFIG0_PBPR_FIL		BIT(18)
#define VEC_CONFIG0_CHROMA_GAIN_MASK	GENMASK(17, 16)
#define VEC_CONFIG0_CHROMA_GAIN_UNITY	(0 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_32	(1 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_16	(2 << 16)
#define VEC_CONFIG0_CHROMA_GAIN_1_8	(3 << 16)
#define VEC_CONFIG0_CBURST_GAIN_MASK	GENMASK(14, 13)
#define VEC_CONFIG0_CBURST_GAIN_UNITY	(0 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_128	(1 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_64	(2 << 13)
#define VEC_CONFIG0_CBURST_GAIN_1_32	(3 << 13)
#define VEC_CONFIG0_CHRBW1		BIT(11)
#define VEC_CONFIG0_CHRBW0		BIT(10)
#define VEC_CONFIG0_SYNCDIS		BIT(9)
#define VEC_CONFIG0_BURDIS		BIT(8)
#define VEC_CONFIG0_CHRDIS		BIT(7)
#define VEC_CONFIG0_PDEN		BIT(6)
#define VEC_CONFIG0_YCDELAY		BIT(4)
#define VEC_CONFIG0_RAMPEN		BIT(2)
#define VEC_CONFIG0_YCDIS		BIT(2)
#define VEC_CONFIG0_STD_MASK		GENMASK(1, 0)
#define VEC_CONFIG0_NTSC_STD		0
#define VEC_CONFIG0_PAL_BDGHI_STD	1
#define VEC_CONFIG0_PAL_N_STD		3


#define VEC_CONFIG1			0x188
#define VEC_CONFIG_VEC_RESYNC_OFF	BIT(18)
#define VEC_CONFIG_RGB219		BIT(17)
#define VEC_CONFIG_CBAR_EN		BIT(16)
#define VEC_CONFIG_TC_OBB		BIT(15)
#define VEC_CONFIG1_OUTPUT_MODE_MASK	GENMASK(12, 10)
#define VEC_CONFIG1_C_Y_CVBS		(0 << 10)
#define VEC_CONFIG1_CVBS_Y_C		(1 << 10)
#define VEC_CONFIG1_PR_Y_PB		(2 << 10)
#define VEC_CONFIG1_RGB			(4 << 10)
#define VEC_CONFIG1_Y_C_CVBS		(5 << 10)
#define VEC_CONFIG1_C_CVBS_Y		(6 << 10)
#define VEC_CONFIG1_C_CVBS_CVBS		(7 << 10)
#define VEC_CONFIG1_DIS_CHR		BIT(9)
#define VEC_CONFIG1_DIS_LUMA		BIT(8)
#define VEC_CONFIG1_YCBCR_IN		BIT(6)
#define VEC_CONFIG1_DITHER_TYPE_LFSR	0
#define VEC_CONFIG1_DITHER_TYPE_COUNTER	BIT(5)
#define VEC_CONFIG1_DITHER_EN		BIT(4)
#define VEC_CONFIG1_CYDELAY		BIT(3)
#define VEC_CONFIG1_LUMADIS		BIT(2)
#define VEC_CONFIG1_COMPDIS		BIT(1)
#define VEC_CONFIG1_CUSTOM_FREQ		BIT(0)

#define PIXELVALVE2_VERTA 0x14
#define PIXELVALVE2_VERTB 0x18
#define PIXELVALVE2_VERTA_EVEN 0x1c
#define PIXELVALVE2_VERTB_EVEN 0x20

#define MAP_SIZE 4096UL
#define MAP_MASK (MAP_SIZE - 1)
#include <byteswap.h>
 
unsigned int get_reg_base() {
    unsigned int reg;
    int f = open("/proc/device-tree/soc/ranges", O_RDONLY);
    read(f, &reg, sizeof(reg));
    read(f, &reg, sizeof(reg));
    reg = bswap_32(reg);
    printf("Detected register base address: 0x%08x\n", reg);
    return reg;
}

int main(int argc, char **argv) {
    int fd;
    if((fd = open("/dev/mem", O_RDWR | O_SYNC)) == -1) {
        perror("Error opening /dev/mem (try sudo)");
        return -1;
    }
    // memmap VEC regisister region
    unsigned int reg_base = get_reg_base();
    unsigned int *vec_regs = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, reg_base + 0x806000);
    unsigned int *pixelvalve2_regs = mmap(0, MAP_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, reg_base + 0x807000);
    int cbar = 0, burst = 0, chroma = 0, pos = -1;
    printf("Videocore chip revision: %x\n", vec_regs[VEC_REVID/4]);
    unsigned int vert_b = pixelvalve2_regs[PIXELVALVE2_VERTB/4];
    unsigned int vert_a = pixelvalve2_regs[PIXELVALVE2_VERTA/4];
    unsigned int vert_b_even = pixelvalve2_regs[PIXELVALVE2_VERTB_EVEN/4];
    unsigned int vert_a_even = pixelvalve2_regs[PIXELVALVE2_VERTA_EVEN/4];
    unsigned int offset = (vert_a >> 16) + (vert_a_even >> 16) + (vert_a & 0xffff) + (vert_a_even & 0xffff);
    unsigned int lines = (vert_b & 0xffff) + (vert_b_even & 0xffff);
    printf("Video lines: %d%c at offset %d\n",lines, (vert_b_even & 0xffff) ? 'i' : 'p', offset);
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "+chroma")) chroma = +1;
        else if (!strcmp(argv[i], "-chroma")) chroma = -1;
        else if (!strcmp(argv[i], "+burst")) burst = +1;
        else if (!strcmp(argv[i], "-burst")) burst = -1;
        else if (!strcmp(argv[i], "+colorbars")) cbar = +1;
        else if (!strcmp(argv[i], "-colorbars")) cbar = -1;
        else if (!strcmp(argv[i], "-pos")) pos = atoi(argv[++i]);
        else {
            fprintf(stderr, "Usage: %s [[+-]chroma] [[+-]burst] [[+-]colorbars]\n", argv[0]);
            return -1;
        }
    }
    
    if (cbar < 0)
     vec_regs[VEC_CONFIG1/4] &= ~VEC_CONFIG_CBAR_EN;
    else if (cbar > 0)
     vec_regs[VEC_CONFIG1/4] |= VEC_CONFIG_CBAR_EN;

    if (burst > 0)
     vec_regs[VEC_CONFIG0/4] &= ~VEC_CONFIG0_BURDIS;
    else if (burst < 0)
     vec_regs[VEC_CONFIG0/4] |= VEC_CONFIG0_BURDIS;

    if (chroma > 0)
     vec_regs[VEC_CONFIG0/4] &= ~VEC_CONFIG0_CHRDIS;
    else if (chroma < 0)
     vec_regs[VEC_CONFIG0/4] |= VEC_CONFIG0_CHRDIS;

    if (pos >= 0) {
        int basepos1, basepos2;
        if (lines == 480) basepos1 = basepos2 = 0x10;
        else if (lines == 576) basepos1 = 0x14, basepos2 = 0x13;
        else {
            fprintf(stderr, "Mode not supported\n");
            return -1;
        }
        if (pos > 16) pos = 16;
        int shift1 = basepos1 - (vert_a >> 16) - pos;
        int shift2 = basepos2 - (vert_a_even >> 16) - pos;
        pixelvalve2_regs[PIXELVALVE2_VERTA/4] += shift1 << 16;
        pixelvalve2_regs[PIXELVALVE2_VERTB/4] -= shift1 << 16;
        pixelvalve2_regs[PIXELVALVE2_VERTA_EVEN/4] += shift2 << 16;
        pixelvalve2_regs[PIXELVALVE2_VERTB_EVEN/4] -= shift2 << 16;
    }
    return 0;
}

