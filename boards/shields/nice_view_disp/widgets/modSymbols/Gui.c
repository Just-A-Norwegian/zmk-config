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

#ifndef LV_ATTRIBUTE_IMG_GUI
#define LV_ATTRIBUTE_IMG_GUI
#endif

const LV_ATTRIBUTE_MEM_ALIGN LV_ATTRIBUTE_LARGE_CONST LV_ATTRIBUTE_IMG_GUI uint8_t Gui_map[] = {
  0xff, 0xff, 0xff, 0xff, 	/*Color of index 0*/
  0x00, 0x00, 0x00, 0xff, 	/*Color of index 1*/

  0x03, 0xc3, 0xc0, 
  0x07, 0xe7, 0xe0, 
  0x06, 0x66, 0x60, 
  0x06, 0x66, 0x60, 
  0x07, 0xff, 0xe0, 
  0x03, 0xff, 0xc0, 
  0x00, 0x66, 0x00, 
  0x00, 0x66, 0x00, 
  0x03, 0xff, 0xc0, 
  0x07, 0xff, 0xe0, 
  0x06, 0x66, 0x60, 
  0x06, 0x66, 0x60, 
  0x07, 0xe7, 0xe0, 
  0x03, 0xc3, 0xc0, 
};

const lv_img_dsc_t Gui = {
  .header.cf = LV_IMG_CF_INDEXED_1BIT,
  .header.always_zero = 0,
  .header.reserved = 0,
  .header.w = 24,
  .header.h = 14,
  .data_size = 50,
  .data = Gui_map,
};
