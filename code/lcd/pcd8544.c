/*
 * Biblioteka dla LCD Nokia 3310 i innych
 * opartych o sterownik pcd8544.h
 *
 * SunRiver
 *
 * przygotowane dla toolchaina ATMEL 3.4.x
 *
 */

#include <avr32/io.h>
#include <string.h>
#include "gpio.h"
#include "pcd8544.h"

#define LCD_NOP  __asm__ __volatile__("nop")


static void PCD_Snd    ( uint8_t data, LcdCmdData cd );
static void Delay      ( void );



// --------------------------  bufor cache w SRAM 84*48 bits or 504 uint8_ts
static uint8_t  LcdCache [ LCD_CACHE_SIZE ];
static int   LcdCacheIdx;
static int   LoWaterMark;
static int   HiWaterMark;
static bool  UpdateLcd;


void PCD_Ini(void)
{
    /* gpio_set/clr_gpio_pin() enable GPIO mode + the output driver + drive
       the level in one call — collapses the AVR's separate PORT-then-DDR
       (pull-up-then-output) sequence into a single step per pin. */
    gpio_set_gpio_pin(LCD_RST_PIN);
    gpio_set_gpio_pin(LCD_DC_PIN);
    gpio_set_gpio_pin(LCD_CE_PIN);
    gpio_clr_gpio_pin(SPI_MOSI_PIN);
    gpio_clr_gpio_pin(SPI_CLK_PIN);
    gpio_clr_gpio_pin(LCD_LIGHT_PIN);   /* AVR left this pin's PORT bit untouched (reset = 0); explicit here */

    Delay();
    gpio_clr_gpio_pin(LCD_RST_PIN);
    Delay();
    gpio_set_gpio_pin(LCD_RST_PIN);

    gpio_set_gpio_pin(LCD_CE_PIN);      // deselect controller

    PCD_Snd(0x21, LCD_CMD);
    PCD_Snd(0xC8, LCD_CMD);
    PCD_Snd(0x06, LCD_CMD);
    PCD_Snd(0x13, LCD_CMD);
    PCD_Snd(0x20, LCD_CMD);
    PCD_Snd(0x0C, LCD_CMD);

    LoWaterMark = LCD_CACHE_SIZE;
    HiWaterMark = 0;

    PCD_Clr();
    PCD_Upd();
}

// PCD_Contr
// Ustawia kontrast LCD
// Wartosc  w zakresie 0x00 do 0x7F.

void PCD_Contr ( uint8_t contrast )
{
    PCD_Snd( 0x21, LCD_CMD );

    // Ustawienie poziomu kontrastu
    PCD_Snd( 0x80 | contrast, LCD_CMD );

    // Tryb - horizontal addressing mode.
    PCD_Snd( 0x20, LCD_CMD );
}

// PCD_Clr
// Czyci LCD
void PCD_Clr ( void )
{
	memset(LcdCache,0x00,LCD_CACHE_SIZE);

    LoWaterMark = 0;
    HiWaterMark = LCD_CACHE_SIZE - 1;

    // Ustawienie flagi na true
    UpdateLcd = TRUE;
}

// PCD_GotoXYFont
// Ustawienie kursora z uwzglednieniem bazowej czcionki 5x7
uint8_t PCD_GotoXYFont ( uint8_t x, uint8_t y )
{
    if( x > 14)
        return OUT_OF_BORDER;
    if( y > 6)
        return OUT_OF_BORDER;
    // Kalkulacja indexu.

    LcdCacheIdx = ( x - 1 ) * 6 + ( y - 1 ) * 84;
    return OK;
}

// PCD_Chr
// Wyswietla znaki

uint8_t PCD_Chr ( LcdFontSize size, uint8_t ch )
{
    uint8_t i, c;
    uint8_t b1, b2;
    int  tmpIdx;

    if ( LcdCacheIdx < LoWaterMark )
    {

        LoWaterMark = LcdCacheIdx;
    }

    if ( (ch < 0x20) || (ch > 0x7b) )
    {
       // konwersja znaku do wyswietlenia
        ch = 92;
    }

    if ( size == FONT_1X )
    {
        for ( i = 0; i < 5; i++ )
        {
            // kopiowanie tablicy z Flash ROM do LcdCache
            LcdCache[LcdCacheIdx++] = FontLookup[ ch - 32 ][ i ] << 1;
        }
    }
    else if ( size == FONT_2X )
    {
        tmpIdx = LcdCacheIdx - 84;

        if ( tmpIdx < LoWaterMark )
        {
            LoWaterMark = tmpIdx;
        }

        if ( tmpIdx < 0 ) return OUT_OF_BORDER;

        for ( i = 0; i < 5; i++ )
        {
            /* Copy lookup table from Flash ROM to temporary c */
            c = FontLookup[ch - 32][i] << 1;
            // du? obraz
            // pierwsza cz?c
            b1 =  (c & 0x01) * 3;
            b1 |= (c & 0x02) * 6;
            b1 |= (c & 0x04) * 12;
            b1 |= (c & 0x08) * 24;

            c >>= 4;
            // druga czesc
            b2 =  (c & 0x01) * 3;
            b2 |= (c & 0x02) * 6;
            b2 |= (c & 0x04) * 12;
            b2 |= (c & 0x08) * 24;

            /* kopiowanie obu czesci do LcdCache */
            LcdCache[tmpIdx++] = b1;
            LcdCache[tmpIdx++] = b1;
            LcdCache[tmpIdx + 82] = b2;
            LcdCache[tmpIdx + 83] = b2;
        }

        // aktualizacja po?zenia X
        LcdCacheIdx = (LcdCacheIdx + 11) % LCD_CACHE_SIZE;
    }

    if ( LcdCacheIdx > HiWaterMark )
    {
        HiWaterMark = LcdCacheIdx;
    }

    LcdCache[LcdCacheIdx] = 0x00;
    if(LcdCacheIdx == (LCD_CACHE_SIZE - 1) )
    {
        LcdCacheIdx = 0;
        return OK_WITH_WRAP;
    }
    // Inkrementacja indexu
    LcdCacheIdx++;
    return OK;
}

// PCD_Str

uint8_t PCD_Str ( LcdFontSize size, uint8_t dataArray[] )
{
    uint8_t tmpIdx=0;
    uint8_t response;
    while( dataArray[ tmpIdx ] != '\0' )
	{
        // wys?nie znaku
		response = PCD_Chr( size, dataArray[ tmpIdx ] );
        // OUT_OF_BORDER
        if( response == OUT_OF_BORDER)
            return OUT_OF_BORDER;
		tmpIdx++;
	}
    return OK;
}

// PCD_Pixel
// Wyswietla pixel o zadanych wsp?rz?nych X, Y

uint8_t PCD_Pixel ( uint8_t x, uint8_t y, LcdPixelMode mode )
{
    uint16_t  index;
    uint8_t  offset;
    uint8_t  data;

    // obliczenie ramek
    if ( x > LCD_X_RES ) return OUT_OF_BORDER;
    if ( y > LCD_Y_RES ) return OUT_OF_BORDER;

    // rekalkulacja ofsetu
    index = ( ( y / 8 ) * 84 ) + x;
    offset  = y - ( ( y / 8 ) * 8 );

    data = LcdCache[ index ];

	// Czyszczenie
    if ( mode == PIXEL_OFF )
    {
        data &= ( ~( 0x01 << offset ) );
    }

    // tryb W?czony
    else if ( mode == PIXEL_ON )
    {
        data |= ( 0x01 << offset );
    }

    // Tryb Xor
    else if ( mode  == PIXEL_XOR )
    {
        data ^= ( 0x01 << offset );
    }

    // kopiowanie rezultatu do cache
    LcdCache[ index ] = data;

    if ( index < LoWaterMark )
    {
        LoWaterMark = index;
    }

    if ( index > HiWaterMark )
    {
        HiWaterMark = index;
    }
    return OK;
}


// PCD_Line
// Pozwala na rysowanie lini  o zadanych wsp?rz?nych

uint8_t PCD_Line ( uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2, LcdPixelMode mode )
{
    int dx, dy, stepx, stepy, fraction;
    uint8_t response;

    dy = y2 - y1;
    dx = x2 - x1;

    if ( dy < 0 )
    {
        dy    = -dy;
        stepy = -1;
    }
    else
    {
        stepy = 1;
    }

    // DX negatyw
    if ( dx < 0 )
    {
        dx    = -dx;
        stepx = -1;
    }
    else
    {
        stepx = 1;
    }

    dx <<= 1;
    dy <<= 1;

    // rysowanie na pozycji
    response = PCD_Pixel( x1, y1, mode );
    if(response)
        return response;

    // zmiana lub koniec
    if ( dx > dy )
    {
        //frakcja
        fraction = dy - ( dx >> 1);
        while ( x1 != x2 )
        {
            if ( fraction >= 0 )
            {
                y1 += stepy;
                fraction -= dx;
            }
            x1 += stepx;
            fraction += dy;

            // rysowanie punktu
            response = PCD_Pixel( x1, y1, mode );
            if(response)
                return response;

        }
    }
    else
    {
        //frakcja
        fraction = dx - ( dy >> 1);
        while ( y1 != y2 )
        {
            if ( fraction >= 0 )
            {
                x1 += stepx;
                fraction -= dy;
            }
            y1 += stepy;
            fraction += dx;

            //rysowanie punktu
            response = PCD_Pixel( x1, y1, mode );
            if(response)
                return response;
        }
    }

    //ustawienie flagi

    UpdateLcd = TRUE;
    return OK;
}


// PCD_SBar
// Pozwala na rysowanie s?pka

uint8_t PCD_SBar ( uint8_t baseX, uint8_t baseY, uint8_t height, uint8_t width, LcdPixelMode mode )
{
	uint8_t tmpIdxX,tmpIdxY,tmp;

    uint8_t response;

    // Sprawdzenie ramek
	if ( ( baseX > LCD_X_RES ) || ( baseY > LCD_Y_RES ) ) return OUT_OF_BORDER;

	if ( height > baseY )
		tmp = 0;
	else
		tmp = baseY - height;

    // Rysowanie lini
	for ( tmpIdxY = tmp; tmpIdxY < baseY; tmpIdxY++ )
	{
		for ( tmpIdxX = baseX; tmpIdxX < (baseX + width); tmpIdxX++ )
        {
			response = PCD_Pixel( tmpIdxX, tmpIdxY, mode );
            if(response)
                return response;

        }
	}

    // Ustawienie flagi
	UpdateLcd = TRUE;
    return OK;
}


// PCD_Bars
// Pozwala na rysowanie wielu s?pk?

uint8_t PCD_Bars ( uint8_t data[], uint8_t numbBars, uint8_t width, uint8_t multiplier )
{
	uint8_t b;
	uint8_t tmpIdx = 0;
    uint8_t response;

	for ( b = 0;  b < numbBars ; b++ )
	{
        // obliczenie ramek (LCD_X_RES)
		if ( tmpIdx > LCD_X_RES ) return OUT_OF_BORDER;

		// kalkulacja osi x
		tmpIdx = ((width + EMPTY_SPACE_BARS) * b) + BAR_X;

		// Rysowanie s?pka
		response = PCD_SBar( tmpIdx, BAR_Y, data[ b ] * multiplier, width, PIXEL_ON);
        if(response == OUT_OF_BORDER)
            return response;
	}

	// Ustawienie flagi na True
	UpdateLcd = TRUE;
    return OK;

}

// PCD_Rect
// Rysuje prostok? o zadanych parametrach

uint8_t PCD_Rect ( uint8_t x1, uint8_t x2, uint8_t y1, uint8_t y2, LcdPixelMode mode )
{
	uint8_t tmpIdxX,tmpIdxY;
    uint8_t response;

	// Sprawdzenie ramek
	if ( ( x1 > LCD_X_RES ) ||  ( x2 > LCD_X_RES ) || ( y1 > LCD_Y_RES ) || ( y2 > LCD_Y_RES ) )
		// jesli osiagnieto ramke -- powr?
		return OUT_OF_BORDER;

	if ( ( x2 > x1 ) && ( y2 > y1 ) )
	{
		for ( tmpIdxY = y1; tmpIdxY < y2; tmpIdxY++ )
		{
			// Rysowanie lini poziomej
			for ( tmpIdxX = x1; tmpIdxX < x2; tmpIdxX++ )
            {
				// rysowanie pixeli
				response = PCD_Pixel( tmpIdxX, tmpIdxY, mode );
                if(response)
                    return response;
            }
		}

		// ustawienie flagi
		UpdateLcd = TRUE;
	}
    return OK;
}

// PCD_Img
// Wyswietla bitmape

void PCD_Img ( const uint8_t *imageData )
{

    memcpy(LcdCache,imageData,LCD_CACHE_SIZE);

    LoWaterMark = 0;
    HiWaterMark = LCD_CACHE_SIZE - 1;

    UpdateLcd = TRUE;
}

// PCD_Upd
// Kopiuje zawartosc cache do pamieci RAM wyswietlacza

void PCD_Upd ( void )
{
    int i;

    if ( LoWaterMark < 0 )
        LoWaterMark = 0;
    else if ( LoWaterMark >= LCD_CACHE_SIZE )
        LoWaterMark = LCD_CACHE_SIZE - 1;

    if ( HiWaterMark < 0 )
        HiWaterMark = 0;
    else if ( HiWaterMark >= LCD_CACHE_SIZE )
        HiWaterMark = LCD_CACHE_SIZE - 1;

    PCD_Snd( 0x80 | ( LoWaterMark % LCD_X_RES ), LCD_CMD );
    PCD_Snd( 0x40 | ( LoWaterMark / LCD_X_RES ), LCD_CMD );

    for ( i = LoWaterMark; i <= HiWaterMark; i++ )
    {
        PCD_Snd( LcdCache[ i ], LCD_DATA );
    }

    LoWaterMark = LCD_CACHE_SIZE - 1;
    HiWaterMark = 0;

	UpdateLcd = FALSE;
}


// PCD_Snd
// Wysy? dane do wyswietlacza

static void PCD_Snd(uint8_t data, LcdCmdData cd)
{
    uint8_t m;

    gpio_clr_gpio_pin(LCD_CE_PIN);      // select — active low

    if (cd == LCD_DATA)
        gpio_set_gpio_pin(LCD_DC_PIN);
    else
        gpio_clr_gpio_pin(LCD_DC_PIN);

    for (m = 0x80; m; m >>= 1)
    {
        if (data & m)
            gpio_set_gpio_pin(SPI_MOSI_PIN);
        else
            gpio_clr_gpio_pin(SPI_MOSI_PIN);

        gpio_set_gpio_pin(SPI_CLK_PIN);
        LCD_NOP;
        gpio_clr_gpio_pin(SPI_CLK_PIN);
    }

    gpio_set_gpio_pin(LCD_CE_PIN);      // deselect
}

// PCD_DrwBrd
// Rysuje ramke dooko? ekranu
/*
void PCD_DrwBrd ( void )
{
  unsigned char i, j;
  for(i=0; i<7; i++)
  {
    LCD_gotoXY (0,i);
	for(j=0; j<84; j++)
	{
	  if(j == 0 || j == 83)
		LCD_writeData (0xff);		// first and last column solid fill to make line
	  else if(i == 0)
	    LCD_writeData (0x08);		// row 0 is having only 5 bits (not 8)
	  else if(i == 6)
	    LCD_writeData (0x04);		// row 6 is having only 3 bits (not 8)
	  else
	    LCD_writeData (0x00);
	}
  }
}
*/
// Delay
// konieczne opznienie dla LCD
static void Delay ( void )
{
    int i;

    for ( i = -32000; i < 32000; i++ );
}
