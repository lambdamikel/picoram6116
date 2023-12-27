#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>


#include "hardware/vreg.h"

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "ssd1306_i2c.c"
#include "sd_card.h"
#include "ff.h"
#include "hardware/adc.h"
#include "pico/time.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/spi.h"

//
//
//

#define VERSION " v1.1  (C) 2023 "

//
// ADC Configuration (MPF.INI file!) 
// 

#define FILE_LENGTH 17
#define FILE_BUFF_SIZE 20
#define FILE_EXT "*.MPF" 

//
//
//

#define ADC_DEBUG_DELAY 100

volatile bool DEBUG_ADC = false; 

volatile char MACHINE[FILE_LENGTH] = "MPF-1P";
volatile char BANK_PROG[4][FILE_LENGTH]; 

volatile uint16_t CANCEL2_ADC = 0xF00; 
volatile uint16_t CANCEL_ADC  = 0xD80; 
volatile uint16_t OK_ADC      = 0xC00; 
volatile uint16_t BACK_ADC    = 0x800; 
volatile uint16_t DOWN_ADC    = 0x500; 
volatile uint16_t UP_ADC      = 0x200; 

//
//
//

#define byte uint8_t

#define DISPLAY_DELAY       200
#define DISPLAY_DELAY_LONG  500
#define DISPLAY_DELAY_SHORT 100

#define BLINK_DELAY (100 * 1000) 
#define LONG_BUTTON_DELAY (400 * 1000) 

typedef char display_line[17];
display_line file;

const char* hexStringChar[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "C", "d", "E", "F" };

#define TEXT_BUFFER_SIZE 256
char text_buffer[TEXT_BUFFER_SIZE]; 

char line1[TEXT_BUFFER_SIZE]; 
char line2[TEXT_BUFFER_SIZE]; 
char line3[TEXT_BUFFER_SIZE]; 
char line4[TEXT_BUFFER_SIZE]; 

#define LINES 4
char *screen[LINES] = {line1, line2, line3, line4}; 

//
//
//

#define PICO_DEFAULT_I2C_SDA_PIN 20
#define PICO_DEFAULT_I2C_SCL_PIN 21

#define ADR_INPUTS_START 2
#define DATA_GPIO_START (ADR_INPUTS_START + 6)
#define DATA_GPIO_END (DATA_GPIO_START + 8) 

#define SEL1 0
#define SEL2 1 
#define RESET_PIN 28

#define CE_INPUT 26
#define WE_INPUT 22

#define ADC_KEYS_INPUT 27

const uint LED_PIN = PICO_DEFAULT_LED_PIN;

//
//
//

#define MAX_BANKS 0x4

static uint8_t cur_bank = 0; 

uint8_t ram[MAX_BANKS][(uint32_t) 2048] = {};
uint8_t sdram[(uint32_t) 2048] = {};

//
// 
// 

volatile bool disabled = false;
volatile bool read = false; 
volatile bool written = false; 
volatile bool confirmed = false; 

uint32_t addr_mask = 0; 
uint32_t data_mask = 0; 
uint32_t gpio = 0; 

uint32_t low_adr = 0; 
uint32_t high_adr = 0; 

uint32_t m_adr = 0; 
uint32_t r_op = 0; 
uint32_t w_op = 0; 

uint32_t d_adr = 0; 
uint32_t dr_op = 0; 
uint32_t dw_op = 0; 

//
// Utilities 
//

unsigned char decode_hex(char c) {
  if (c >= 65 && c <= 70)
    return c - 65 + 10;
  else if (c >= 97 && c <= 102)
    return c - 97 + 10;
  else if(c >= 48 && c <= 67)
    return c - 48;
  else
    return -1;
}

unsigned char reverse_bits(unsigned char b) {
  return (b & 0b00000001) << 3 |  (b & 0b00000010) << 1 | (b & 0b00000100) >> 1 |  (b & 0b00001000) >> 3 | 
    (b & 0b00010000) << 3 |  (b & 0b00100000) << 1 | (b & 0b01000000) >> 1 |  (b & 0b10000000) >> 3 ; 
}

void clear_bank(uint8_t bank) {
  for (uint32_t adr = 0; adr < 2048; adr++) {
    ram[bank][adr] = 0; 
  }
  memset(BANK_PROG[bank], 0, FILE_LENGTH);

} 

//
// Display
//

void clear_screen() {
  memset(buf, 0, SSD1306_BUF_LEN);
  render(buf, &frame_area);
  memset(line1, 0, TEXT_BUFFER_SIZE);
  memset(line2, 0, TEXT_BUFFER_SIZE);
  memset(line3, 0, TEXT_BUFFER_SIZE);
  memset(line4, 0, TEXT_BUFFER_SIZE);
}

void clear_screen0() {
  memset(buf, 0, SSD1306_BUF_LEN);
  memset(line1, 0, TEXT_BUFFER_SIZE);
  memset(line2, 0, TEXT_BUFFER_SIZE);
  memset(line3, 0, TEXT_BUFFER_SIZE);
  memset(line4, 0, TEXT_BUFFER_SIZE);
}

void clear_line0(int line) {
  switch (line) {
  case 0 : memset(line1, 0, TEXT_BUFFER_SIZE); break; 
  case 1 : memset(line2, 0, TEXT_BUFFER_SIZE); break;
  case 2 : memset(line3, 0, TEXT_BUFFER_SIZE); break; 
  case 4 : memset(line4, 0, TEXT_BUFFER_SIZE); break;
  }
  strcpy(screen[line],  "                "); 
  WriteString(buf, 0, line*8, screen[line]);
}

void clear_line(int line) {
  clear_line0(line);
  render(buf, &frame_area); 
}

void print_string0(int x, int y, char* text, ...) {

  va_list args;
  va_start(args, text); 
  vsnprintf(text_buffer, TEXT_BUFFER_SIZE, text, args); 
  strcpy(screen[y], text_buffer); 
  WriteString(buf, x*8, y*8, text_buffer);
  va_end(args);
}

void print_string(int x, int y, char* text, ...) {
  va_list args;
  va_start(args, text); 
  vsnprintf(text_buffer, TEXT_BUFFER_SIZE, text, args); 
  strcpy(screen[y], text_buffer); 
  WriteString(buf, x*8, y*8, text_buffer);
  render(buf, &frame_area); 
}

void print_line(int x, char* text, ...) {    
  va_list args;
  va_start(args, text); 
  vsnprintf(text_buffer, TEXT_BUFFER_SIZE, text, args); 
  char* screen0 = screen[0]; 
  for (int i = 0; i < 3; i++) {
    screen[i] = screen[i+1];
    WriteString(buf, x*8, i*8, screen[i]); 
  }
  screen[3] = screen0; 
  strcpy(screen[3], text_buffer);     
  WriteString(buf, x*8, 3*8, text_buffer);
  render(buf, &frame_area); 
  va_end(args);
}

void print_line0(int x, char* text, ...) {    
  va_list args;
  va_start(args, text); 
  vsnprintf(text_buffer, TEXT_BUFFER_SIZE, text, args); 
  char* screen0 = screen[0]; 
  for (int i = 0; i < 3; i++) {
    screen[i] = screen[i+1];
    WriteString(buf, x*8, i*8, screen[i]); 
  }
  screen[3] = screen0; 
  strcpy(screen[3], text_buffer);     
  WriteString(buf, x*8, 3*8, text_buffer);
  va_end(args);
}

void print_char0(int x, int y, char c) {
  text_buffer[0] = c;
  text_buffer[1] = 0; 
  WriteString(buf, x*8, y*8, text_buffer);
}

void print_char(int x, int y, char c) {
  text_buffer[0] = c; 
  text_buffer[1] = 0; 
  WriteString(buf, x*8, y*8, text_buffer);
  render(buf, &frame_area); 
}

void disp_plot0(int x, int y) {
  SetPixel(buf, x, y, true);
}

void disp_plot(int x, int y) {
  SetPixel(buf, x, y, true);
  render(buf, &frame_area); 
}

void disp_line0(int x1, int y1, int x2, int y2 ) {
  DrawLine(buf, x1, y1, x2, y2, true);
}

void disp_line(int x1, int y1, int x2, int y2 ) {
  DrawLine(buf, x1, y1, x2, y2, true);
  render(buf, &frame_area); 
}

void render_display() {    
  render(buf, &frame_area); 
}

//
// UI Buttons 
//

typedef enum { NONE, UP, DOWN, BACK, OK, CANCEL, CANCEL2 } button_state; 

button_state read_button_state(void) { 

  adc_select_input(1); 
  uint16_t adc = adc_read();
  
  if (adc <= UP_ADC) {
    return UP; 
  } else if (adc <= DOWN_ADC) {
    return DOWN; 
  } else if (adc <= BACK_ADC) { 
    return BACK; 
  } else if (adc <= OK_ADC) {
    return OK; 
  } else if (adc <= CANCEL_ADC) {
    return CANCEL; 
  } else if (adc <= CANCEL2_ADC) {
    return CANCEL2; 
  } else {
    return NONE; 
  }

}

bool wait_for_button_release(void) {
  uint64_t last = time_us_64(); 
  while (read_button_state() != NONE) {}
  return (time_us_64() - last > LONG_BUTTON_DELAY); 
}

void wait_for_button(void) {
  while (read_button_state() == NONE) {}
  wait_for_button_release(); 
  return;
}

bool wait_for_yes_no_button(void) {
  button_state button; 
  while (true) {
    button = read_button_state();
    if (button == OK) {
      wait_for_button_release(); 
      return true;
    } else if (button == CANCEL || button == CANCEL2) {
      wait_for_button_release(); 
      return false;
    }
  }
}

//
// UI Display Loop 
//

typedef enum { OFF, ON } disp_mode; 

void display_loop() {    

  disp_mode cur_disp_mode = ON; 
  button_state buttons = NONE; 

  //
  //
  //

  if (DEBUG_ADC) {

    reset_hold(); 
    clear_screen(); 	  

    while (true) {
      adc_select_input(1); 
      print_string(0,1,"ADC:%03x       ", adc_read());
      sleep_ms(ADC_DEBUG_DELAY);       
    }    
  }

  //
  //
  // 
  
  while (true) {

    //
    //
    //

    /* 
       if (cur_disp_mode != OFF) { 

       sprintf(text_buffer, "%1x:%04x O:%02x I:%02x", cur_bank, d_adr, dr_op, dw_op); 
       WriteString(buf, 0, 0, text_buffer);
	            
       render(buf, &frame_area);
       } */ 

    //
    //
    //

    buttons = read_button_state(); 

    if (buttons != NONE ) {

      reset_hold(); 
      confirmed = false;
      disabled = true;

      // in case it is in the 2nd part of the busy loop... 
      read = true; 
      written = true;

      while (!confirmed) {};

      switch (buttons) {
      case UP: 
	// LOAD
	pgm1();
	sleep_ms(DISPLAY_DELAY);
	sleep_ms(DISPLAY_DELAY);
	if (cur_disp_mode == ON) 
	  show_info();
	else
	  clear_screen(); 	  

	break;
	
      case DOWN: 
	// SAVE
	pgm2();
	sleep_ms(DISPLAY_DELAY);
	sleep_ms(DISPLAY_DELAY);
	if (cur_disp_mode == ON) 
	  show_info();
	else
	  clear_screen(); 	  

	break;
	
      case BACK:
	// CHANGE CUR BANK

	cur_bank = (cur_bank + 1) % (MAX_BANKS);

	clear_screen();
	sprintf(text_buffer, "BANK #%1x", cur_bank); 
	WriteString(buf, 0, 0, text_buffer);
	render(buf, &frame_area);
	sleep_ms(DISPLAY_DELAY);
	sleep_ms(DISPLAY_DELAY);
	if (cur_disp_mode == ON) 
	  show_info();
	else
	  clear_screen(); 	  

	break;

      case OK:

	clear_screen();
	sprintf(text_buffer, "CLEAR BANK #%1x?", cur_bank); 
	WriteString(buf, 0, 0, text_buffer);
	render(buf, &frame_area);
	wait_for_button_release(); 

	if ( wait_for_yes_no_button()) {
	  clear_bank(cur_bank);
	  print_string(0,3,"CLEARED!");
	} else 
	  print_string(0,3,"CANCELED!");       

	sleep_ms(DISPLAY_DELAY);
	sleep_ms(DISPLAY_DELAY);
	if (cur_disp_mode == ON) 
	  show_info();
	else
	  clear_screen(); 	  
       
	break; 

      case CANCEL:
	
	if (cur_disp_mode == OFF ) 
	  cur_disp_mode = ON; 
	else
	  cur_disp_mode = OFF; 

	sleep_ms(DISPLAY_DELAY);
	sleep_ms(DISPLAY_DELAY);

	if (cur_disp_mode == ON) 
	  show_info();
	else
	  clear_screen(); 	  

	break;
	
      } 

      gpio_put(LED_PIN, 0);
      disabled = false;
      reset_release(); 

      
    }

  } 

}

//
// SD Card
//

int sd_test() {

  FRESULT fr;
  FATFS fs;
  FIL fil;
  int ret;
  char buf[FILE_BUFF_SIZE];
  char filename[] = "test02.txt";
    
  // Initialize SD card
  if (!sd_init_driver()) {
    print_string(0,0,"BAD SDCARD 1");
    while (true);
  }

  // Mount drive
  fr = f_mount(&fs, "0:", 1);
  if (fr != FR_OK) {
    print_string(0,0,"BAD SDCARD 2");
    while (true);
  }

  // Open file for writing ()
  fr = f_open(&fil, filename, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr != FR_OK) {
    print_string(0,0,"BAD SDCARD 3");
    while (true);
  }

  // Write something to file
  ret = f_printf(&fil, "SD Card\r\n");
  if (ret < 0) {
    print_string(0,0,"BAD SDCARD 4");
    f_close(&fil);
    while (true);
  }
  ret = f_printf(&fil, "works without\r\n");
  if (ret < 0) {
    print_string(0,0,"BAD SDCARD 5");
    f_close(&fil);
    while (true);
  }

  ret = f_printf(&fil, "any problems!\r\n");
  if (ret < 0) {
    print_string(0,0,"BAD SDCARD 6");
    f_close(&fil);
    while (true);
  }

  ret = f_printf(&fil, "Cool!\r\n");
  if (ret < 0) {
    print_string(0,0,"BAD SDCARD 7");
    f_close(&fil);
    while (true);
  }

  // Close file
  fr = f_close(&fil);
  if (fr != FR_OK) {
    print_string(0,0,"BAD SDCARD 8");
    while (true);
  }

  // Open file for reading
  fr = f_open(&fil, filename, FA_READ);
  if (fr != FR_OK) {
    print_string(0,0,"BAD SDCARD 9");
    while (true);
  }

  // Print every line in file over serial
  clear_screen(); 
  int i = 0;
  
  while (f_gets(buf, sizeof(buf), &fil)) {
    // print_string(0, i++ , buf);
  }

  // Close file
  fr = f_close(&fil);
  if (fr != FR_OK) {
    print_string(0,0,"BAD SDCARD 10");
    while (true);
  }

  // Unmount drive
  f_unmount("0:");

}

int sd_read_init() {

  FRESULT fr;
  FATFS fs;
  FIL fil;
  int ret;
  char filename[] = "6116.INI";
  char buf[FILE_BUFF_SIZE];
  bool skip = false; 

  clear_screen(); 
    
  // Initialize SD card
  if (!sd_init_driver()) {
    print_string(0,0,"INI - INIT");
    sleep_ms(DISPLAY_DELAY_LONG); 
    skip = true;    
  }

  // Mount drive
  if (! skip) {
    fr = f_mount(&fs, "0:", 1);
    if (fr != FR_OK) {
      print_string(0,0,"INI - MOUNT");
      sleep_ms(DISPLAY_DELAY_LONG); 
      skip = true; 
    }
  }
  
  // Open file for reading
  if (! skip) {
    fr = f_open(&fil, filename, FA_READ);
    if (fr != FR_OK) {
      print_string(0,0,"INI - OPEN");
      sleep_ms(DISPLAY_DELAY_LONG); 
      skip = true;    
    }
  }

  // 6116.INI: 
  // MPF-1P
  // F00
  // D80
  // C00
  // 800
  // 500
  // 200 
  // COUNTER1.MPF 
  // COUNTER2.MPF 
  // COUNTER3.MPF
  // COUNTER4.MPF
  // 0 

  while (! skip) {
  
    if (! f_gets(MACHINE, sizeof(MACHINE), &fil)) {
      show_error(0,0,"INI - MACHINE");
      skip = true; 
      break; 
    }
    print_line(0, MACHINE); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    //
    //
    //
    
    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - CANCEL2");
      skip = true; 
      break; 
    }
    CANCEL2_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%CANCEL2: %03x     ", CANCEL2_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - CANCEL");
      skip = true; 
      break; 
    } 
    CANCEL_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%CANCEL : %03x     ", CANCEL_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);
    
    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - OK");
      skip = true; 
      break; 
    }
    OK_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%OK     : %03x     ", OK_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - BACK");
      skip = true; 
      break; 
    } 
    BACK_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%BACK   : %03x     ", BACK_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);
  
    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - DOWN");
      skip = true; 
      break; 
    } 
    DOWN_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%DOWN   : %03x     ", DOWN_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);
      
    if (! f_gets(buf, sizeof(buf), &fil)) {
      show_error(0,0,"INI - UP");
      skip = true; 
      break; 
    } 
    UP_ADC = decode_hex(buf[0])*16*16 + decode_hex(buf[1])*16 + decode_hex(buf[2]);
    print_line(0,"%UP     : %03x     ", UP_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    //
    //
    // 

    if (! f_gets(BANK_PROG[0], sizeof(BANK_PROG[0]), &fil)) {
      show_error(0,0,"INI - PROG1");
      skip = true; 
      break; 
    }
    print_line(0,"P1: %12s", BANK_PROG[0]); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    if (! f_gets(BANK_PROG[1], sizeof(BANK_PROG[1]), &fil)) {
      show_error(0,0,"INI - PROG2");
      skip = true; 
      break; 
    }
    print_line(0,"P2: %12s", BANK_PROG[1]); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    if (! f_gets(BANK_PROG[2], sizeof(BANK_PROG[2]), &fil)) {
      show_error(0,0,"INI - PROG3");
      skip = true; 
      break; 
    }
    print_line(0,"P3:%12s", BANK_PROG[2]); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    if (! f_gets(BANK_PROG[3], sizeof(BANK_PROG[3]), &fil)) {
      show_error(0,0,"INI - PROG 4");
      skip = true; 
      break; 
    }
    print_line(0,"P4: %12s", BANK_PROG[3]); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    break;

  }

  //
  //
  //

  if (! skip && f_gets(buf, sizeof(buf), &fil)) {
    DEBUG_ADC = buf[0] == '1';
    print_line(0,"%ADC DEBUG: 01x    ", DEBUG_ADC); 
    sleep_ms(DISPLAY_DELAY_SHORT);

    clear_screen();
  }

  //
  //
  // 
    
  // Close file
  fr = f_close(&fil);
  if (fr != FR_OK) {
    show_error(0,0,"INI - CLOSE");
  }

  // Unmount drive
  f_unmount("0:");

}

//
//
//

static FRESULT fr;
static FATFS fs;
static FIL fil;
static cwdbuf[FF_LFN_BUF] = {0};

void show_error_and_halt(char* err) {
  clear_screen(); 
  print_string(0,0,err);
  while (true); 
}

void show_error(char* err) {
  clear_screen(); 
  print_string(0,0,err);
  sleep_ms(DISPLAY_DELAY_LONG);
  return; 
}

void show_error_wait_for_button(char* err) {
  show_error(err);
  wait_for_button_release(); 
  return; 
}

char* init_and_mount_sd_card(void) {

  memset(&cwdbuf, 0, FF_LFN_BUF);

  if (!sd_init_driver()) 
    show_error_and_halt("SD INIT ERR1");

  // Mount drive
  fr = f_mount(&fs, "0:", 1);
  if (fr != FR_OK) 
    show_error_and_halt("SD INIT ERR2");

  fr = f_getcwd(cwdbuf, sizeof cwdbuf);
  if (FR_OK != fr)
    show_error_and_halt("SD INIT ERR3");
    
  return &cwdbuf;

}

int count_files() {

  int count = 0;

  char const *p_dir;
   
  p_dir = init_and_mount_sd_card(); 
    
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */
  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);
    
  fr = f_findfirst(&dj, &fno, p_dir, FILE_EXT);
  if (FR_OK != fr) {
    show_error("Count Files ERR"); 
    return;
  }

  while (fr == FR_OK && fno.fname[0]) { 
    if (fno.fattrib & AM_DIR) {
      // directory 
    } else {
      count++; 
    }

    fr = f_findnext(&dj, &fno); /* Search for next item */
  }
    
  f_closedir(&dj);

  return count; 

}

void clear_file_buffer() {

  for (int i = 0; i <= 16; i++)
    file[i] = 0;

}

int select_file_no(int no) {
  int count = 0;

  clear_file_buffer();

  char const *p_dir;
   
  p_dir = init_and_mount_sd_card(); 
    
  DIR dj;      /* Directory object */
  FILINFO fno; /* File information */
  memset(&dj, 0, sizeof dj);
  memset(&fno, 0, sizeof fno);
    
  fr = f_findfirst(&dj, &fno, p_dir, FILE_EXT);
  if (FR_OK != fr) {
    show_error("File Sel ERR"); 
    return;
  }

  while (fr == FR_OK && fno.fname[0]) { 
    if (fno.fattrib & AM_DIR) {
      // directory 
    } else {
      count++;
      if (count == no) {
	// copy name into file buffer for display 
	strcpy(file, fno.fname);
	return count; 
      }
    }

    fr = f_findnext(&dj, &fno); /* Search for next item */
  }
    
  f_closedir(&dj);

  return 0;

}

int select_file() {

  clear_screen(); 
  int no = 1;
  int count = count_files();
  select_file_no(no);

  uint64_t last = time_us_64(); 
   
  bool blink = false;

  wait_for_button_release();

  while ( read_button_state() !=  OK ) {

    print_string(0,0,"Load %2d of %2d", no, count);
    print_string(0,3,"CANCEL OR OK");

    if ( time_us_64() - last > BLINK_DELAY) {

      last = time_us_64();
      blink = !blink;

      if (blink)
	print_string(0,1,"                ");
      else
	print_string(0,1,file);
    }

    switch ( read_button_state() ) {
    case UP :
      wait_for_button_release();
      if (no < count)
	no = select_file_no(no + 1);
      else
	no = select_file_no(1);
      break;
    case DOWN :
      wait_for_button_release();
      if (no > 1)
	no = select_file_no(no - 1);
      else
	no = select_file_no(count);
      break;
    case CANCEL :
    case CANCEL2 :
      wait_for_button_release();
      return -1;
    default : break;
    }
  }

  wait_for_button_release();

  return 0;

}

int create_name() {

  clear_screen(); 

  wait_for_button_release();

  clear_file_buffer();
  
  file[0] = 'A';
  file[1] = 0;

  int cursor = 0;

  uint64_t last = time_us_64(); 

  bool blink = false;

  read_button_state();

  while ( read_button_state() != OK ) {

    print_string(0,0,"Save MPF - Name:");
    print_string(0,3,"CANCEL OR OK");

    if ( time_us_64() - last > BLINK_DELAY ) {

      last = time_us_64();      
      print_string0(0,1,file);      
      blink = !blink;

      if (blink)
	print_char(cursor,1,'_');
      else
	print_char(cursor,1,file[cursor]);
    }

    switch ( read_button_state() ) {
    case UP : if (file[cursor] < 127)  { file[cursor]++; wait_for_button_release(); } break;
    case DOWN : if (file[cursor] > 32) { file[cursor]--; wait_for_button_release(); } break;
    case BACK : 
      if ( ! wait_for_button_release() ) { // short press?
	if (cursor < 7) {
	  cursor++;
	  if (! file[cursor] ) 
	    file[cursor] = file[cursor-1];
	}
      } else {
	if (cursor > 0) {
	  cursor--;
	}
      }
      break; 
    case CANCEL :
    case CANCEL2: wait_for_button_release(); return -1;
    default : break;
    }

  }

  cursor++;
  file[cursor++] = '.';
  file[cursor++] = 'M';
  file[cursor++] = 'P';
  file[cursor++] = 'F';
  file[cursor++] = 0;

  clear_screen(); 

  wait_for_button_release();
  
  return 0;

}

//
// PGM 1 - Load from SD Card
//

void load_file(bool quiet) {

  FRESULT fr;
  FATFS fs;
  FIL fil;
  int ret;
  char buf[FILE_BUFF_SIZE];
  char const *p_dir;

  if (! quiet) {
    print_string(0,0,"Loading");
    print_string(0,1,file);
    sleep_ms(DISPLAY_DELAY_LONG); 
  }

  p_dir = init_and_mount_sd_card(); 
    
  fr = f_open(&fil, file, FA_READ);
  if (fr != FR_OK) {
    sleep_ms(DISPLAY_DELAY_LONG); 
    show_error("Can't open file!");
    sleep_ms(DISPLAY_DELAY_LONG); 
    show_error(file);
    sleep_ms(DISPLAY_DELAY_LONG); 
    return; 
  }

  bool readingComment = false;
  bool readingOrigin = false;

  //
  //
  //

  uint32_t pc = 0;
  byte val = 0;
  int count = 0;
  int line = 0; 

  //
  //
  //

  while (true) {

    memset(&buf, 0, sizeof(buf));
    if (! f_gets(buf, sizeof(buf), &fil)) 
      break;

    line++; 

    int i = 0; 	

    while (true) {

      byte b = buf[i++];

      if (!b)
	break;

      if (b == '\n' || b == '\r') {
	readingComment = false;
	readingOrigin = false;
      } else if (b == '#') {
	readingComment = true;
      } else if (b == '@') {
	readingOrigin = true;
      }

      if (!readingComment && 
	  b != '\r' && b != '\n' && b != '\t' && b != ' ' &&
	  b != '@' ) { 

	int decoded = decode_hex(b);

	if ( decoded == -1) {

	  sprintf(text_buffer, "ERR LINE %05d", line);
	  show_error_wait_for_button(text_buffer); 
	  clear_screen(); 
	  fr = f_close(&fil);
	  return;
	  
	}

	if (! readingOrigin) {
	  switch ( count ) {
	  case 0 : val  = decoded * 16; count = 1;  break;
	  case 1 : val += decoded;      count = 0; sdram[pc++] = val; break; 
	  }
	} else { 

	  switch ( count ) {
	  case 0 : pc =  decoded * 16*16*16; count = 1; break;
	  case 1 : pc += decoded * 16*16;    count = 2; break; 
	  case 2 : pc += decoded * 16;       count = 3; break; 
	  case 3 : pc += decoded;            count = 0; readingOrigin = false; pc -= 0x1800; break;
	  default : break;	  
	  }
	}       
      }
    }
  } 

  //
  //
  //
  
  fr = f_close(&fil);
  if (fr != FR_OK) {
    show_error("Cant't close file!"); 
  }

  // Unmount drive
  f_unmount("0:");

  //
  //
  //

  if (! quiet) {
    clear_screen();
    print_string(0,0,"Loaded: RESET!");
    print_string(0,1,file);
    sleep_ms(DISPLAY_DELAY);
  }

  strcpy(BANK_PROG[cur_bank], file);

  //
  //
  //

  for (uint32_t b = 0; b < 2048; b++) {
    uint32_t i = 
      ((b & 0b00000000000000000000000000100000) ? 1 : 0) << 0x5 |
      ((b & 0b00000000000000000000000001000000) ? 1 : 0) << 0x0 |
      ((b & 0b00000000000000000000000010000000) ? 1 : 0) << 0x1 |
      ((b & 0b00000000000000000000000100000000) ? 1 : 0) << 0x2 |
      ((b & 0b00000000000000000000001000000000) ? 1 : 0) << 0x3 |
      ((b & 0b00000000000000000000010000000000) ? 1 : 0) << 0x4 |
      ((b & 0b00000000000000000000000000000001) ? 1 : 0) << 0x6 |
      ((b & 0b00000000000000000000000000000010) ? 1 : 0) << 0x7 |
      ((b & 0b00000000000000000000000000000100) ? 1 : 0) << 0x8 | 
      ((b & 0b00000000000000000000000000001000) ? 1 : 0) << 0x9 |
      ((b & 0b00000000000000000000000000010000) ? 1 : 0) << 0xA ;
    ram[cur_bank][b] = sdram[i];      
  }

  //
  //
  //
  
  return;

}

void pgm1() {

  int aborted = select_file();
  clear_screen(); 

  if ( aborted == -1 ) {
    print_string(0,3,"CANCELED!       ");
    sleep_ms(DISPLAY_DELAY);
    sleep_ms(DISPLAY_DELAY);
    return;
  }

  load_file(false);

}

//
//
//

void load_init_progs(void) {

  cur_bank = 0; 
  if (BANK_PROG[0][0] >= 48) {
    strcpy(file, BANK_PROG[0]);
    load_file(true);     
  }

  cur_bank = 1; 
  if (BANK_PROG[1][0] >= 48) {
    strcpy(file, BANK_PROG[1]);
    load_file(true);     
  }

  cur_bank = 2; 
  if (BANK_PROG[2][0] >= 48) {
    strcpy(file, BANK_PROG[2]);
    load_file(true);     
  }

  cur_bank = 3; 
  if (BANK_PROG[3][0] >= 48) {
    strcpy(file, BANK_PROG[3]);
    load_file(true);     
  }

  cur_bank = 0; 

}

//
// PGM 2 - Save to SD Card
//

void pgm2() {

  // 0800 
  // 000 - 7FFF
  
  for (uint32_t b = 0; b < 2048; b++) {
    uint32_t i = 
      ((b & 0b00000000000000000000000000100000) ? 1 : 0) << 0x5 |
      ((b & 0b00000000000000000000000001000000) ? 1 : 0) << 0x0 |
      ((b & 0b00000000000000000000000010000000) ? 1 : 0) << 0x1 |
      ((b & 0b00000000000000000000000100000000) ? 1 : 0) << 0x2 |
      ((b & 0b00000000000000000000001000000000) ? 1 : 0) << 0x3 |
      ((b & 0b00000000000000000000010000000000) ? 1 : 0) << 0x4 |
      ((b & 0b00000000000000000000000000000001) ? 1 : 0) << 0x6 |
      ((b & 0b00000000000000000000000000000010) ? 1 : 0) << 0x7 |
      ((b & 0b00000000000000000000000000000100) ? 1 : 0) << 0x8 | 
      ((b & 0b00000000000000000000000000001000) ? 1 : 0) << 0x9 |
      ((b & 0b00000000000000000000000000010000) ? 1 : 0) << 0xA ;
    sdram[b] = ram[cur_bank][i];      
  }

  clear_screen(); 
  int aborted = create_name();

  if ( aborted == -1 ) {
    print_string(0,3,"CANCELED!       ");
    sleep_ms(DISPLAY_DELAY);
    sleep_ms(DISPLAY_DELAY);
    return;
  }

  print_string(0,0,"Saving");
  print_string(0,1,file);

  FRESULT fr;
  FATFS fs;
  FIL fil;
  int ret;
  char buf[100];
  char const *p_dir;

  p_dir = init_and_mount_sd_card(); 

  fr = f_open(&fil, file, FA_READ);
  if (FR_OK == fr) {
    print_string(0,2,"Overwrite File?");
    if (! wait_for_yes_no_button()) {
      print_string(0,3,"*** CANCELED ***");
      sleep_ms(DISPLAY_DELAY);
      sleep_ms(DISPLAY_DELAY);
      f_close(&fil);
      return;
    } else
      print_string(0,2,"               ");
    f_close(&fil);
  } 

  //
  //
  //

  // Open file for writing ()
  fr = f_open(&fil, file, FA_WRITE | FA_CREATE_ALWAYS);
  if (fr != FR_OK) {
    show_error("WRITE ERROR 1");
    f_close(&fil);
    return; 
  }

  //
  //
  //

  byte val = 0;

  for (uint32_t pc = 0; pc < 2048 ; pc++) {
    val = sdram[pc]; 
    ret = f_printf(&fil, ( pc % 16 == 0) ? "\n%02X" : " %02X", val);
    if (ret < 0) {
      show_error("WRITE ERROR 2");
      f_close(&fil);
      return; 
    }
  }

  //
  //
  //
  
  fr = f_close(&fil);
  if (fr != FR_OK) {
    show_error_wait_for_button("CANT'T CLOSE FILE");
    clear_screen(); 
  } else {
    strcpy(BANK_PROG[cur_bank], file);
    print_string(0,3, "Saved: %s", file);
    sleep_ms(DISPLAY_DELAY);
    sleep_ms(DISPLAY_DELAY);
  }

  //
  //
  //
  
  return;

}

//
//
//

int center_string(char* string) {
  int n = strlen(string);
  int l = 8 - (n / 2);
  return l < 0 ? 0 : l; 
}

void show_logo(void) {
  
  clear_screen(); 
  
  print_string(0,0,"* PicoRAM 6161 *"); 
  print_string(0,1, VERSION);
  print_string(center_string(MACHINE),2, MACHINE); 
  print_string(0,3,"  LambdaMikel   ");
    
}

void show_info(void) {
  
  clear_screen();
  
  print_string(0,0,"%s", BANK_PROG[cur_bank]);
  print_string(0,1,"----------------"); 
  print_string(0,2,"TYPE: %s", MACHINE); 
  print_string(0,3,"BANK: %d", cur_bank); 
    
}

//
//
//

void reset_release(void) {
  gpio_set_dir(RESET_PIN, GPIO_IN);
}

void reset_hold(void) {
  gpio_set_dir(RESET_PIN, GPIO_OUT);
  gpio_put(RESET_PIN, 0);
}

//
//
//

int main() {

  // Set system clock speed.
  // 125 MHz

  set_sys_clock_pll(1100000000, 4, 1);
  
  //
  //
  //
    
  stdio_init_all();
  gpio_init(LED_PIN);
  gpio_set_dir(LED_PIN, GPIO_OUT);  

  //
  //
  //

  gpio_init(RESET_PIN);
  gpio_set_function(RESET_PIN, GPIO_FUNC_SIO); 
  reset_hold();   

  //
  //
  //

  adc_init();    
  adc_gpio_init(ADC_KEYS_INPUT);

  //
  //
  //

  ssd1306_setup();
  show_logo(); 
  sleep_ms(DISPLAY_DELAY_LONG); 
  sleep_ms(DISPLAY_DELAY_LONG); 

  //
  //
  //

  cur_bank = 0; 
    
  for (uint8_t pgm = 0; pgm < MAX_BANKS; pgm++) {
    clear_bank(pgm); 
  }

  //
  //
  //
  
  sd_read_init();
  load_init_progs();
  show_info(); 
  //sd_test(); 

  //
  //
  //

  for (gpio = ADR_INPUTS_START; gpio < DATA_GPIO_START; gpio++) {
    addr_mask |= (1 << gpio);
    gpio_init(gpio);
    gpio_set_function(gpio, GPIO_FUNC_SIO); 
    gpio_set_dir(gpio, GPIO_IN);
  }

  for (gpio = DATA_GPIO_START; gpio < DATA_GPIO_END; gpio++) {
    data_mask |= (1 << gpio);
    gpio_init(gpio);
    gpio_set_function(gpio, GPIO_FUNC_SIO); 
    gpio_set_dir(gpio, GPIO_IN);
  }

  gpio_init(SEL1);
  gpio_set_function(SEL1, GPIO_FUNC_SIO); 
  gpio_set_dir(SEL1, GPIO_OUT);
  gpio_set_outover(SEL1, GPIO_OVERRIDE_NORMAL);
  gpio_put(SEL1, 1);

  gpio_init(SEL2);
  gpio_set_function(SEL2, GPIO_FUNC_SIO); 
  gpio_set_dir(SEL2, GPIO_OUT);
  gpio_set_outover(SEL2, GPIO_OVERRIDE_NORMAL);
  gpio_put(SEL2, 1);

  gpio_init(WE_INPUT);
  gpio_set_function(WE_INPUT, GPIO_FUNC_SIO); 
  gpio_set_dir(WE_INPUT, GPIO_IN);
  gpio_set_inover(WE_INPUT, GPIO_OVERRIDE_NORMAL);

  gpio_init(CE_INPUT);
  gpio_set_function(CE_INPUT, GPIO_FUNC_SIO); 
  gpio_set_dir(CE_INPUT, GPIO_IN);
  gpio_set_inover(CE_INPUT, GPIO_OVERRIDE_NORMAL);

  //
  //
  //

  m_adr = 0;
  r_op = 0;
  w_op = 0; 

  multicore_launch_core1(display_loop);

  //
  //
  // 

  read = false; 
  written = false; 
  confirmed = false; 

  gpio_set_dir_masked(data_mask, 0);

  reset_release();   

  while (true) {   
    
    if (disabled ) {
      gpio_put(LED_PIN, 1);
      confirmed = true;
      while (disabled ) {}; 
    }

    while ( gpio_get(CE_INPUT) && ! disabled ) { // 135 ns (11000.... high clock!)    
    
      gpio_put(SEL1, 0);
      low_adr = ((gpio_get_all() & addr_mask) >> ADR_INPUTS_START ) & 0b111111; // A0 - A5
      gpio_put(SEL1, 1);
    
      gpio_put(SEL2, 0);
      high_adr = (((gpio_get_all() & addr_mask) >> ADR_INPUTS_START ) & 0b011111) << 6; // A6 - A10
      gpio_put(SEL2, 1);

      m_adr = low_adr | high_adr;

    }

    //
    //
    //

    read = false; 
    written = false; 

    while ( ! gpio_get(CE_INPUT) && ! disabled ) { 

      if (! read && ! gpio_get(WE_INPUT)) {
	gpio_set_dir_masked(data_mask, 0);
	// wait until address is stable! ~80 ns or so...
	__asm volatile (" nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n nop\n");
	r_op = (gpio_get_all() & data_mask) >> DATA_GPIO_START ;
	ram[cur_bank][m_adr] = r_op;
	read = true;  
      } else if (! written) {
	w_op = ram[cur_bank][m_adr];   
	gpio_set_dir_masked(data_mask, data_mask);	
	gpio_put_masked(data_mask, (w_op << DATA_GPIO_START));
	written = true;
      }
    }
    
    gpio_set_dir_masked(data_mask, 0);

  }
}
 
