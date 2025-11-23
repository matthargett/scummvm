// Stub implementations for external libraries not available on Playdate
// These functions should never be called at runtime - they're only here
// to satisfy the linker and code signing requirements

#include <stddef.h>
#include <stdint.h>

// SDL stubs
typedef struct {
    uint8_t major;
    uint8_t minor;
    uint8_t patch;
} SDL_version;

void SDL_GetVersion(SDL_version *ver) {
    if (ver) {
        ver->major = 2;
        ver->minor = 0;
        ver->patch = 0;
    }
}

// FreeType stubs
typedef void* FT_Library;
typedef void* FT_Face;
typedef void* FT_GlyphSlot;
typedef void* FT_Bitmap;
typedef struct { int x, y; } FT_Vector;
typedef struct { int xx, xy, yx, yy; } FT_Matrix;
typedef int FT_Error;
typedef long FT_Long;
typedef unsigned long FT_ULong;
typedef int FT_Int;
typedef unsigned int FT_UInt;

FT_Error FT_Init_FreeType(FT_Library *alibrary) { return -1; }
FT_Error FT_Done_FreeType(FT_Library library) { return 0; }
FT_Error FT_Open_Face(FT_Library library, void* args, FT_Long face_index, FT_Face *aface) { return -1; }
FT_Error FT_Done_Face(FT_Face face) { return 0; }
FT_Error FT_Set_Char_Size(FT_Face face, FT_Long width, FT_Long height, FT_UInt hres, FT_UInt vres) { return -1; }
FT_UInt FT_Get_Char_Index(FT_Face face, FT_ULong charcode) { return 0; }
FT_Error FT_Load_Glyph(FT_Face face, FT_UInt glyph_index, FT_Int load_flags) { return -1; }
FT_Error FT_Render_Glyph(FT_GlyphSlot slot, int render_mode) { return -1; }
FT_Error FT_Get_Kerning(FT_Face face, FT_UInt left_glyph, FT_UInt right_glyph, FT_UInt kern_mode, FT_Vector *akerning) { return -1; }
FT_Error FT_Set_Transform(FT_Face face, FT_Matrix *matrix, FT_Vector *delta) { return -1; }
void* FT_Get_Sfnt_Table(FT_Face face, int tag) { return NULL; }
FT_Error FT_Load_Sfnt_Table(FT_Face face, FT_ULong tag, FT_Long offset, unsigned char* buffer, FT_ULong* length) { return -1; }
FT_Error FT_Bitmap_Embolden(FT_Library library, FT_Bitmap *bitmap, FT_Long xStrength, FT_Long yStrength) { return -1; }
FT_Error FT_GlyphSlot_Own_Bitmap(FT_GlyphSlot slot) { return -1; }
FT_Error FT_Face_Properties(FT_Face face, FT_UInt num_properties, void* properties) { return -1; }

// PNG stubs
typedef void* png_structp;
typedef void* png_infop;
typedef void* png_const_structp;
typedef void* png_const_infop;
typedef void (*png_error_ptr)(png_structp, const char*);
typedef void (*png_rw_ptr)(png_structp, unsigned char*, size_t);
typedef unsigned char png_byte;
typedef uint32_t png_uint_32;
typedef int32_t png_int_32;

png_structp png_create_read_struct(const char *user_png_ver, void *error_ptr, png_error_ptr error_fn, png_error_ptr warn_fn) { return NULL; }
png_infop png_create_info_struct(png_const_structp png_ptr) { return NULL; }
void png_destroy_read_struct(png_structp *png_ptr_ptr, png_infop *info_ptr_ptr, png_infop *end_info_ptr_ptr) {}
void png_set_sig_bytes(png_structp png_ptr, int num_bytes) {}
void png_set_read_fn(png_structp png_ptr, void *io_ptr, png_rw_ptr read_data_fn) {}
void png_read_info(png_structp png_ptr, png_infop info_ptr) {}
png_uint_32 png_get_IHDR(png_const_structp png_ptr, png_const_infop info_ptr, png_uint_32 *width, png_uint_32 *height, int *bit_depth, int *color_type, int *interlace_method, int *compression_method, int *filter_method) { return 0; }
png_uint_32 png_get_valid(png_const_structp png_ptr, png_const_infop info_ptr, png_uint_32 flag) { return 0; }
png_uint_32 png_get_PLTE(png_const_structp png_ptr, png_const_infop info_ptr, void **palette, int *num_palette) { return 0; }
png_uint_32 png_get_tRNS(png_const_structp png_ptr, png_infop info_ptr, png_byte **trans, int *num_trans, void **trans_values) { return 0; }
void* png_get_io_ptr(png_const_structp png_ptr) { return NULL; }
void png_set_expand(png_structp png_ptr) {}
void png_set_gray_to_rgb(png_structp png_ptr) {}
void png_set_strip_16(png_structp png_ptr) {}
void png_set_packing(png_structp png_ptr) {}
void png_set_interlace_handling(png_structp png_ptr) {}
void png_set_error_fn(png_const_structp png_ptr, void *error_ptr, png_error_ptr error_fn, png_error_ptr warning_fn) {}
void png_set_crc_action(png_structp png_ptr, int crit_action, int ancil_action) {}
void png_read_update_info(png_structp png_ptr, png_infop info_ptr) {}
void png_read_image(png_structp png_ptr, unsigned char **image) {}
void png_read_row(png_structp png_ptr, unsigned char *row, unsigned char *display_row) {}
void png_read_end(png_structp png_ptr, png_infop info_ptr) {}
