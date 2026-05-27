#ifdef __has_include
    #if __has_include("lvgl.h")
        #ifndef LV_LVGL_H_INCLUDE_SIMPLE
            #define LV_LVGL_H_INCLUDE_SIMPLE
        #endif
    #endif
#endif

#if defined(LV_LVGL_H_INCLUDE_SIMPLE)
    #include "lvgl.h"
#else
    #include "lvgl/lvgl.h"
#endif


#ifndef LV_ATTRIBUTE_MEM_ALIGN
#define LV_ATTRIBUTE_MEM_ALIGN
#endif

#ifndef LV_ATTRIBUTE_IMG_CTRL
#define LV_ATTRIBUTE_IMG_CTRL
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_CTRL uint8_t Ctrl_map[] = {
  0xff, 0xff, 0xff, 0xff, 	/*Color of index 0*/
  0x00, 0x00, 0x00, 0xff, 	/*Color of index 1*/

  0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 
  0x00, 0x18, 0x00, 
  0x00, 0x3c, 0x00, 
  0x00, 0x7e, 0x00, 
  0x00, 0xff, 0x00, 
  0x01, 0xff, 0x80, 
  0x03, 0xef, 0xc0, 
  0x07, 0xc7, 0xe0, 
  0x0f, 0x83, 0xf0, 
  0x1f, 0x01, 0xf8, 
  0x3e, 0x00, 0xfc, 
  0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 
};

const lv_img_dsc_t Ctrl = {
  .header.cf = LV_IMG_CF_INDEXED_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 24,
  .header.h = 14,
  .data_size = 50,
  .data = Ctrl_map,
};
