#ifndef INC_DEDMD
#define INC_DEDMD

/*--------------------- DMD 128x32 -------------------*/
#define DE_DMD32CPUNO     2
#define DE_DMD32CPUREGION (REGION_CPU1 + DE_DMD32CPUNO)
#define DE_DMD32ROMREGION (REGION_GFX1 + DE_DMD32CPUNO)
extern MACHINE_DRIVER_EXTERN(de_dmd32);

#define DE_DMD32ROM44(n1,chk1,n2,chk2) \
  NORMALREGION(0x10000, DE_DMD32CPUREGION) \
  NORMALREGION(0x80000, DE_DMD32ROMREGION) \
    ROM_LOAD(n1, 0x00000, 0x40000, chk1) \
    ROM_LOAD(n2, 0x40000, 0x40000, chk2)

#define DE_DMD32ROM8x(n1,chk1) \
  NORMALREGION(0x10000, DE_DMD32CPUREGION) \
  NORMALREGION(0x80000, DE_DMD32ROMREGION) \
    ROM_LOAD(n1, 0x00000, 0x80000, chk1) \

/*--------------------- DMD 192x64 -------------------*/
#define DE_DMD64CPUNO     2
#define DE_DMD64CPUREGION (REGION_CPU1  + DE_DMD64CPUNO)

extern MACHINE_DRIVER_EXTERN(de_dmd64);

#define DE_DMD64ROM88(n1,chk1,n2,chk2) \
  NORMALREGION(0x01000000, DE_DMD64CPUREGION) \
    ROM_LOAD16_BYTE(n1, 0x00000001, 0x00080000, chk1) \
    ROM_LOAD16_BYTE(n2, 0x00000000, 0x00080000, chk2)

/*--------------------- DMD 128x16 -------------------*/
#define DE_DMD16CPUNO 2
#define DE_DMD16CPUREGION (REGION_CPU1  + DE_DMD16CPUNO)
#define DE_DMD16ROMREGION (REGION_GFX1  + DE_DMD16CPUNO)
#define DE_DMD16DMDREGION (REGION_USER1 + DE_DMD16CPUNO)

extern MACHINE_DRIVER_EXTERN(de_dmd16);
#define DE_DMD16ROM1(n1,chk1) \
  NORMALREGION(0x10000, DE_DMD16CPUREGION) \
  NORMALREGION(0x400,   DE_DMD16DMDREGION) /* 16x128x2bitsx2buffers */ \
  NORMALREGION(0x20000, DE_DMD16ROMREGION) \
    ROM_LOAD(n1, 0x00000, 0x10000, chk1) \
      ROM_RELOAD(0x10000, 0x10000)

#define DE_DMD16ROM2(n1,chk1) \
  NORMALREGION(0x10000, DE_DMD16CPUREGION) \
  NORMALREGION(0x400,   DE_DMD16DMDREGION) /* 16x128x2bitsx2buffers */ \
  NORMALREGION(0x20000, DE_DMD16ROMREGION) \
    ROM_LOAD(n1, 0x00000, 0x20000, chk1)

#endif /* INC_DEDMD */

