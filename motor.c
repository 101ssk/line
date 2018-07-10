#include "h8-3069-iodef.h"
#include "h8-3069-int.h"

#include "lcd.h"
#include "timer.h"
#include "ad.h"

#define TIMER0 1000

/* 割り込み処理で各処理を行う頻度を決める定数 */
#define DISPTIME 100
#define CONTROLTIME 10
#define PWMTIME 1
#define ADTIME 2

#define MAXPWMCOUNT 10

/* LCD表示関連 */
/* 1段に表示できる文字数 */
#define LCDDISPSIZE 8

/* A/D変換関連 */
/* A/D変換のチャネル数とバッファサイズ */
#define ADCHNUM   4
#define ADBUFSIZE 8
/* 平均化するときのデータ個数 */
#define ADAVRNUM 4
/* チャネル指定エラー時に返す値 */
#define ADCHNONE -1

volatile int motor_time,pwm_time,ad_time,disp_time;
volatile int pwm_count;

/* LCD関係 */
volatile int disp_flag;
volatile char lcd_str_upper[LCDDISPSIZE+1];
volatile char lcd_str_lower[LCDDISPSIZE+1];

/* A/D変換関係 */
volatile unsigned char adbuf[ADCHNUM][ADBUFSIZE];
volatile int adbufdp;

unsigned int left,right;

void int_imia0(void);
void control_motor(void);
void pwm_proc(void);
void int_adi(void);
void disp_lcd(void);
void lcd_str_set(void);
int  ad_read(int ch);

int main(void){
  int i;
  ROMEMU();

  lcd_init();
  ad_init();

  //モーターポートの初期化
  PBDDR = 0x0F;

  motor_time = 0;
  pwm_time = 0;
  ad_time = 0;
  adbufdp = 0;
  disp_time = 0;
  disp_flag = 1;
  
  timer_init();        /* タイマの初期化 */
  timer_set(0,TIMER0); /* タイマ0の時間間隔をセット */
  timer_start(0);      /* タイマ0スタート */
  ENINT();             /* 全割り込み受付可 */

  /* ここでLCDに表示する文字列を初期化しておく */
  for(i = 0;i < LCDDISPSIZE;i++){
    lcd_str_upper[i] = ' ';
    lcd_str_lower[i] = ' ';
  }
  lcd_str_upper[i] = lcd_str_lower[i] = '\0';
  
  //01 後退
  //10 前進
  while(1) {
    if(disp_flag == 1){
      disp_lcd();
      disp_flag = 0;
    }
  }
}

#pragma interrupt
void int_imia0(void)
{
  /* LCD表示の処理 */
  disp_time++;
  if (disp_time >= DISPTIME){
    disp_flag = 1;
    disp_time = 0;
  }
  
  pwm_time++;
  if(pwm_time >= PWMTIME){
    pwm_proc();
    pwm_time = 0;
  }

  /* ここにA/D変換開始の処理を直接書く */
  /* A/D変換の初期化・スタート・ストップの処理関数は ad.c にある */
  ad_time++;
  if(ad_time >= ADTIME){
    ad_scan(0,1);
    ad_time = 0;
  }
  
  motor_time++;
  if(motor_time >= CONTROLTIME) {
    control_motor();
    motor_time = 0;
  }
  
  timer_intflag_reset(0); /* 割り込みフラグをクリア */
  ENINT();                /* CPUを割り込み許可状態に */
}

#pragma interrupt
void pwm_proc(void)
{
  if(pwm_count < left) {
    PBDR &= 0x01;
  } else {
    PBDR |= 0x0F;
  }
  
  if(pwm_count < right) {
    PBDR &= 0x04;
  } else {
    PBDR |= 0x0F;
  }

  pwm_count++;
  if(pwm_count >= MAXPWMCOUNT) {
    pwm_count = 0;
  }
}

void int_adi(void)
     /* A/D変換終了の割り込みハンドラ                               */
     /* 関数の名前はリンカスクリプトで固定している                   */
     /* 関数の直前に割り込みハンドラ指定の #pragma interrupt が必要  */
{
  ad_stop();    /* A/D変換の停止と変換終了フラグのクリア */

  /* ここでバッファポインタの更新を行う */
  /* 　但し、バッファの境界に注意して更新すること */
  if(adbufdp < ADBUFSIZE-1){
  	adbufdp++;
  }else{
  	adbufdp = 0;
  }
  /* ここでバッファにA/Dの各チャネルの変換データを入れる */
  /* スキャングループ 0 を指定した場合は */
  /*   A/D ch0〜3 (信号線ではAN0〜3)の値が ADDRAH〜ADDRDH に格納される */
  /* スキャングループ 1 を指定した場合は */
  /*   A/D ch4〜7 (信号線ではAN4〜7)の値が ADDRAH〜ADDRDH に格納される */
  adbuf[0][adbufdp] = ADDRAH;
  adbuf[1][adbufdp] = ADDRBH;
  adbuf[2][adbufdp] = ADDRCH;
  adbuf[3][adbufdp] = ADDRDH;

  ENINT();      /* 割り込みの許可 */
}

int ad_read(int ch)
     /* A/Dチャネル番号を引数で与えると, 指定チャネルの平均化した値を返す関数 */
     /* チャネル番号は，0〜ADCHNUM の範囲 　　　　　　　　　　　             */
     /* 戻り値は, 指定チャネルの平均化した値 (チャネル指定エラー時はADCHNONE) */
{
  int i,ad,bp;

  if ((ch > ADCHNUM) || (ch < 0)) ad = ADCHNONE; /* チャネル範囲のチェック */
  else {

    /* ここで指定チャネルのデータをバッファからADAVRNUM個取り出して平均する */
    /* データを取り出すときに、バッファの境界に注意すること */
    /* 平均した値が戻り値となる */
    bp = adbufdp;
    ad = 0;
    
    for(i = 0;i < ADAVRNUM;i++){
      if(bp <= 0){
        bp = ADBUFSIZE-1;
      }else{
        bp--;
      }
      ad += adbuf[ch][bp];
    }
  }
  return ad / ADAVRNUM; /* データの平均値を返す */
}


void control_motor(void)
{
  left = ad_read(1);
  right = ad_read(2);
}


void lcd_str_set(void)
{
  char str_upper[] = "RIGHT:";
  char str_lower[] = "LEFT:";
  int i;
  
  for(i = 0;str_upper[i] != '\0';i++){
    lcd_str_upper[i] = str_upper[i];
  }
  if(left / 100 != 0){
    lcd_str_upper[i++] = (left / 100) + '0';
    lcd_str_upper[i++] = (left / 10 % 10) + '0';
  }else if(left / 10 != 0){
    lcd_str_upper[i++] = (left / 10) + '0';
  }
  lcd_str_upper[i++] = left % 10 + '0';
  lcd_str_upper[i] = ' ';
  
  for(i = 0;str_lower[i]!= '\0';i++){
    lcd_str_lower[i] = str_lower[i];
  }
  if(right / 100 != 0){
    lcd_str_lower[i++] = (right / 100) + '0';
    lcd_str_lower[i++] = (right / 10 % 10) + '0';
  }else if(right / 10 != 0){
    lcd_str_lower[i++] = (right / 10) + '0';
  }
  lcd_str_lower[i++] = right % 10 + '0';
  lcd_str_lower[i] = ' ';
}

void disp_lcd(void){
  
  lcd_str_set();

  lcd_cursor(0,0);
  lcd_printstr(lcd_str_upper);
  lcd_cursor(0,1);
  lcd_printstr(lcd_str_lower);
}
