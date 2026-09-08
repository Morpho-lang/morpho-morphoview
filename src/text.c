/** @file text.c
 *  @author T J Atherton
 *
 *  @brief Text rendering using freetype
 */

#include "text.h"

FT_Library ftlibrary;

/* -------------------------------------------------------
 * UTF8 handling code
 * ------------------------------------------------------- */

/** Number of bytes in the next UTF-8 character, or 0 if this is a continuation byte. */
int text_utf8numberofbytes(uint8_t *string) {
    uint8_t byte = * string;
    
    if ((byte & 0xc0) == 0x80) return 0; // In the middle of a utf8 string
    
    // Get the number of bytes from the first character
    if ((byte & 0xf8) == 0xf0) return 4;
    if ((byte & 0xf0) == 0xe0) return 3;
    if ((byte & 0xe0) == 0xc0) return 2;
    return 1;
}

/** Decode one UTF-8 character.
 * @param[in] string - bytes to decode
 * @param[out] out - code point
 * @returns true on success */
bool text_utf8decode(const uint8_t* string, int *out) {
    if (*string <= 0x7f) { // ASCII single byte value
        *out = *string;
        return true;
    }

    int value;
    uint32_t nbytes;
    if ((*string & 0xe0) == 0xc0) { // Two byte sequence: 110xxxxx 10xxxxxx.
        value = *string & 0x1f;
        nbytes = 1;
    } else if ((*string & 0xf0) == 0xe0) { // Three byte sequence: 1110xxxx     10xxxxxx 10xxxxxx.
        value = *string & 0x0f;
        nbytes = 2;
    } else if ((*string & 0xf8) == 0xf0) { // Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
        value = *string & 0x07;
        nbytes = 3;
    } else return false; // UTF8 sequence was invalid

    for (const uint8_t *s=string+1; nbytes > 0; nbytes--, s++) {
        if ((*s & 0xc0) != 0x80) return false; // Invalid UTF8 sequence
        value = value << 6 | (*s & 0x3f);
    }
    
    *out = value;

    return true;
}

/* -------------------------------------------------------
 * Testing code
 * ------------------------------------------------------- */

/** Print a FreeType bitmap (debug). */
void text_drawbitmap(FT_Bitmap *bitmap) {
  FT_Int  i, j;
  FT_Int  x_max = bitmap->width;
  FT_Int  y_max = bitmap->rows;

  for (i=0; i<y_max; i++) {
    for (j=0; j<x_max; j++) {
        char c = bitmap->buffer[i * bitmap->width + j];
        printf("%c", (c==0 ? ' ' : '*'));
    }
    printf("\n");
  }
}

/** Print a font atlas as ASCII (debug). */
void text_showtexture(textfont *font) {
    for (int i=0; i<font->skyline.height; i++) {
      for (int j=0; j<font->skyline.width; j++) {
          char c = font->texturedata[i * font->skyline.width + j];
          printf("%c", (c==0 ? ' ' : '*'));
      }
      printf("\n");
    }
}

/* -------------------------------------------------------
 * Skyline algorithm to allocate glyphs
 * ------------------------------------------------------- */

/* The skyline algorithm facilitates allocation of rectangles within a rectangular strip.
   At any time, we store the "skyline" of the uppermost edges of existing rectangles.
   New rectangles are inserted so that they occupy the lowest possible position.

    ********************************
    *                              *
    *                          ----*
    *----*                     *   *
    *    *----------*          *   *
    *    *          *----------*   *
    *    *          *          *   *
    ********************************
 
 */

DEFINE_VARRAY(textskylineentry, textskylineentry);

/** Initialize a texture skyline of the given size. */
void text_skylineinit(textskyline *skyline, int width, int height) {
    skyline->width=width;
    skyline->height=height;
    varray_textskylineentryinit(&skyline->skyline);
    
    /* Initially empty skyline */
    textskylineentry def = { .xpos = 0, .ypos = 0, .width = width, .next = TEXTSKYLINE_EMPTY};
    varray_textskylineentrywrite(&skyline->skyline, def);
}

/** Clear a texture skyline. */
void text_skylineclear(textskyline *skyline) {
    varray_textskylineentryclear(&skyline->skyline);
}

/** True if a rectangle of the given width fits at start looking right. */
bool text_skylinetestfit(textskyline *skyline, int start, int width, int ypos) {
    int w = width;
    for (int i=start; i!=TEXTSKYLINE_EMPTY && i<skyline->skyline.count; i=skyline->skyline.data[i].next) {
        textskylineentry *e = skyline->skyline.data+i;
        if (e->ypos>ypos) break; // Stop if the next rectangle is taller
        if (w<=e->width) return true; // We can fit the remaining width
        w-=e->width;
    }
    return false;
}

/** Scans right, finding the necessary vertical placement to fit a rectangle
 * @param[in] skyline - skyline data structure
 * @param[in] start - starting index
 * @param[in] width - width of rectangle to fit
 * @param[in,out] ypos - vertical position to fit; updated to give the minimum vertical position to fit this rectangle
 * @returns true on success (the rectangle could be fit at this location)   */
bool text_skylineforcefit(textskyline *skyline, int start, int width, int *ypos) {
    int w = width;
    for (int i=start; i!=TEXTSKYLINE_EMPTY && i<skyline->skyline.count; i=skyline->skyline.data[i].next) {
        textskylineentry *e = skyline->skyline.data+i;
        if (e->ypos>*ypos) *ypos = e->ypos; // Push ypos up if the next rectangle is taller.
        if (w<=e->width) return true; // We can fit the remaining width
        w-=e->width;
    }
    return false;
}

/** Fit a rectangle into the skyline starting at index start. */
bool text_skylinefit(textskyline *skyline, int start, int width, int height, int ypos) {
    int end=start; // Final element
    int w = width;
    
    for (; end!=TEXTSKYLINE_EMPTY; end=skyline->skyline.data[end].next) { // Identify the final element in the skyline
        int elwidth = TEXT_SKYLINEENTRY(skyline, end)->width;
        
        if (w<=elwidth) break;
        w-=elwidth;
    }
    
    if (start==end) { // Split the block
        if (TEXT_SKYLINEENTRY(skyline, start)->width-width>0) {
            
            textskylineentry new = { .xpos = TEXT_SKYLINEENTRY(skyline, start)->xpos+width,
                                     .ypos = TEXT_SKYLINEENTRY(skyline, start)->ypos,
                                     .width = TEXT_SKYLINEENTRY(skyline, start)->width-width,
                                     .next=TEXT_SKYLINEENTRY(skyline, start)->next };
            end = varray_textskylineentrywrite(&skyline->skyline, new);
        } else end=TEXTSKYLINE_EMPTY;
    } else { // Update the end block
        TEXT_SKYLINEENTRY(skyline, end)->width-=w;
        TEXT_SKYLINEENTRY(skyline, end)->xpos+=w;
        
        /* Mark these as deleted */
        for (int s=start; s!=end; s=skyline->skyline.data[s].next) {
            if (s!=start) skyline->skyline.data[s].xpos=TEXTSKYLINE_EMPTY;
        }
    }
    
    TEXT_SKYLINEENTRY(skyline, start)->width=width;
    TEXT_SKYLINEENTRY(skyline, start)->ypos=ypos+height;
    TEXT_SKYLINEENTRY(skyline, start)->next=end;
    
    return true;
}

/** Insert a rectangle into the skyline, growing the atlas if needed.
 * @param[in] skyline - packing state
 * @param[in] width - rectangle width
 * @param[in] height - rectangle height
 * @param[out] x - bottom-left x
 * @param[out] y - bottom-left y
 * @returns false if there is no room */
bool text_skylinesinsert(textskyline *skyline, int width, int height, int *x, int *y) {
    int best=-1, bxpos=0, bypos=skyline->height; // Start with maximum possible height
    
    /* Generate feasible space by looking right at each point */
    for (int i=0; i!=TEXTSKYLINE_EMPTY && i<skyline->skyline.count; i=skyline->skyline.data[i].next) {
        
        textskylineentry *e = &skyline->skyline.data[i];
        if (e->xpos<0) continue;
    
        if (text_skylinetestfit(skyline, i, width, e->ypos) && e->ypos<bypos) {
            best = i; bypos = e->ypos; bxpos = e->xpos;
        } else { // Try force fitting the rectangle if it doesn't fit
            int ypos = e->ypos;
            if (text_skylineforcefit(skyline, i, width, &ypos) && ypos<bypos) {
                best = i; bypos = ypos; bxpos = e->xpos;
            }
        }
    }
    
    if (text_skylinetestfit(skyline, best, width, bypos) && skyline->height>bypos+height) {
        *x=bxpos;
        *y=bypos;
        return text_skylinefit(skyline, best, width, height, bypos);
    }
    
    return false;
}

/** Grow the skyline height. */
bool text_skylineextend(textskyline *skyline, int height) {
    int extend = height;
    if (extend<TEXT_DEFAULTHEIGHT) extend = TEXT_DEFAULTHEIGHT;
    skyline->height+=extend;
    return true;
}

/* -------------------------------------------------------
 * Creating the texture
 * ------------------------------------------------------- */

/** Allocates a texture of correct size (frees any previous atlas). */
bool text_allocatetexture(textfont *font) {
    size_t size = (size_t) font->skyline.width * (size_t) font->skyline.height;
    if (font->texturedata) {
        free(font->texturedata);
        font->texturedata = NULL;
    }
    font->texturedata = malloc(sizeof(char) * size);
    if (font->texturedata) memset(font->texturedata, 0, size);
    return (font->texturedata != NULL);
}

/** Generate the texture atlas from glyph bitmaps. */
bool text_generatetexture(textfont *font) {
    if (!text_allocatetexture(font)) return false;
    
    for (int i=0; i<font->glyphs.count; i++) {
        textglyph *glyph = &font->glyphs.data[i];
        
        FT_Error error = FT_Load_Char(font->face, glyph->code, FT_LOAD_RENDER);
        if (error) return false;
        
        FT_Bitmap *bitmap=&font->face->glyph->bitmap;
        
        for (int k=0; k<glyph->height; k++) {
            for (int j=0; j<glyph->width; j++) {
                font->texturedata[(k+glyph->y) * font->skyline.width + j+glyph->x]= bitmap->buffer[k * bitmap->width + j];
            }
        }
        
    }
    font->atlas_dirty = false;
    return true;
}

/** Free the texture atlas. */
void text_cleartexture(textfont *font) {
    if (font->texturedata) free(font->texturedata);
}

/* -------------------------------------------------------
 * Manage fonts
 * ------------------------------------------------------- */

DEFINE_VARRAY(textglyph, textglyph);

/** Initialize a font structure. */
void text_fontinit(textfont *font, int width) {
    text_skylineinit(&font->skyline, width, width*3/4);
    varray_textglyphinit(&font->glyphs);
    font->texturedata=NULL;
    font->atlas_dirty=true;
}

/** Clear a font structure. */
void text_fontclear(textfont *font) {
    FT_Done_Face(font->face);
    
    text_skylineclear(&font->skyline);
    varray_textglyphclear(&font->glyphs);
    
    text_cleartexture(font);
}

/** Open a FreeType face.
 * @param[in] file - font path
 * @param[in] size - pixel size
 * @param[out] font - filled font record */
bool text_openfont(char *file, int size, textfont *font) {
    FT_Error error = FT_New_Face(ftlibrary, file, 0, &font->face);
    if (error) return false;
    
    error = FT_Set_Pixel_Sizes(font->face, 0, size);
    if (error) return false;
    
    return true;
}

/** True if the font already has a glyph for this code point. */
bool text_containscharacter(textfont *font, int code, int *indx) {
    for (int i=0; i<font->glyphs.count; i++) {
        if (font->glyphs.data[i].code==code) {
            if (indx) *indx=i;
            return true;
        }
    }
    return false;
}

/** Add a glyph to a font atlas if it is not already present.
 * @param[in] font - font record
 * @param[in] code - Unicode code point */
bool text_addcharacter(textfont *font, int code) {
    if (text_containscharacter(font, code, NULL)) return true;
    
    FT_Error error = FT_Load_Char(font->face, code, FT_LOAD_RENDER);
    if (error) return false;
    
    textglyph glyph;
    glyph.code=code;
    glyph.width=font->face->glyph->bitmap.width;
    glyph.height=font->face->glyph->bitmap.rows;
    glyph.bearingx=font->face->glyph->bitmap_left;
    glyph.bearingy=font->face->glyph->bitmap_top;
    glyph.advance=(unsigned int) font->face->glyph->advance.x;
    
    /* Allocate space in the texture */
    while (!text_skylinesinsert(&font->skyline, glyph.width+1, glyph.height+1, &glyph.x, &glyph.y)) {
        
        if (!text_skylineextend(&font->skyline, glyph.height+1)) {
            printf("Could not allocate space in font texture atlas.\n");
            return false;
        }
    }
    
    varray_textglyphwrite(&font->glyphs, glyph);
    font->atlas_dirty = true;
    
    //text_drawbitmap(&font->face->glyph->bitmap);
    
    return true;
}

/** Ensure the font has glyphs for every character in text. */
bool text_prepare(textfont *font, char *text) {
    for (uint8_t *c = (uint8_t *) text; *c!='\0'; ) {
        int code;
        if (!text_utf8decode(c, &code)) return false;
        if (!text_addcharacter(font, code)) return false;
        
        c+=text_utf8numberofbytes(c);
    }

    return true;
}

/** Find the glyph for the next UTF-8 character.
 * @param[in] font - font
 * @param[in] string - remaining text
 * @param[out] glyph - glyph metrics
 * @param[out] next - advanced past this character */
bool text_findglyph(textfont *font, char *string, textglyph *glyph, char **next) {
    uint8_t *c = (uint8_t *) string;
    int code;
    if (!text_utf8decode(c, &code)) return false;
    
    for (int i=0; i<font->glyphs.count; i++) {
        if (font->glyphs.data[i].code==code) {
            *glyph = font->glyphs.data[i];
            if (next) *next = string + text_utf8numberofbytes(c);
            return true;
        }
    }
    
    return false;
}

/* -------------------------------------------------------
 * Initialization
 * ------------------------------------------------------- */

/** Initialize FreeType. */
void text_initialize(void) {
    FT_Init_FreeType(&ftlibrary);
}

/** Shut down FreeType. */
void text_finalize(void) {
    FT_Done_FreeType(ftlibrary);
}

/** Test font system */
void text_test(textfont *font) {
    text_fontinit(font, 100);
    text_openfont("/Library/Fonts/Arial Unicode.ttf", 32, font);
    //text_openfont("/System/Library/Fonts/Helvetica.ttc", 32, &font);
    
    text_prepare(font, "Olá mundo! Hello world!");
    
    text_generatetexture(font);
    text_showtexture(font);
    
    text_fontclear(font);
}
